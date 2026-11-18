#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "core/document_depth.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_depth.hpp"
#include "core/vector_shape.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/layer_thumbnail_size.hpp"
#include "ui/main_window.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QTimer>
#include <QTest>
#include <QPushButton>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QContextMenuEvent>
#include <QScopeGuard>
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

void ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(96, 72, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(96, 72, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Mask"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  const auto start = canvas->widget_position_for_document_point(QPoint(20, 20));
  const auto end = canvas->widget_position_for_document_point(QPoint(70, 54));
  drag(*canvas, start, end);
  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(255, 255, 255), 8));

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  auto* row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")) != nullptr);
  auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
  CHECK(mask_thumbnail != nullptr);
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  send_mouse(*mask_thumbnail, QEvent::MouseButtonPress, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*mask_thumbnail, QEvent::MouseButtonRelease, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(30, 30)));
  CHECK(!canvas->selected_document_region().contains(QPoint(8, 8)));
  CHECK(!canvas->selected_document_region().contains(QPoint(80, 30)));
  auto* link = row->findChild<QToolButton*>(QStringLiteral("layerMaskLinkButton"));
  CHECK(link != nullptr);
  CHECK(link->isChecked());
  link->click();
  QApplication::processEvents();

  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_show_transform_controls(false);
  const auto move_start = canvas->widget_position_for_document_point(QPoint(30, 30));
  const auto move_end = canvas->widget_position_for_document_point(QPoint(50, 30));
  drag(*canvas, move_start, move_end);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 30)), QColor(255, 255, 255), 8));

  require_action(window, "layerDeleteMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 30)), QColor(220, 30, 30), 8));
  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) == nullptr);
  save_widget_artifact("ui_layer_mask_from_selection", window);
}

// Undo and redo restore a Document object; the canvas used to reset the edit
// target to the layer pixels with it, so a Ctrl+Z after a mask stroke sent the
// next stroke to the layer (GitHub issue 105). The mask stays targeted while
// the restored active layer still has one.
void ui_layer_mask_edit_target_survives_undo() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Undo"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);
  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(8, 8)),
       canvas->widget_position_for_document_point(QPoint(10, 8)));
  QApplication::processEvents();
  // Black on the mask hides the red layer there.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(255, 255, 255), 18));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(220, 30, 30), 18));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  auto* mask_label = window.findChild<QLabel*>(QStringLiteral("activeLayerMaskLabel"));
  CHECK(mask_label != nullptr && mask_label->text().contains(QStringLiteral("Target: Mask")));

  patchy::ui::MainWindowTestAccess::redo(window);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(255, 255, 255), 18));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);

  // A second stroke after the undo lands on the mask, not the layer pixels.
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(42, 40)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(255, 255, 255), 18));
  const auto& red_fill = patchy::ui::MainWindowTestAccess::document(window).layers().back();
  CHECK(red_fill.mask().has_value());

  // Undoing past the mask's creation drops the target with the mask.
  patchy::ui::MainWindowTestAccess::undo(window);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
}

void ui_layer_mask_target_paints_inverts_disables_and_applies() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Target"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  canvas->set_snap_enabled(false);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(24, 24)),
       canvas->widget_position_for_document_point(QPoint(44, 44)));
  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(255, 255, 255), 8));

  // Adding the mask targets it automatically; clicking the mask thumbnail
  // toggles mask editing off and back on.
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerMaskThumbnail"));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerMaskThumbnail"));

  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  auto* item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  auto* row = layers->itemWidget(item);
  CHECK(row != nullptr);
  auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
  CHECK(mask_thumbnail != nullptr);
  CHECK(mask_thumbnail->property("layerTargetActive").toBool());
  auto* mask_label = window.findChild<QLabel*>(QStringLiteral("activeLayerMaskLabel"));
  CHECK(mask_label != nullptr);
  CHECK(mask_label->text().contains(QStringLiteral("Target: Mask")));

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::white);
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(8, 8)),
       canvas->widget_position_for_document_point(QPoint(10, 8)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(220, 30, 30), 18));

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(220, 30, 30), 8));

  require_action(window, "layerInvertMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(255, 255, 255), 8));

  auto* disable_mask = require_action(window, "layerDisableMaskAction");
  disable_mask->trigger();
  QApplication::processEvents();
  CHECK(disable_mask->isChecked());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(220, 30, 30), 8));

  disable_mask->trigger();
  QApplication::processEvents();
  CHECK(!disable_mask->isChecked());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(255, 255, 255), 8));

  require_action(window, "layerApplyMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(255, 255, 255), 8));
  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) == nullptr);
  CHECK(mask_label->text().isEmpty());
  CHECK(!mask_label->isVisible());
  save_widget_artifact("ui_layer_mask_target_editing", window);
}

void ui_apply_layer_mask_bakes_rendered_raster_coverage_at_depth() {
  using namespace patchy;
  ui::MainWindow window;
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Float32}) {
    for (int mode = 0; mode < 3; ++mode) {
      Document document(9, 1, PixelFormat::rgb8());
      document.add_pixel_layer("Ink", solid_pixels(9, 1, PixelFormat::rgba8(), QColor(32, 84, 230)));
      convert_document_depth(document, depth);
      auto& layer = document.layers().front();
      LayerMask mask;
      mask.bounds = layer.bounds();
      mask.feather_canvas = mask.bounds;
      mask.pixels = PixelBuffer(9, 1, with_bit_depth(PixelFormat::gray8(), depth));
      mask.density = mode == 0 ? 0 : 255;
      if (mode != 0) {
        store_coverage_row(mask.pixels, 0, 4, 5, std::array{91.37F, 255.0F, 255.0F, 255.0F, 255.0F});
        mask.feather = mode == 1 ? 1.5 : 0.0;
      }
      layer.set_mask(std::move(mask));
      LayerVectorMask vector;
      vector.cache_bounds = layer.bounds();
      vector.cache = PixelBuffer(9, 1, PixelFormat::gray8());
      vector.cache.clear(128);
      layer.set_vector_mask(std::move(vector));
      window.add_document_session(std::move(document), QStringLiteral("Apply Mask"));
      show_window(window);
      const auto& doc = std::as_const(ui::MainWindowTestAccess::document(window));
      std::array<float, 9> expected{};
      for (int x = 0; x < 9; ++x) {
        expected[x] = layer_mask_alpha_at(doc.layers().front(), x, 0, LayerMaskParts{true, false});
      }
      std::vector<std::uint8_t> before, after;
      (void)Compositor{}.flatten_rgb8(doc, &before);
      require_action(window, "layerApplyMaskAction")->trigger();
      const auto& applied = doc.layers().front();
      CHECK(!applied.mask().has_value() && applied.vector_mask() != nullptr);
      (void)Compositor{}.flatten_rgb8(doc, &after);
      CHECK(before.size() == 9 && after.size() == 9);
      for (int x = 0; x < 9; ++x) {
        CHECK(std::abs(static_cast<int>(before[x]) - static_cast<int>(after[x])) <= 1);
        const float tolerance = depth == BitDepth::UInt8 ? 0.5F / 255.0F : 0.5F / 65535.0F;
        CHECK(std::abs(pixel_alpha_at(applied.pixels(), x, 0) - expected[x]) <= tolerance + 1e-7F);
      }
    }
  }
}

void ui_group_layer_mask_add_paint_and_delete() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  patchy::Layer group(document.allocate_layer_id(), "Masked Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Red Child",
                                solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30))));
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Group Mask"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  canvas->set_snap_enabled(false);

  // The group is the active layer; adding a mask targets it for editing and
  // the group row gains the mask thumbnail.
  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  auto* item = require_layer_item(*layers, QStringLiteral("Masked Folder"));
  auto* row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) != nullptr);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(220, 30, 30), 8));

  // Painting the group mask black hides the child there; elsewhere stays red.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 32)),
       canvas->widget_position_for_document_point(QPoint(34, 32)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 56)), QColor(220, 30, 30), 8));

  require_action(window, "layerDeleteMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(220, 30, 30), 8));
  item = require_layer_item(*layers, QStringLiteral("Masked Folder"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) == nullptr);
  save_widget_artifact("ui_group_layer_mask", window);
}

void ui_mask_paint_updates_distant_drop_shadow() {
  patchy::Document document(220, 220, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(220, 220, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  patchy::Layer shadowed(document.allocate_layer_id(), "Shadowed",
                         solid_pixels(40, 40, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));
  shadowed.set_bounds(patchy::Rect{120, 90, 40, 40});
  patchy::PixelBuffer mask_pixels(220, 220, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  shadowed.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 220, 220}, std::move(mask_pixels), 255, false});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 0.0F;  // light from the right; shadow lands 70px to the LEFT
  shadow.distance = 70.0F;
  shadow.size = 6.0F;
  shadowed.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(shadowed));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Shadow Repaint"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);

  // Square at (120,90)+40x40, shadow centered around (50..96, 90..130).
  CHECK(color_close(canvas_pixel(*canvas, QPoint(140, 110)), QColor(220, 30, 30), 8));
  const auto shadow_before = canvas_pixel(*canvas, QPoint(70, 110));
  CHECK(shadow_before.red() < 80 && shadow_before.green() < 80 && shadow_before.blue() < 80);

  // Hide the square by painting the mask black; the stroke stays over the
  // square, far from the shadow region, so only effect-aware invalidation
  // repaints the shadow.
  require_action(window, "layerEditMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(50);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(122, 110)),
       canvas->widget_position_for_document_point(QPoint(158, 110)));
  QApplication::processEvents();

  // The square is hidden...
  CHECK(color_close(canvas_pixel(*canvas, QPoint(140, 110)), QColor(255, 255, 255), 8));
  // ...and its drop shadow must vanish with it, even though the stroke never
  // touched the shadow's screen region (Photoshop repaints it immediately).
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 110)), QColor(255, 255, 255), 8));

  save_widget_artifact("ui_mask_paint_updates_distant_drop_shadow", window);
}

void ui_layer_mask_add_without_selection_targets_mask_and_chip_exits() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Without Selection"));
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(!canvas->has_selection());

  auto* chip = window.findChild<QToolButton*>(QStringLiteral("maskEditModeChip"));
  CHECK(chip != nullptr);
  CHECK(!chip->isVisible());
  auto* edit_mask_action = require_action(window, "layerEditMaskAction");
  CHECK(!edit_mask_action->isEnabled());

  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(220, 30, 30), 8));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  CHECK(chip->isVisible());
  CHECK(edit_mask_action->isEnabled());
  CHECK(edit_mask_action->isChecked());

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(16);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(22, 20)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), QColor(255, 255, 255), 18));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(220, 30, 30), 8));

  chip->click();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(!chip->isVisible());
  CHECK(!edit_mask_action->isChecked());

  edit_mask_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  CHECK(chip->isVisible());
  edit_mask_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(!chip->isVisible());

  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);

  edit_mask_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  require_action(window, "layerDeleteMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(!chip->isVisible());
  CHECK(!edit_mask_action->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), QColor(220, 30, 30), 8));
  save_widget_artifact("ui_layer_mask_add_without_selection", window);
}

void ui_eon_spider_canvas_matches_compositor_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("eon_spider.psd");
  patchy::test::skip_unless_exists(path);
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Eon Spider"));
  show_window(window);
  auto* canvas = require_canvas(window);
  QApplication::processEvents();
  const auto image = canvas->grab().toImage();

  // The canvas downscales to fit, so compare block means instead of pixels.
  // Region A: the drop-shadow band over the area the layer mask hides.
  // Region B: the shadow body left of the mask rect.
  const struct {
    const char* name;
    QRect rect;
  } regions[] = {
      {"shadow-over-masked-area", QRect(515, 350, 100, 260)},
      {"shadow-body", QRect(360, 440, 90, 160)},
      {"background-control", QRect(40, 60, 120, 120)},
  };
  for (const auto& region : regions) {
    double canvas_sum = 0.0;
    double flat_sum = 0.0;
    int samples = 0;
    for (int y = region.rect.top(); y < region.rect.bottom(); y += 2) {
      for (int x = region.rect.left(); x < region.rect.right(); x += 2) {
        const auto wp = canvas->widget_position_for_document_point(QPoint(x, y));
        if (!image.rect().contains(wp)) {
          continue;
        }
        const auto c = image.pixelColor(wp);
        const auto* f = flattened.pixel(x, y);
        canvas_sum += c.red() + c.green() + c.blue();
        flat_sum += f[0] + f[1] + f[2];
        ++samples;
      }
    }
    CHECK(samples > 0);
    const auto canvas_mean = canvas_sum / samples;
    const auto flat_mean = flat_sum / samples;
    std::cout << "[diag] " << region.name << ": canvas mean=" << canvas_mean << " compositor mean=" << flat_mean
              << " delta=" << (canvas_mean - flat_mean) << '\n';
    // The live canvas must show the same composite the compositor produces;
    // a large delta means the canvas tile pipeline renders effects differently.
    CHECK(std::abs(canvas_mean - flat_mean) < 30.0);
  }
  save_widget_artifact("ui_eon_spider_canvas", window);
}

void ui_layer_mask_link_button_toggles_from_mouse_clicks() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Link Click"));
  show_window(window);
  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  auto* row = layers->itemWidget(item);
  CHECK(row != nullptr);
  auto* link = row->findChild<QToolButton*>(QStringLiteral("layerMaskLinkButton"));
  CHECK(link != nullptr);
  CHECK(link->isChecked());

  // A plain mouse click must reach the button itself instead of being
  // swallowed by the list's row select/drag handling.
  send_mouse(*link, QEvent::MouseButtonPress, link->rect().center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*link, QEvent::MouseButtonRelease, link->rect().center(), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  link = row->findChild<QToolButton*>(QStringLiteral("layerMaskLinkButton"));
  CHECK(link != nullptr);
  CHECK(!link->isChecked());

  send_mouse(*link, QEvent::MouseButtonPress, link->rect().center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*link, QEvent::MouseButtonRelease, link->rect().center(), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  link = row->findChild<QToolButton*>(QStringLiteral("layerMaskLinkButton"));
  CHECK(link != nullptr);
  CHECK(link->isChecked());
  save_widget_artifact("ui_layer_mask_link_button_click", window);
}

void ui_layer_mask_overlay_and_view_modes() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Overlay"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  require_action(window, "layerAddMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(16);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(22, 20)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), QColor(255, 255, 255), 18));

  // The rubylith overlay marks the hidden area with translucent red.
  auto* overlay_action = require_action(window, "layerMaskOverlayAction");
  CHECK(overlay_action->isEnabled());
  CHECK(!overlay_action->isChecked());
  overlay_action->trigger();
  QApplication::processEvents();
  CHECK(overlay_action->isChecked());
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::Overlay);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), QColor(255, 128, 128), 16));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(220, 30, 30), 8));
  save_widget_artifact("ui_layer_mask_rubylith_overlay", window);

  // Painting while the overlay is on updates it live.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(44, 44)),
       canvas->widget_position_for_document_point(QPoint(46, 44)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(44, 44)), QColor(255, 128, 128), 16));

  // Shift-clicking the mask thumbnail disables the mask (and hides the overlay
  // because a disabled mask hides nothing).
  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerMaskThumbnail"),
                            Qt::ShiftModifier);
  CHECK(require_action(window, "layerDisableMaskAction")->isChecked());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), QColor(220, 30, 30), 8));

  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerMaskThumbnail"),
                            Qt::ShiftModifier);
  CHECK(!require_action(window, "layerDisableMaskAction")->isChecked());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), QColor(255, 128, 128), 16));

  // Alt-clicking the mask thumbnail shows the mask itself in grayscale.
  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerMaskThumbnail"),
                            Qt::AltModifier);
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::Grayscale);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  CHECK(!overlay_action->isChecked());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 20)), QColor(0, 0, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(255, 255, 255), 8));
  save_widget_artifact("ui_layer_mask_grayscale_view", window);

  // Alt-clicking again returns to the composite.
  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerMaskThumbnail"),
                            Qt::AltModifier);
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::None);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(220, 30, 30), 8));

  // Clicking the content thumbnail exits mask editing and clears the mask view.
  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerMaskThumbnail"),
                            Qt::AltModifier);
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::Grayscale);
  click_layer_row_thumbnail(*layers, QStringLiteral("Red Fill"), QStringLiteral("layerContentThumbnail"));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::None);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(220, 30, 30), 8));
}

void ui_layer_thumbnail_updates_after_brush_edit() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Thumbnail Refresh"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  auto thumbnail_center = [&]() {
    auto* item = require_layer_item(*layers, QStringLiteral("Paint"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    const auto image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    CHECK(!image.isNull());
    return image.pixelColor(image.width() / 2, image.height() / 2);
  };

  const auto before = thumbnail_center();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(240, 30, 20));
  canvas->set_brush_size(28);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(32, 32)),
       canvas->widget_position_for_document_point(QPoint(34, 32)));
  QApplication::processEvents();

  const auto after = thumbnail_center();
  // The light checkerboard already has a high red channel, so the repaint
  // shows up as green/blue collapsing under the red paint, not a red delta.
  CHECK(after.red() > 150);
  CHECK(after.green() < 90);
  CHECK(after.green() + 80 < before.green());
  CHECK(after.blue() < 90);
  save_widget_artifact("ui_layer_thumbnail_refresh", window);
}

void ui_layer_thumbnail_defers_brush_refresh_until_stroke_end() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Deferred Thumbnail Refresh"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  auto thumbnail_center = [&]() {
    auto* item = require_layer_item(*layers, QStringLiteral("Paint"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    const auto image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    CHECK(!image.isNull());
    return image.pixelColor(image.width() / 2, image.height() / 2);
  };

  const auto before = thumbnail_center();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(240, 30, 20));
  canvas->set_brush_size(28);
  const auto start = canvas->widget_position_for_document_point(QPoint(32, 32));
  const auto end = canvas->widget_position_for_document_point(QPoint(34, 32));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);

  const auto mid_stroke = thumbnail_center();
  CHECK(mid_stroke == before);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(240, 30, 20), 55));

  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto after = thumbnail_center();
  // See ui_layer_thumbnail_updates_after_brush_edit: the light checkerboard
  // makes green/blue, not red, the channel that proves the repaint.
  CHECK(after.red() > 150);
  CHECK(after.green() < 90);
  CHECK(after.green() + 80 < before.green());
  CHECK(after.blue() < 90);
}

// Layers panel thumbnail gestures (#431, #432, #433, #441). A 40x20 document: "Left" is opaque
// on its left half; "Masked" is opaque everywhere with a mask that is white on the top half.
patchy::Document thumbnail_gesture_document() {
  patchy::Document document(40, 20, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(40, 20, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  patchy::PixelBuffer left(40, 20, patchy::PixelFormat::rgba8());
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      auto* pixel = left.pixel(x, y);
      pixel[0] = 30;
      pixel[1] = 90;
      pixel[2] = 200;
      pixel[3] = x < 20 ? 255 : 0;
    }
  }
  document.add_pixel_layer("Left", std::move(left));
  auto& masked =
      document.add_pixel_layer("Masked", solid_pixels(40, 20, patchy::PixelFormat::rgba8(), QColor(220, 60, 30)));
  patchy::PixelBuffer mask(40, 20, patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    std::fill(row.begin(), row.end(), y < 10 ? std::uint8_t{255} : std::uint8_t{0});
  }
  masked.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 40, 20}, std::move(mask), 0, false});
  return document;
}

const patchy::Layer* find_named_layer(patchy::ui::MainWindow& window, const char* name) {
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  for (const auto& layer : doc.layers()) {
    if (layer.name() == name) {
      return &layer;
    }
  }
  return nullptr;
}

struct SeenMenu {
  QString name;
  QStringList actions;
  QStringList texts;
};

// Right-clicks `widget_name` in `row_name`'s row and picks `pick` (an action objectName, also one
// inside a submenu, or empty to close); returns the menu that opened, submenus listed by their
// object names. `artifact` saves the open menu.
SeenMenu right_click_layer_row_part(QListWidget& layers, const QString& row_name, const QString& widget_name,
                                    const QString& pick, const std::string& artifact = {}) {
  SeenMenu seen;
  QTimer::singleShot(0, [&seen, pick, artifact] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || !menu->isVisible()) {
        continue;
      }
      seen.name = menu->objectName();
      QAction* chosen = nullptr;
      QAction* chosen_in_submenu = nullptr;
      for (auto* action : menu->actions()) {
        seen.actions.push_back(action->isSeparator() ? QStringLiteral("-")
                               : action->menu() != nullptr ? action->menu()->objectName()
                                                           : action->objectName());
        seen.texts.push_back(action->text());
        if (!pick.isEmpty() && action->objectName() == pick) {
          chosen = action;
        }
        if (action->menu() != nullptr && !pick.isEmpty()) {
          for (auto* entry : action->menu()->actions()) {
            if (entry->objectName() == pick) {
              chosen_in_submenu = entry;
            }
          }
        }
      }
      if (!artifact.empty()) {
        save_widget_artifact(artifact, *menu);
      }
      if (chosen_in_submenu != nullptr) {
        menu->close();
        chosen_in_submenu->trigger();
      } else if (chosen != nullptr) {
        menu->setActiveAction(chosen);
        QTest::keyClick(menu, Qt::Key_Return);
      } else {
        menu->close();
      }
      return;
    }
  });
  auto* row = layers.itemWidget(require_layer_item(layers, row_name));
  CHECK(row != nullptr);
  auto* part = row != nullptr ? row->findChild<QWidget*>(widget_name) : nullptr;
  CHECK(part != nullptr);
  if (part != nullptr) {
    const auto local = part->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, part->mapToGlobal(local));
    QApplication::sendEvent(part, &event);
  }
  QApplication::processEvents();
  return seen;
}

void ui_layer_thumbnail_ctrl_click_loads_and_combines_selection() {
  patchy::ui::MainWindow window;
  window.add_document_session(thumbnail_gesture_document(), QStringLiteral("Thumbnail Clicks"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* background_item = require_layer_item(*layers, QStringLiteral("Background"));
  layers->clearSelection();
  layers->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto active_before = doc.active_layer_id();
  const auto selected = [&](int x, int y) { return canvas->selection_alpha_at(QPoint(x, y)) == 255U; };
  const auto ctrl_click = [&](const char* row, const char* part, Qt::KeyboardModifiers extra) {
    click_layer_row_thumbnail(*layers, QString::fromLatin1(row), QString::fromLatin1(part),
                              Qt::ControlModifier | extra);
  };

  // Ctrl replaces with the layer's pixels; the row selection and active layer stay put.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  ctrl_click("Left", "layerContentThumbnail", Qt::NoModifier);
  CHECK(selected(5, 5) && selected(5, 15) && !selected(30, 5));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1U);
  // Ctrl+Shift adds the mask: its top half joins.
  ctrl_click("Masked", "layerMaskThumbnail", Qt::ShiftModifier);
  CHECK(selected(5, 5) && selected(5, 15) && selected(30, 5) && !selected(30, 15));
  // Ctrl+Alt subtracts the layer's pixels: only the mask's right half is left.
  ctrl_click("Left", "layerContentThumbnail", Qt::AltModifier);
  CHECK(!selected(5, 5) && !selected(5, 15) && selected(30, 5) && !selected(30, 15));
  // Ctrl+Shift+Alt intersects: the layer's pixels within the mask.
  ctrl_click("Left", "layerContentThumbnail", Qt::NoModifier);
  ctrl_click("Masked", "layerMaskThumbnail", Qt::ShiftModifier | Qt::AltModifier);
  CHECK(selected(5, 5) && !selected(5, 15) && !selected(30, 5) && !selected(30, 15));
  CHECK(doc.active_layer_id() == active_before);
  CHECK(layers->selectedItems().size() == 1);
  CHECK(background_item->isSelected());
  // Each load is one undo step.
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(selected(5, 5) && selected(5, 15) && !selected(30, 5));
}

void ui_layer_thumbnail_right_click_opens_pixels_menu() {
  patchy::ui::MainWindow window;
  window.add_document_session(thumbnail_gesture_document(), QStringLiteral("Thumbnail Menu"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  const auto selected = [&](int x, int y) { return canvas->selection_alpha_at(QPoint(x, y)) == 255U; };

  click_layer_row_thumbnail(*layers, QStringLiteral("Masked"), QStringLiteral("layerMaskThumbnail"),
                            Qt::ControlModifier);
  const auto seen = right_click_layer_row_part(*layers, QStringLiteral("Left"), QStringLiteral("layerContentThumbnail"),
                                               QStringLiteral("layerThumbnailPixelsAddAction"),
                                               "issue432_layer_thumbnail_menu");
  CHECK(seen.name == QStringLiteral("layerThumbnailMenu"));
  CHECK(seen.actions.mid(0, 5) ==
        (QStringList{QStringLiteral("layerThumbnailPixelsSelectAction"), QStringLiteral("layerThumbnailPixelsAddAction"),
                     QStringLiteral("layerThumbnailPixelsSubtractAction"),
                     QStringLiteral("layerThumbnailPixelsIntersectAction"), QStringLiteral("-")}));
  // Then the Export submenu (#462) and Thumbnail Size, each after a separator.
  CHECK(seen.texts.value(5) == QStringLiteral("Export"));
  CHECK(seen.actions.value(6) == QStringLiteral("-"));
  CHECK(seen.texts.value(7) == QStringLiteral("Thumbnail Size"));
  // And the Thumbnail Display submenu (#481).
  CHECK(seen.texts.mid(8) == QStringList{QStringLiteral("Thumbnail Display")});
  CHECK(selected(5, 15) && selected(30, 5) && !selected(30, 15));

  click_layer_row_thumbnail(*layers, QStringLiteral("Masked"), QStringLiteral("layerMaskThumbnail"),
                            Qt::ControlModifier);
  right_click_layer_row_part(*layers, QStringLiteral("Left"), QStringLiteral("layerContentThumbnail"),
                             QStringLiteral("layerThumbnailPixelsIntersectAction"));
  CHECK(selected(5, 5) && !selected(5, 15) && !selected(30, 5));

  // The rest of the row keeps the full layer menu.
  const auto row_menu =
      right_click_layer_row_part(*layers, QStringLiteral("Left"), QStringLiteral("layerRowName"), QString());
  CHECK(row_menu.name == QStringLiteral("layerContextMenu"));
}

void ui_layer_mask_thumbnail_right_click_opens_mask_menu() {
  patchy::ui::MainWindow window;
  window.add_document_session(thumbnail_gesture_document(), QStringLiteral("Mask Menu"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  const auto selected = [&](int x, int y) { return canvas->selection_alpha_at(QPoint(x, y)) == 255U; };
  const auto mask_menu = [&](const char* pick, const std::string& artifact = {}) {
    return right_click_layer_row_part(*layers, QStringLiteral("Masked"), QStringLiteral("layerMaskThumbnail"),
                                      QString::fromLatin1(pick), artifact);
  };

  // The right-clicked row becomes the target of the mask commands.
  auto* left_item = require_layer_item(*layers, QStringLiteral("Left"));
  layers->clearSelection();
  layers->setCurrentItem(left_item);
  left_item->setSelected(true);
  QApplication::processEvents();
  auto seen = mask_menu("layerMaskThumbnailToggleAction", "issue431_mask_thumbnail_menu");
  CHECK(seen.name == QStringLiteral("layerMaskThumbnailMenu"));
  CHECK(seen.actions ==
        (QStringList{QStringLiteral("layerMaskThumbnailToggleAction"),
                     QStringLiteral("layerMaskThumbnailPropertiesAction"), QStringLiteral("-"),
                     QStringLiteral("layerDeleteMaskAction"), QStringLiteral("layerApplyMaskAction"),
                     QStringLiteral("-"), QStringLiteral("layerMaskThumbnailSelectAction"),
                     QStringLiteral("layerMaskThumbnailAddAction"), QStringLiteral("layerMaskThumbnailSubtractAction"),
                     QStringLiteral("layerMaskThumbnailIntersectAction"), QStringLiteral("-"),
                     QStringLiteral("layerThumbnailExportMenu"), QStringLiteral("-"),
                     QStringLiteral("layerThumbnailSizeMenu"), QStringLiteral("layerThumbnailDisplayMenu")}));
  CHECK(seen.texts.value(0) == QStringLiteral("Disable Layer Mask"));
  CHECK(seen.texts.value(11) == QStringLiteral("Export"));  // the layer export submenu (#462)
  // Then how thumbnails are drawn, the same submenus as the pixel thumbnail's (#481): picking
  // there changes the shared settings.
  CHECK(seen.texts.mid(13) == (QStringList{QStringLiteral("Thumbnail Size"), QStringLiteral("Thumbnail Display")}));
  {
    SettingsValueRestorer restore_size{QString::fromLatin1(patchy::ui::kLayerThumbnailSizeSettingsKey)};
    SettingsValueRestorer restore_zoom(QStringLiteral("view/zoomLayerThumbnailsToContent"));
    const bool zoomed = require_action(window, "layerThumbnailBoundsLayerAction")->isChecked();
    mask_menu(zoomed ? "layerThumbnailBoundsDocumentAction" : "layerThumbnailBoundsLayerAction");
    CHECK(patchy::ui::app_settings().value(QStringLiteral("view/zoomLayerThumbnailsToContent")).toBool() == !zoomed);
    CHECK(require_action(window, zoomed ? "layerThumbnailBoundsDocumentAction" : "layerThumbnailBoundsLayerAction")
              ->isChecked());
    mask_menu("layerThumbnailSizeLargeAction", "issue481_mask_thumbnail_menu");
    CHECK(patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerThumbnailSizeSettingsKey)).toString() ==
          QStringLiteral("large"));
    // Back to the defaults the rest of this test was written against.
    require_action(window, "layerThumbnailSizeMediumAction")->trigger();
    require_action(window, zoomed ? "layerThumbnailBoundsLayerAction" : "layerThumbnailBoundsDocumentAction")
        ->trigger();
    QApplication::processEvents();
  }
  const auto* masked = find_named_layer(window, "Masked");
  CHECK(masked != nullptr && masked->mask().has_value() && masked->mask()->disabled);
  seen = mask_menu("layerMaskThumbnailToggleAction");
  CHECK(seen.texts.value(0) == QStringLiteral("Enable Layer Mask"));
  masked = find_named_layer(window, "Masked");
  CHECK(masked != nullptr && masked->mask().has_value() && !masked->mask()->disabled);

  mask_menu("layerMaskThumbnailSelectAction");
  CHECK(selected(5, 5) && selected(30, 5) && !selected(30, 15));
  click_layer_row_thumbnail(*layers, QStringLiteral("Left"), QStringLiteral("layerContentThumbnail"),
                            Qt::ControlModifier);
  mask_menu("layerMaskThumbnailSubtractAction");
  CHECK(!selected(5, 5) && selected(5, 15) && !selected(30, 15));

  mask_menu("layerDeleteMaskAction");
  masked = find_named_layer(window, "Masked");
  CHECK(masked != nullptr && !masked->mask().has_value());
}

// Answers the footer Delete prompt with `button` (an objectName, or "cancel") when it opens.
void answer_delete_mask_prompt(bool& seen, const char* button) {
  QTimer::singleShot(0, guarded([&seen, button] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    CHECK(box != nullptr);
    if (box == nullptr) {
      return;
    }
    seen = box->objectName() == QStringLiteral("deleteLayerMaskPrompt");
    if (std::string_view(button) == "cancel") {
      box->button(QMessageBox::Cancel)->click();
      return;
    }
    auto* push = box->findChild<QPushButton*>(QString::fromLatin1(button));
    CHECK(push != nullptr);
    if (push != nullptr) {
      push->click();
    }
  }));
}

void ui_layer_footer_delete_removes_targeted_mask() {
  patchy::ui::MainWindow window;
  window.add_document_session(thumbnail_gesture_document(), QStringLiteral("Footer Delete"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  CHECK(delete_button != nullptr);
  const auto target_mask = [&] {
    click_layer_row_thumbnail(*layers, QStringLiteral("Masked"), QStringLiteral("layerContentThumbnail"));
    click_layer_row_thumbnail(*layers, QStringLiteral("Masked"), QStringLiteral("layerMaskThumbnail"));
    CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  };

  // Cancel keeps both.
  target_mask();
  bool prompt_seen = false;
  answer_delete_mask_prompt(prompt_seen, "cancel");
  delete_button->click();
  QApplication::processEvents();
  CHECK(prompt_seen);
  const auto* masked = find_named_layer(window, "Masked");
  CHECK(masked != nullptr && masked->mask().has_value());

  // Delete removes the mask and keeps the layer, whole again.
  prompt_seen = false;
  answer_delete_mask_prompt(prompt_seen, "deleteLayerMaskDeleteButton");
  delete_button->click();
  QApplication::processEvents();
  CHECK(prompt_seen);
  masked = find_named_layer(window, "Masked");
  CHECK(masked != nullptr && !masked->mask().has_value());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 15)), QColor(220, 60, 30), 8));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);

  // Apply bakes the mask into the layer's transparency.
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  target_mask();
  prompt_seen = false;
  answer_delete_mask_prompt(prompt_seen, "deleteLayerMaskApplyButton");
  delete_button->click();
  QApplication::processEvents();
  CHECK(prompt_seen);
  masked = find_named_layer(window, "Masked");
  CHECK(masked != nullptr && !masked->mask().has_value());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 15)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 5)), QColor(220, 60, 30), 8));

  // With the layer content targeted, Delete removes the layer as before.
  click_layer_row_thumbnail(*layers, QStringLiteral("Masked"), QStringLiteral("layerContentThumbnail"));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  delete_button->click();
  QApplication::processEvents();
  CHECK(find_named_layer(window, "Masked") == nullptr);
  CHECK(find_named_layer(window, "Left") != nullptr);
}

// GitHub issue 449: the layer row's right-click menu has a Vector Mask submenu beside Layer Mask
// holding the Layer > Vector Mask actions themselves; its Reveal All adds a vector mask.
void ui_layer_row_menu_vector_mask_submenu_adds_vector_mask() {
  using Access = patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  window.add_document_session(thumbnail_gesture_document(), QStringLiteral("Row Vector Mask"));
  show_window(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }
  auto* left_item = require_layer_item(*layers, QStringLiteral("Left"));
  layers->clearSelection();
  layers->setCurrentItem(left_item);
  left_item->setSelected(true);
  QApplication::processEvents();
  const auto history = Access::active_session_undo_depth(window);

  QStringList top_submenus;
  QStringList vector_actions;
  bool submenu_opened = false;
  QTimer::singleShot(0, guarded([&] {
    QMenu* row_menu = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->isVisible() && menu->objectName() == QStringLiteral("layerContextMenu")) {
        row_menu = menu;
      }
    }
    CHECK(row_menu != nullptr);
    if (row_menu == nullptr) {
      return;
    }
    QAction* vector_entry = nullptr;
    for (auto* action : row_menu->actions()) {
      if (action->menu() != nullptr) {
        top_submenus.push_back(action->menu()->objectName());
        if (action->menu()->objectName() == QStringLiteral("layerContextVectorMaskMenu")) {
          vector_entry = action;
        }
      }
    }
    if (vector_entry == nullptr) {
      row_menu->close();
      return;
    }
    auto* submenu = vector_entry->menu();
    QAction* reveal_all = nullptr;
    for (auto* action : submenu->actions()) {
      vector_actions.push_back(action->isSeparator() ? QStringLiteral("-") : action->objectName());
      if (action->objectName() == QStringLiteral("layerVectorMaskRevealAllAction")) {
        reveal_all = action;
      }
    }
    row_menu->setActiveAction(vector_entry);
    QTest::keyClick(row_menu, Qt::Key_Right);
    QApplication::processEvents();
    submenu_opened = submenu->isVisible();
    if (submenu_opened) {
      // The row menu with the submenu open over the window, both menus whole even where they
      // reach past the window's edge.
      const auto origin = window.mapToGlobal(QPoint(0, 0));
      QRect crop;
      for (QWidget* popup : {static_cast<QWidget*>(row_menu), static_cast<QWidget*>(submenu)}) {
        crop = crop.united(QRect(popup->mapToGlobal(QPoint(0, 0)) - origin, popup->size()));
      }
      crop = crop.adjusted(-12, -12, 12, 12);
      QImage shot(crop.size(), QImage::Format_ARGB32_Premultiplied);
      shot.fill(window.palette().color(QPalette::Window));
      {
        QPainter painter(&shot);
        painter.drawPixmap(-crop.topLeft(), window.grab());
        for (QWidget* popup : {static_cast<QWidget*>(row_menu), static_cast<QWidget*>(submenu)}) {
          painter.drawPixmap(popup->mapToGlobal(QPoint(0, 0)) - origin - crop.topLeft(), popup->grab());
        }
      }
      save_image_artifact("issue449_row_menu_vector_mask", shot);
    }
    if (reveal_all != nullptr && submenu_opened) {
      submenu->setActiveAction(reveal_all);
      QTest::keyClick(submenu, Qt::Key_Return);
    } else {
      row_menu->close();
    }
  }));
  auto* row = layers->itemWidget(left_item);
  auto* name = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerRowName")) : nullptr;
  CHECK(name != nullptr);
  if (name == nullptr) {
    return;
  }
  const auto local = name->rect().center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, local, name->mapToGlobal(local));
  QApplication::sendEvent(name, &event);
  QApplication::processEvents();

  CHECK(top_submenus.contains(QStringLiteral("layerContextMaskMenu")));
  CHECK(top_submenus.indexOf(QStringLiteral("layerContextVectorMaskMenu")) ==
        top_submenus.indexOf(QStringLiteral("layerContextMaskMenu")) + 1);
  CHECK(vector_actions ==
        (QStringList{QStringLiteral("layerVectorMaskRevealAllAction"), QStringLiteral("layerVectorMaskHideAllAction"),
                     QStringLiteral("layerVectorMaskCurrentPathAction"), QStringLiteral("-"),
                     QStringLiteral("layerVectorMaskDeleteAction"), QStringLiteral("layerVectorMaskDisableAction"),
                     QStringLiteral("layerVectorMaskRasterizeAction")}));
  CHECK(submenu_opened);
  const auto* left = find_named_layer(window, "Left");
  CHECK(left != nullptr && left->vector_mask() != nullptr && !left->vector_mask()->inverted);
  CHECK(left != nullptr && !left->mask().has_value());
  CHECK(Access::active_session_undo_depth(window) == history + 1);
}


// local-029: the Layer Mask and Vector Mask commands act on every selected layer they apply to.
// A 40x20 document: "Background" (no mask), "A" and "B" opaque with a mask white on the top half,
// "Plain" opaque without a mask, and "Locked", masked like A with its image pixels locked.
patchy::Document multi_mask_document() {
  patchy::Document document(40, 20, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(40, 20, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  const auto top_half_mask = [] {
    patchy::PixelBuffer mask(40, 20, patchy::PixelFormat::gray8());
    for (int y = 0; y < mask.height(); ++y) {
      auto row = mask.row(y);
      std::fill(row.begin(), row.end(), y < 10 ? std::uint8_t{255} : std::uint8_t{0});
    }
    return patchy::LayerMask{patchy::Rect{0, 0, 40, 20}, std::move(mask), 0, false};
  };
  document.add_pixel_layer("A", solid_pixels(40, 20, patchy::PixelFormat::rgba8(), QColor(220, 60, 30)))
      .set_mask(top_half_mask());
  document.add_pixel_layer("B", solid_pixels(40, 20, patchy::PixelFormat::rgba8(), QColor(30, 160, 60)))
      .set_mask(top_half_mask());
  document.add_pixel_layer("Plain", solid_pixels(40, 20, patchy::PixelFormat::rgba8(), QColor(40, 40, 200)));
  auto& locked =
      document.add_pixel_layer("Locked", solid_pixels(40, 20, patchy::PixelFormat::rgba8(), QColor(200, 200, 40)));
  locked.set_mask(top_half_mask());
  patchy::set_layer_lock_flags(locked, patchy::kLayerLockImagePixels);
  return document;
}

std::vector<patchy::LayerId> named_layer_ids(patchy::ui::MainWindow& window, std::initializer_list<const char*> names) {
  std::vector<patchy::LayerId> ids;
  for (const auto* name : names) {
    const auto* layer = find_named_layer(window, name);
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      ids.push_back(layer->id());
    }
  }
  return ids;
}

bool named_layer_masked(patchy::ui::MainWindow& window, const char* name) {
  const auto* layer = find_named_layer(window, name);
  return layer != nullptr && layer->mask().has_value();
}

std::uint8_t named_layer_mask_value(patchy::ui::MainWindow& window, const char* name, int x, int y) {
  const auto* layer = find_named_layer(window, name);
  CHECK(layer != nullptr && layer->mask().has_value());
  return layer != nullptr && layer->mask().has_value() ? layer->mask()->pixels.pixel(x, y)[0] : std::uint8_t{0};
}

void ui_layer_mask_commands_act_on_every_selected_layer() {
  using Access = patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  window.add_document_session(multi_mask_document(), QStringLiteral("Multi Mask"));
  show_window(window);
  QApplication::processEvents();
  // "Plain" is active and has no mask: the commands still act on the masked layers.
  const auto selection = named_layer_ids(window, {"A", "B", "Plain", "Locked"});
  const auto plain_id = find_named_layer(window, "Plain")->id();
  Access::select_layers(window, selection, plain_id);
  QApplication::processEvents();
  const auto selection_kept = [&] {
    auto selected = Access::selected_layer_ids(window);
    auto wanted = selection;
    std::sort(selected.begin(), selected.end());
    std::sort(wanted.begin(), wanted.end());
    CHECK(selected == wanted);
    CHECK(std::as_const(Access::document(window)).active_layer_id() == plain_id);
  };
  const auto locked_untouched = [&] {
    const auto* locked = find_named_layer(window, "Locked");
    CHECK(locked != nullptr && locked->mask().has_value() && !locked->mask()->disabled &&
          patchy::layer_mask_linked(*locked) && locked->mask()->pixels.pixel(5, 5)[0] == 255U);
  };
  const auto run = [&](const char* action_name) {
    auto* action = require_action(window, action_name);
    CHECK(action->isEnabled());
    const auto history = Access::active_session_undo_depth(window);
    action->trigger();
    QApplication::processEvents();
    CHECK(Access::active_session_undo_depth(window) == history + 1);
    selection_kept();
    locked_untouched();
  };
  const auto undo = [&] {
    Access::undo(window);
    QApplication::processEvents();
    Access::select_layers(window, selection, plain_id);
    QApplication::processEvents();
  };
  const auto disabled = [&](const char* name) {
    const auto* layer = find_named_layer(window, name);
    return layer != nullptr && layer->mask().has_value() && layer->mask()->disabled;
  };

  // Disable: every mask takes the toggled state; then Enable turns them all back on.
  CHECK(!require_action(window, "layerDisableMaskAction")->isChecked());
  run("layerDisableMaskAction");
  CHECK(disabled("A") && disabled("B"));
  CHECK(require_action(window, "layerDisableMaskAction")->isChecked());
  run("layerDisableMaskAction");
  CHECK(!disabled("A") && !disabled("B"));
  // Mixed states end equal (the new state of the first masked layer), not flipped each.
  Access::select_layers(window, named_layer_ids(window, {"B"}), find_named_layer(window, "B")->id());
  QApplication::processEvents();
  require_action(window, "layerDisableMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(disabled("B") && !disabled("A"));
  Access::select_layers(window, selection, plain_id);
  QApplication::processEvents();
  const bool shown_checked = require_action(window, "layerDisableMaskAction")->isChecked();
  run("layerDisableMaskAction");
  CHECK(disabled("A") == !shown_checked && disabled("B") == !shown_checked);
  undo();
  undo();
  CHECK(!disabled("A") && !disabled("B"));
  Access::select_layers(window, selection, plain_id);
  QApplication::processEvents();

  // Link: both unlink, then link again.
  CHECK(require_action(window, "layerLinkMaskAction")->isChecked());
  run("layerLinkMaskAction");
  CHECK(!patchy::layer_mask_linked(*find_named_layer(window, "A")));
  CHECK(!patchy::layer_mask_linked(*find_named_layer(window, "B")));
  run("layerLinkMaskAction");
  CHECK(patchy::layer_mask_linked(*find_named_layer(window, "A")));
  CHECK(patchy::layer_mask_linked(*find_named_layer(window, "B")));

  // Invert both masks in one step; one undo restores both.
  run("layerInvertMaskAction");
  CHECK(named_layer_mask_value(window, "A", 5, 5) == 0U && named_layer_mask_value(window, "B", 5, 5) == 0U);
  CHECK(named_layer_mask_value(window, "A", 5, 15) == 255U && named_layer_mask_value(window, "B", 5, 15) == 255U);
  undo();
  CHECK(named_layer_mask_value(window, "A", 5, 5) == 255U && named_layer_mask_value(window, "B", 5, 5) == 255U);

  // Apply bakes both masks into their layers' alpha.
  run("layerApplyMaskAction");
  CHECK(!named_layer_masked(window, "A") && !named_layer_masked(window, "B"));
  CHECK(find_named_layer(window, "A")->pixels().pixel(5, 15)[3] == 0U);
  CHECK(find_named_layer(window, "B")->pixels().pixel(5, 5)[3] == 255U);
  undo();
  CHECK(named_layer_masked(window, "A") && named_layer_masked(window, "B"));
  CHECK(find_named_layer(window, "A")->pixels().pixel(5, 15)[3] == 255U);

  // Delete removes both masks (the locked layer keeps its own); one undo brings both back.
  run("layerDeleteMaskAction");
  CHECK(!named_layer_masked(window, "A") && !named_layer_masked(window, "B") && !named_layer_masked(window, "Plain"));
  CHECK(!require_action(window, "layerDeleteMaskAction")->isEnabled());
  undo();
  CHECK(named_layer_masked(window, "A") && named_layer_masked(window, "B"));
  Access::select_layers(window, selection, plain_id);
  QApplication::processEvents();

  // Add Layer Mask with nothing selected on the canvas: only the layers without a mask take one.
  run("layerDeleteMaskAction");
  run("layerAddMaskAction");
  CHECK(named_layer_masked(window, "A") && named_layer_masked(window, "B") && named_layer_masked(window, "Plain"));
  CHECK(named_layer_mask_value(window, "A", 5, 15) == 255U && named_layer_mask_value(window, "Plain", 5, 15) == 255U);
  undo();
  CHECK(!named_layer_masked(window, "A") && !named_layer_masked(window, "B") && !named_layer_masked(window, "Plain"));
  save_widget_artifact("local029_multi_mask_after_delete", window);
}

// local-029: the Layers panel row menu's Layer Mask entries act on the selection too.
void ui_layer_row_menu_delete_mask_acts_on_every_selected_layer() {
  using Access = patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  window.add_document_session(multi_mask_document(), QStringLiteral("Row Menu Multi Mask"));
  show_window(window);
  QApplication::processEvents();
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }
  const auto selection = named_layer_ids(window, {"A", "B", "Plain", "Locked"});
  Access::select_layers(window, selection, find_named_layer(window, "A")->id());
  QApplication::processEvents();
  const auto history = Access::active_session_undo_depth(window);

  bool picked = false;
  QTimer::singleShot(0, guarded([&] {
    QMenu* row_menu = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->isVisible() && menu->objectName() == QStringLiteral("layerContextMenu")) {
        row_menu = menu;
      }
    }
    CHECK(row_menu != nullptr);
    if (row_menu == nullptr) {
      return;
    }
    QAction* mask_entry = nullptr;
    for (auto* action : row_menu->actions()) {
      if (action->menu() != nullptr && action->menu()->objectName() == QStringLiteral("layerContextMaskMenu")) {
        mask_entry = action;
      }
    }
    QAction* delete_entry = nullptr;
    if (mask_entry != nullptr) {
      for (auto* action : mask_entry->menu()->actions()) {
        if (action->objectName() == QStringLiteral("layerContextDeleteMaskAction")) {
          delete_entry = action;
        }
      }
    }
    CHECK(delete_entry != nullptr && delete_entry->isEnabled());
    if (delete_entry == nullptr) {
      row_menu->close();
      return;
    }
    row_menu->setActiveAction(mask_entry);
    QTest::keyClick(row_menu, Qt::Key_Right);
    QApplication::processEvents();
    auto* submenu = mask_entry->menu();
    if (!submenu->isVisible()) {
      row_menu->close();
      return;
    }
    submenu->setActiveAction(delete_entry);
    QTest::keyClick(submenu, Qt::Key_Return);
    picked = true;
  }));
  auto* row = layers->itemWidget(require_layer_item(*layers, QStringLiteral("B")));
  auto* name = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerRowName")) : nullptr;
  CHECK(name != nullptr);
  if (name == nullptr) {
    return;
  }
  const auto local = name->rect().center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, local, name->mapToGlobal(local));
  QApplication::sendEvent(name, &event);
  QApplication::processEvents();

  CHECK(picked);
  CHECK(!named_layer_masked(window, "A") && !named_layer_masked(window, "B"));
  CHECK(named_layer_masked(window, "Locked"));
  CHECK(Access::active_session_undo_depth(window) == history + 1);
  Access::undo(window);
  QApplication::processEvents();
  CHECK(named_layer_masked(window, "A") && named_layer_masked(window, "B"));
}

// local-029: Layer > Vector Mask commands over a selection: Reveal All adds a vector mask to each
// layer, Disable sets them all, Delete removes them all, each one undo step.
void ui_vector_mask_commands_act_on_every_selected_layer() {
  using Access = patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  window.add_document_session(multi_mask_document(), QStringLiteral("Multi Vector Mask"));
  show_window(window);
  QApplication::processEvents();
  const auto selection = named_layer_ids(window, {"A", "B", "Plain"});
  Access::select_layers(window, selection, find_named_layer(window, "Plain")->id());
  QApplication::processEvents();
  const auto vector_mask = [&](const char* name) {
    const auto* layer = find_named_layer(window, name);
    return layer != nullptr ? layer->vector_mask() : nullptr;
  };
  const auto run = [&](const char* action_name) {
    const auto history = Access::active_session_undo_depth(window);
    require_action(window, action_name)->trigger();
    QApplication::processEvents();
    CHECK(Access::active_session_undo_depth(window) == history + 1);
  };

  run("layerVectorMaskRevealAllAction");
  for (const auto* name : {"A", "B", "Plain"}) {
    CHECK(vector_mask(name) != nullptr && !vector_mask(name)->disabled);
  }
  CHECK(vector_mask("Locked") == nullptr);
  run("layerVectorMaskDisableAction");
  for (const auto* name : {"A", "B", "Plain"}) {
    CHECK(vector_mask(name) != nullptr && vector_mask(name)->disabled);
  }
  run("layerVectorMaskDeleteAction");
  for (const auto* name : {"A", "B", "Plain"}) {
    CHECK(vector_mask(name) == nullptr);
  }
  Access::undo(window);
  QApplication::processEvents();
  for (const auto* name : {"A", "B", "Plain"}) {
    CHECK(vector_mask(name) != nullptr && vector_mask(name)->disabled);
  }
  // Rasterize bakes each one; the layers that had a raster mask keep one, Plain gets one.
  Access::select_layers(window, selection, find_named_layer(window, "Plain")->id());
  QApplication::processEvents();
  run("layerVectorMaskRasterizeAction");
  for (const auto* name : {"A", "B", "Plain"}) {
    CHECK(vector_mask(name) == nullptr);
  }
}

// local-029, Seth's file: select every layer of apple-worm-layers.psd and Delete Layer Mask
// removes every mask a layer with unlocked pixels has; one undo restores them all.
void ui_layer_mask_delete_on_all_layers_of_apple_worm_if_available() {
  using Access = patchy::ui::MainWindowTestAccess;
  const auto path = patchy::test::local_psd_fixture_path("apple-worm-layers.psd");
  patchy::test::skip_unless_exists(path);
  patchy::ui::MainWindow window;
  window.add_document_session(patchy::psd::DocumentIo::read_file(path), QStringLiteral("Apple Worm"));
  show_window(window);
  QApplication::processEvents();
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }
  layers->selectAll();
  QApplication::processEvents();
  const auto selected = Access::selected_layer_ids(window);
  const auto& doc = std::as_const(Access::document(window));
  const auto masked_ids = [&] {
    std::vector<patchy::LayerId> ids;
    for (const auto id : selected) {
      const auto* layer = doc.find_layer(id);
      if (layer != nullptr && layer->mask().has_value()) {
        ids.push_back(id);
      }
    }
    return ids;
  };
  const auto before = masked_ids();
  CHECK(before.size() >= 2U);
  const auto history = Access::active_session_undo_depth(window);
  require_action(window, "layerDeleteMaskAction")->trigger();
  QApplication::processEvents();
  for (const auto id : masked_ids()) {
    // Only a layer whose pixels are locked keeps its mask.
    CHECK((patchy::layer_effective_lock_flags(doc.layers(), id) & patchy::kLayerLockImagePixels) !=
          patchy::kLayerLockNone);
  }
  CHECK(masked_ids().size() < before.size());
  CHECK(Access::active_session_undo_depth(window) == history + 1);
  Access::undo(window);
  QApplication::processEvents();
  CHECK(masked_ids() == before);
}

// A Brush or Eraser stroke on a grayscale target painted with the mask brush's row spans,
// covered-disk skip and band workers, and painted the way 1.37 did, every segment's whole
// bounding box on one thread (GitHub issue 728): the same mask bytes and bounds, and the same
// canvas, which also equals a fresh render of the result.
void ui_layer_mask_brush_fast_walk_keeps_bytes() {
  enum class Target { LayerMask, SmallLayerMask, QuickMask };
  struct Case {
    const char* name;
    Target target;
    patchy::ui::CanvasTool tool;
    int size;
    int softness;
    int opacity;
    int flow;
    bool build_up;
    int roundness;
    double zoom;
    bool feathered_selection;
    bool deep;
  };
  const auto brush = patchy::ui::CanvasTool::Brush;
  const std::vector<Case> cases{
      {"hard", Target::LayerMask, brush, 301, 0, 100, 100, false, 100, 0.5, false, false},
      {"hard_big", Target::LayerMask, brush, 501, 0, 100, 100, false, 100, 0.5, false, false},
      {"soft_8_big", Target::LayerMask, brush, 481, 8, 100, 100, false, 100, 0.5, false, false},
      {"soft_15", Target::LayerMask, brush, 281, 15, 100, 100, false, 100, 0.37, false, false},
      {"soft_100", Target::LayerMask, brush, 241, 100, 100, 100, false, 100, 0.5, false, false},
      {"opacity_60", Target::LayerMask, brush, 221, 40, 60, 100, false, 100, 1.0, false, false},
      {"eraser", Target::LayerMask, patchy::ui::CanvasTool::Eraser, 201, 30, 80, 100, false, 100, 0.5, false, false},
      {"flow_20", Target::LayerMask, brush, 181, 25, 100, 20, false, 100, 0.5, false, false},
      {"build_up", Target::LayerMask, brush, 161, 50, 70, 100, true, 100, 0.5, false, false},
      {"oval", Target::LayerMask, brush, 221, 35, 90, 100, false, 55, 0.5, false, false},
      {"feathered", Target::LayerMask, brush, 201, 25, 100, 100, false, 100, 0.5, true, false},
      {"small_mask", Target::SmallLayerMask, brush, 241, 20, 100, 100, false, 100, 0.5, false, false},
      {"quick_mask", Target::QuickMask, brush, 261, 10, 100, 100, false, 100, 0.5, false, false},
      {"deep_16", Target::LayerMask, brush, 201, 15, 100, 100, false, 100, 0.5, false, true},
      {"size_9", Target::LayerMask, brush, 9, 0, 100, 100, false, 100, 1.0, false, false},
      {"size_3", Target::LayerMask, brush, 3, 0, 100, 100, false, 100, 1.0, false, false},
  };
  patchy::set_deep_editing_override(true);
  const auto restore_deep = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  for (const auto& test_case : cases) {
    struct Result {
      std::vector<std::uint8_t> bytes;
      QRect bounds;
      QImage frame;
      std::int64_t visited{0};
      int segments{0};
      int band_redraws{0};
    };
    const auto paint = [&test_case](bool fast_walk, bool single_threaded) {
      if (single_threaded) {
        qputenv("PATCHY_RENDER_SINGLE_THREADED", "1");
      }
      const auto restore_threads = qScopeGuard([single_threaded] {
        if (single_threaded) {
          qunsetenv("PATCHY_RENDER_SINGLE_THREADED");
        }
      });
      constexpr int kWidth = 700;
      constexpr int kHeight = 520;
      patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgb8());
      document.add_pixel_layer("Background",
                               solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
      auto photo = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
      for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
          auto* px = photo.pixel(x, y);
          px[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
          px[1] = static_cast<std::uint8_t>((y * 5) % 256);
          px[2] = static_cast<std::uint8_t>(((x ^ y) * 11) % 256);
        }
      }
      auto& layer = document.add_pixel_layer("Photo", std::move(photo));
      const auto layer_id = layer.id();
      if (test_case.target != Target::QuickMask) {
        // A mask with every value in it, canvas-sized or a part the stroke grows past.
        const auto mask_bounds = test_case.target == Target::SmallLayerMask ? patchy::Rect{260, 150, 200, 170}
                                                                             : patchy::Rect{0, 0, kWidth, kHeight};
        patchy::PixelBuffer mask_pixels(mask_bounds.width, mask_bounds.height, patchy::PixelFormat::gray8());
        for (int y = 0; y < mask_bounds.height; ++y) {
          for (int x = 0; x < mask_bounds.width; ++x) {
            *mask_pixels.pixel(x, y) = static_cast<std::uint8_t>((x * 3 + y * 5) % 256);
          }
        }
        layer.set_mask(patchy::LayerMask{mask_bounds, std::move(mask_pixels), 255, false});
      }
      if (test_case.deep) {
        patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
      }
      patchy::ui::CanvasWidget canvas;
      canvas.resize(520, 420);
      canvas.set_document(&document);
      canvas.set_zoom(test_case.zoom);
      canvas.set_mask_brush_fast_walk_enabled_for_testing(fast_walk);
      canvas.show();
      QApplication::processEvents();
      const auto widget_point = [&canvas](QPoint document_point) {
        return canvas.widget_position_for_document_point(document_point);
      };
      if (test_case.feathered_selection) {
        canvas.set_tool(patchy::ui::CanvasTool::EllipticalMarquee);
        canvas.set_selection_feather_radius(25);
        drag(canvas, widget_point(QPoint(200, 90)), widget_point(QPoint(640, 470)));
        QApplication::processEvents();
        canvas.set_selection_dash_offset_for_testing(0);  // the two frames below show the same ants
      }
      if (test_case.target == Target::QuickMask) {
        canvas.set_quick_mask_active(true);
      } else {
        canvas.set_layer_edit_target(patchy::ui::CanvasWidget::LayerEditTarget::Mask);
      }
      CHECK(test_case.target == Target::QuickMask ||
            canvas.layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
      canvas.set_tool(test_case.tool);
      canvas.set_primary_color(QColor(40, 40, 40));
      canvas.set_secondary_color(QColor(255, 255, 255));
      canvas.set_brush_size(test_case.size);
      canvas.set_brush_softness(test_case.softness);
      canvas.set_brush_opacity(test_case.opacity);
      canvas.set_brush_flow(test_case.flow);
      canvas.set_brush_build_up(test_case.build_up);
      canvas.set_brush_base_shape(30.0, test_case.roundness);
      // Every fourth move folds three extra samples into it (GitHub issue 165), every seventh
      // repeats the previous point, and the path turns back on itself and leaves the canvas.
      std::vector<QPoint> path;
      for (int index = 0; index <= 90; ++index) {
        const auto t = static_cast<double>(index);
        path.emplace_back(static_cast<int>(std::lround(400.0 + 330.0 * std::sin(t * 0.05))),
                          static_cast<int>(std::lround(260.0 + 150.0 * std::sin(t * 0.11) + (index % 5) * 2)));
        if (index % 7 == 3) {
          path.push_back(path.back());
        }
      }
      std::vector<QPointF> folded;
      canvas.set_coalesced_mouse_positions_provider(
          [&folded](const QMouseEvent&) -> std::vector<QPointF> { return folded; });
      const auto target_pixels = [&]() -> const patchy::PixelBuffer& {
        if (test_case.target == Target::QuickMask) {
          return canvas.quick_mask_pixels();
        }
        return std::as_const(*document.find_layer(layer_id)).mask()->pixels;
      };
      const auto target_bytes = [&] {
        const auto bytes = target_pixels().data();
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
      };
      const auto before = target_bytes();
      const auto diagnostics_before = canvas.render_cache_diagnostics();
      // Sent events only: no event-loop pass runs between press and release, so no Airbrush
      // timer tick lands and every run paints the same segments.
      send_mouse(canvas, QEvent::MouseButtonPress, widget_point(path.front()), Qt::LeftButton, Qt::LeftButton);
      for (std::size_t index = 1; index < path.size();) {
        const auto extra = index % 4 == 0 ? std::min<std::size_t>(3, path.size() - 1 - index) : 0;
        folded.clear();
        for (std::size_t sample = 0; sample < extra; ++sample) {
          folded.emplace_back(widget_point(path[index + sample]));
        }
        const auto event_point = widget_point(path[index + extra]);
        QMouseEvent event(QEvent::MouseMove, event_point, canvas.mapToGlobal(event_point), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &event);
        index += extra + 1;
      }
      folded.clear();
      send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(path.back()), Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      Result result;
      result.bytes = target_bytes();
      if (test_case.target != Target::QuickMask) {
        const auto bounds = std::as_const(*document.find_layer(layer_id)).mask()->bounds;
        result.bounds = QRect(bounds.x, bounds.y, bounds.width, bounds.height);
      }
      const auto diagnostics = canvas.render_cache_diagnostics();
      result.segments = diagnostics.mask_brush_segments - diagnostics_before.mask_brush_segments;
      result.visited = diagnostics.mask_brush_segment_pixels - diagnostics_before.mask_brush_segment_pixels;
      result.band_redraws = diagnostics.mask_brush_band_redraws - diagnostics_before.mask_brush_band_redraws;
      CHECK(result.bytes != before);
      result.frame = canvas.grab().toImage();
      // The frame the stroke's dirty rects patched together is the frame of a whole render.
      canvas.document_changed();
      QApplication::processEvents();
      CHECK(canvas.grab().toImage() == result.frame);
      return result;
    };
    const auto reference = paint(false, true);
    const auto single = paint(true, true);
    const auto parallel = paint(true, false);
    if (single.bytes != reference.bytes || parallel.bytes != reference.bytes) {
      std::printf("[INFO] mask brush case %s: one thread %s, bands %s\n", test_case.name,
                  single.bytes == reference.bytes ? "same" : "changed",
                  parallel.bytes == reference.bytes ? "same" : "changed");
    }
    CHECK(reference.segments > 0);
    CHECK(single.segments == reference.segments);
    CHECK(parallel.segments == reference.segments);
    CHECK(single.bytes == reference.bytes);
    CHECK(parallel.bytes == reference.bytes);
    CHECK(single.bounds == reference.bounds);
    CHECK(parallel.bounds == reference.bounds);
    CHECK(single.frame == reference.frame);
    CHECK(parallel.frame == reference.frame);
    // The spans alone leave out the corners of every bounding box (a 3 px one has none).
    CHECK(test_case.size < 9 ? single.visited <= reference.visited : single.visited < reference.visited);
    CHECK(parallel.visited == single.visited);
    // The frames above also hold when moves redraw only the bands they changed, which a big hard
    // brush's moves do.
    CHECK(reference.band_redraws == 0);
    CHECK(parallel.band_redraws == single.band_redraws);
    if (std::string_view(test_case.name) == "hard_big") {
      CHECK(single.band_redraws > 0);
    }
  }

  // A dab outside a small mask grows it: every old value keeps its place, at 8 and 16 bits, and
  // the new area takes the mask's default color.
  for (const bool deep : {false, true}) {
    patchy::Document document(300, 200, patchy::PixelFormat::rgb8());
    auto& layer =
        document.add_pixel_layer("Fill", solid_pixels(300, 200, patchy::PixelFormat::rgba8(), QColor(200, 40, 40)));
    const auto layer_id = layer.id();
    const patchy::Rect old_bounds{100, 60, 80, 50};
    patchy::PixelBuffer mask_pixels(old_bounds.width, old_bounds.height, patchy::PixelFormat::gray8());
    for (int y = 0; y < old_bounds.height; ++y) {
      for (int x = 0; x < old_bounds.width; ++x) {
        *mask_pixels.pixel(x, y) = static_cast<std::uint8_t>((x * 3 + y * 5) % 256);
      }
    }
    layer.set_mask(patchy::LayerMask{old_bounds, std::move(mask_pixels), 255, false});
    if (deep) {
      patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
    }
    const auto before = std::as_const(*document.find_layer(layer_id)).mask()->pixels;
    patchy::ui::CanvasWidget canvas;
    canvas.resize(340, 240);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.show();
    QApplication::processEvents();
    canvas.set_layer_edit_target(patchy::ui::CanvasWidget::LayerEditTarget::Mask);
    canvas.set_tool(patchy::ui::CanvasTool::Brush);
    canvas.set_primary_color(Qt::black);
    canvas.set_brush_size(21);
    canvas.set_brush_softness(0);
    canvas.set_brush_opacity(100);
    const auto dab = canvas.widget_position_for_document_point(QPoint(30, 30));
    send_mouse(canvas, QEvent::MouseButtonPress, dab, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, dab, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    const auto& mask = *std::as_const(*document.find_layer(layer_id)).mask();
    const auto sample_bytes = patchy::bytes_per_pixel(mask.pixels.format());
    CHECK(mask.pixels.format() == before.format());
    CHECK(mask.bounds.x < 30 && mask.bounds.y < 30);
    CHECK(mask.bounds.x + mask.bounds.width == old_bounds.x + old_bounds.width);
    CHECK(mask.bounds.y + mask.bounds.height == old_bounds.y + old_bounds.height);
    int moved = 0;
    for (int y = 0; y < old_bounds.height; ++y) {
      for (int x = 0; x < old_bounds.width; ++x) {
        if (std::memcmp(before.pixel(x, y),
                        mask.pixels.pixel(old_bounds.x + x - mask.bounds.x, old_bounds.y + y - mask.bounds.y),
                        sample_bytes) != 0) {
          ++moved;
        }
      }
    }
    CHECK(moved == 0);
    const auto value_at = [&mask](int x, int y) {
      return patchy::ui::mask_sample_at(mask.pixels.format(), mask.pixels.pixel(x - mask.bounds.x, y - mask.bounds.y));
    };
    CHECK(value_at(30, 30) == 0.0F);    // the dab
    CHECK(value_at(60, 100) == 255.0F);  // new area the dab does not reach
  }
}

// What a mask stroke costs is set by the brush and the path, never by the mask (GitHub issue
// 728: painting on a layer mask lagged at every image size). The same stroke on a 1200 px and a
// 2400 px canvas paints the same segments over the same number of pixels and redraws the same
// area; a hard brush visits about what each move adds, a fraction of the segments' bounding
// boxes, and redraws under half of what the moves' bounding boxes hold; and no move redraws
// the whole canvas.
void ui_layer_mask_brush_stroke_work_does_not_grow_with_the_mask() {
  struct Result {
    int segments{0};
    std::int64_t visited{0};
    std::int64_t redrawn{0};
    int full_refreshes{0};
    int display_mip_rebuilds{0};
    int partial_patches{0};
  };
  constexpr int kBrushSize = 601;
  constexpr int kMoves = 60;
  const auto paint = [](int side, int softness, double zoom, bool fast_walk) {
    patchy::Document document(side, side, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background",
                             solid_pixels(side, side, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
    auto& layer =
        document.add_pixel_layer("Fill", solid_pixels(side, side, patchy::PixelFormat::rgba8(), QColor(200, 40, 40)));
    const auto layer_id = layer.id();
    patchy::PixelBuffer mask_pixels(side, side, patchy::PixelFormat::gray8());
    mask_pixels.clear(255);
    layer.set_mask(patchy::LayerMask{patchy::Rect{0, 0, side, side}, std::move(mask_pixels), 255, false});
    patchy::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.set_document(&document);
    canvas.set_zoom(zoom);
    canvas.set_mask_brush_fast_walk_enabled_for_testing(fast_walk);
    canvas.show();
    QApplication::processEvents();
    canvas.set_layer_edit_target(patchy::ui::CanvasWidget::LayerEditTarget::Mask);
    canvas.set_tool(patchy::ui::CanvasTool::Brush);
    canvas.set_primary_color(Qt::black);
    canvas.set_brush_size(kBrushSize);
    canvas.set_brush_softness(softness);
    canvas.set_brush_opacity(100);
    canvas.set_brush_flow(100);
    canvas.set_brush_build_up(false);
    // The same path about the middle of either canvas, in document pixels.
    const auto point_at = [&canvas, side](int index) {
      return canvas.widget_position_for_document_point(
          QPoint(side / 2 - 150 + index * 5, side / 2 + static_cast<int>(std::lround(20.0 * std::sin(index * 0.1)))));
    };
    canvas.repaint();
    send_mouse(canvas, QEvent::MouseButtonPress, point_at(0), Qt::LeftButton, Qt::LeftButton);
    canvas.repaint();
    const auto before = canvas.render_cache_diagnostics();
    for (int index = 1; index <= kMoves; ++index) {
      send_mouse(canvas, QEvent::MouseMove, point_at(index), Qt::NoButton, Qt::LeftButton);
      canvas.repaint();
    }
    const auto after = canvas.render_cache_diagnostics();
    send_mouse(canvas, QEvent::MouseButtonRelease, point_at(kMoves), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    // Black on the mask hides the fill where the stroke went.
    CHECK(*std::as_const(*document.find_layer(layer_id)).mask()->pixels.pixel(side / 2, side / 2) == 0);
    Result result;
    result.segments = after.mask_brush_segments - before.mask_brush_segments;
    result.visited = after.mask_brush_segment_pixels - before.mask_brush_segment_pixels;
    result.redrawn = static_cast<std::int64_t>(after.dirty_region_pixels - before.dirty_region_pixels);
    result.full_refreshes = after.full_refreshes - before.full_refreshes;
    result.display_mip_rebuilds = after.display_mip_rebuilds - before.display_mip_rebuilds;
    result.partial_patches = after.partial_patches - before.partial_patches;
    return result;
  };
  // Every segment's bounding box, what each one walked before.
  const auto box = static_cast<std::int64_t>(kBrushSize) * kBrushSize;
  for (const int softness : {0, 15}) {
    const auto small = paint(1200, softness, 1.0, true);
    const auto large = paint(2400, softness, 1.0, true);
    // The 1.37 walk: every segment's box, and the bounding boxes of what the moves changed.
    const auto boxes = paint(1200, softness, 1.0, false);
    std::printf("[INFO] mask stroke, Soft %d: %d segments over %lld pixels, %lld redrawn on 1200 px; %d over %lld, "
                "%lld redrawn on 2400 px (boxes: %lld visited, %lld redrawn)\n",
                softness, small.segments, static_cast<long long>(small.visited),
                static_cast<long long>(small.redrawn), large.segments, static_cast<long long>(large.visited),
                static_cast<long long>(large.redrawn), static_cast<long long>(boxes.visited),
                static_cast<long long>(boxes.redrawn));
    CHECK(small.segments >= kMoves);
    CHECK(large.segments == small.segments);
    CHECK(large.visited == small.visited);
    // The redraw bands sit on fixed rows of the document, so the same stroke elsewhere redraws a
    // slightly different area: within a twentieth.
    CHECK(large.redrawn * 20 <= small.redrawn * 21);
    CHECK(small.redrawn * 20 <= large.redrawn * 21);
    CHECK(boxes.segments == small.segments);
    CHECK(boxes.visited >= box * boxes.segments);
    // Hard: the strip each move adds. Soft 15%: that and the soft rim around the segment's start.
    CHECK(small.visited * (softness == 0 ? 5 : 2) < boxes.visited);
    if (softness == 0) {
      CHECK(small.redrawn * 2 <= boxes.redrawn);
    } else {
      CHECK(small.redrawn <= boxes.redrawn);
    }
    for (const auto& result : {small, large}) {
      CHECK(result.full_refreshes == 0);
      CHECK(result.partial_patches >= kMoves);
    }
  }
  // Zoomed out, the display mips are patched under each move, never rebuilt.
  const auto zoomed_out = paint(2400, 0, 0.25, true);
  CHECK(zoomed_out.segments >= kMoves);
  CHECK(zoomed_out.full_refreshes == 0);
  CHECK(zoomed_out.display_mip_rebuilds == 0);
}


}  // namespace

std::vector<patchy::test::TestCase> layer_mask_tests() {
  return {
      {"ui_layer_mask_brush_fast_walk_keeps_bytes", ui_layer_mask_brush_fast_walk_keeps_bytes},
      {"ui_layer_mask_brush_stroke_work_does_not_grow_with_the_mask",
       ui_layer_mask_brush_stroke_work_does_not_grow_with_the_mask},
      {"ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail",
       ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail},
      {"ui_layer_mask_edit_target_survives_undo", ui_layer_mask_edit_target_survives_undo},
      {"ui_layer_mask_target_paints_inverts_disables_and_applies",
       ui_layer_mask_target_paints_inverts_disables_and_applies},
      {"ui_group_layer_mask_add_paint_and_delete", ui_group_layer_mask_add_paint_and_delete},
      {"ui_apply_layer_mask_bakes_rendered_raster_coverage_at_depth", ui_apply_layer_mask_bakes_rendered_raster_coverage_at_depth},
      {"ui_mask_paint_updates_distant_drop_shadow", ui_mask_paint_updates_distant_drop_shadow},
      {"ui_layer_mask_add_without_selection_targets_mask_and_chip_exits",
       ui_layer_mask_add_without_selection_targets_mask_and_chip_exits},
      {"ui_eon_spider_canvas_matches_compositor_if_available",
       ui_eon_spider_canvas_matches_compositor_if_available},
      {"ui_layer_mask_link_button_toggles_from_mouse_clicks",
       ui_layer_mask_link_button_toggles_from_mouse_clicks},
      {"ui_layer_mask_overlay_and_view_modes", ui_layer_mask_overlay_and_view_modes},
      {"ui_layer_thumbnail_updates_after_brush_edit", ui_layer_thumbnail_updates_after_brush_edit},
      {"ui_layer_thumbnail_defers_brush_refresh_until_stroke_end",
       ui_layer_thumbnail_defers_brush_refresh_until_stroke_end},
      {"ui_layer_thumbnail_ctrl_click_loads_and_combines_selection",
       ui_layer_thumbnail_ctrl_click_loads_and_combines_selection},
      {"ui_layer_thumbnail_right_click_opens_pixels_menu", ui_layer_thumbnail_right_click_opens_pixels_menu},
      {"ui_layer_mask_thumbnail_right_click_opens_mask_menu", ui_layer_mask_thumbnail_right_click_opens_mask_menu},
      {"ui_layer_footer_delete_removes_targeted_mask", ui_layer_footer_delete_removes_targeted_mask},
      {"ui_layer_row_menu_vector_mask_submenu_adds_vector_mask",
       ui_layer_row_menu_vector_mask_submenu_adds_vector_mask},
      {"ui_layer_mask_commands_act_on_every_selected_layer", ui_layer_mask_commands_act_on_every_selected_layer},
      {"ui_layer_row_menu_delete_mask_acts_on_every_selected_layer",
       ui_layer_row_menu_delete_mask_acts_on_every_selected_layer},
      {"ui_vector_mask_commands_act_on_every_selected_layer", ui_vector_mask_commands_act_on_every_selected_layer},
      {"ui_layer_mask_delete_on_all_layers_of_apple_worm_if_available",
       ui_layer_mask_delete_on_all_layers_of_apple_worm_if_available},
  };
}
