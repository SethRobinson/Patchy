#include "ui/canvas_widget.hpp"
#include "core/vector_shape.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
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

// Right-clicks `widget_name` in `row_name`'s row and picks `pick` (an action objectName, or empty
// to close); returns the menu that opened. `artifact` saves the open menu.
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
      for (auto* action : menu->actions()) {
        seen.actions.push_back(action->isSeparator() ? QStringLiteral("-") : action->objectName());
        seen.texts.push_back(action->text());
        if (!pick.isEmpty() && action->objectName() == pick) {
          chosen = action;
        }
      }
      if (!artifact.empty()) {
        save_widget_artifact(artifact, *menu);
      }
      if (chosen != nullptr) {
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
  CHECK(seen.texts.value(5) == QStringLiteral("Thumbnail Size"));
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
                     QStringLiteral("layerMaskThumbnailIntersectAction")}));
  CHECK(seen.texts.value(0) == QStringLiteral("Disable Layer Mask"));
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
  QTimer::singleShot(0, [&seen, button] {
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
  });
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
  QTimer::singleShot(0, [&] {
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
  });
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


}  // namespace

std::vector<patchy::test::TestCase> layer_mask_tests() {
  return {
      {"ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail",
       ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail},
      {"ui_layer_mask_edit_target_survives_undo", ui_layer_mask_edit_target_survives_undo},
      {"ui_layer_mask_target_paints_inverts_disables_and_applies",
       ui_layer_mask_target_paints_inverts_disables_and_applies},
      {"ui_group_layer_mask_add_paint_and_delete", ui_group_layer_mask_add_paint_and_delete},
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
  };
}
