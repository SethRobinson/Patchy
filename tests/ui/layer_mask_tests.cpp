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
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] eon_spider fixture missing: " << path.string() << '\n';
    return;
  }
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
  CHECK(after.red() > before.red() + 80);
  CHECK(after.green() < 90);
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
  CHECK(after.red() > before.red() + 80);
  CHECK(after.green() < 90);
  CHECK(after.blue() < 90);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_mask_tests() {
  return {
      {"ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail",
       ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail},
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
  };
}
