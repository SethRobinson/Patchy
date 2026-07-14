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

void ui_copy_paste_and_transform_pasted_layer_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(255, 80, 20));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(180, 140));
  const auto copied_selection_rect = canvas->selected_document_rect();
  CHECK(copied_selection_rect.has_value());
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == copied_selection_rect->topLeft());
  CHECK(pasted_rect->size() == copied_selection_rect->size());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  drag(*canvas, QPoint(120, 100), QPoint(150, 130));
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  auto before_transform = canvas->active_layer_document_rect();
  CHECK(before_transform.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto bottom_right =
      canvas->widget_position_for_document_point(QPoint(before_transform->x() + before_transform->width(),
                                                       before_transform->y() + before_transform->height()));
  drag(*canvas, bottom_right, bottom_right + QPoint(180, 40), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  auto after_transform = canvas->active_layer_document_rect();
  CHECK(after_transform.has_value());
  CHECK(after_transform->width() > before_transform->width() + 50);
  const auto original_ratio = static_cast<double>(before_transform->width()) / before_transform->height();
  const auto transformed_ratio = static_cast<double>(after_transform->width()) / after_transform->height();
  CHECK(std::abs(original_ratio - transformed_ratio) < 0.2);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto top_center = canvas->widget_position_for_document_point(
      QPoint(after_transform->x() + after_transform->width() / 2, after_transform->y()));
  drag(*canvas, top_center + QPoint(0, -32), top_center + QPoint(80, 20));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  auto after_rotate = canvas->active_layer_document_rect();
  CHECK(after_rotate.has_value());
  CHECK(after_rotate->width() >= after_transform->width());
  save_widget_artifact("ui_copy_paste_transform", window);
}

void ui_external_clipboard_image_paste_creates_centered_layer() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  QImage image(18, 12, QImage::Format_RGBA8888);
  image.fill(QColor(20, 180, 80, 255));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint((1024 - image.width()) / 2, (768 - image.height()) / 2));
  CHECK(pasted_rect->size() == image.size());
  CHECK(color_close(canvas_pixel(*canvas, pasted_rect->center()), QColor(20, 180, 80), 35));
  QApplication::clipboard()->clear();
}

void ui_external_clipboard_image_paste_overrides_internal_payload() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  QImage image(20, 14, QImage::Format_RGBA8888);
  image.fill(QColor(220, 40, 140, 255));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint((1024 - image.width()) / 2, (768 - image.height()) / 2));
  CHECK(pasted_rect->size() == image.size());
  CHECK(color_close(canvas_pixel(*canvas, pasted_rect->center()), QColor(220, 40, 140), 35));
  QApplication::clipboard()->clear();
}

void ui_free_transform_uses_opaque_pixel_bounds() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(140, 115)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(230, 60, 35));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto full_layer_rect = canvas->active_layer_document_rect();
  CHECK(full_layer_rect.has_value());
  CHECK(full_layer_rect->width() > 900);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto handle = canvas->widget_position_for_document_point(filled_rect->bottomRight() + QPoint(1, 1));
  const auto expanded = canvas->widget_position_for_document_point(filled_rect->bottomRight() + QPoint(75, 55));
  drag(*canvas, handle, expanded, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto transformed_rect = canvas->active_layer_document_rect();
  CHECK(transformed_rect.has_value());
  CHECK(transformed_rect->width() > filled_rect->width() + 20);
  CHECK(transformed_rect->height() > filled_rect->height() + 15);
  CHECK(transformed_rect->width() < 180);
  CHECK(transformed_rect->height() < 140);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(transformed_rect->center()),
       canvas->widget_position_for_document_point(transformed_rect->center() + QPoint(40, 24)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == transformed_rect);
  save_widget_artifact("ui_transform_opaque_bounds", window);
}

// Arrow keys during a Free Transform (Ctrl+T) must nudge the bounding box (and the
// previewed pixels) together. The box used to stay put while the keys fell through to a
// destructive layer move — regression for the Ctrl+T pixel-nudge desync.
void ui_free_transform_arrow_keys_nudge_bounding_box() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(140, 115)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(60, 200, 120));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto before = canvas->transform_controls_state();
  CHECK(before.has_value());
  CHECK(before->active);

  // 3px right + 2px down: the box reference position must follow the nudge exactly.
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down);
  send_key(*canvas, Qt::Key_Down);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto after = canvas->transform_controls_state();
  CHECK(after.has_value());
  CHECK(std::abs((after->reference_position.x() - before->reference_position.x()) - 3.0) < 0.001);
  CHECK(std::abs((after->reference_position.y() - before->reference_position.y()) - 2.0) < 0.001);

  // Shift makes each step 10px.
  send_key(*canvas, Qt::Key_Left, Qt::ShiftModifier);
  QApplication::processEvents();
  const auto after_shift = canvas->transform_controls_state();
  CHECK(after_shift.has_value());
  CHECK(std::abs((after_shift->reference_position.x() - after->reference_position.x()) + 10.0) < 0.001);
  CHECK(std::abs(after_shift->reference_position.y() - after->reference_position.y()) < 0.001);

  // Net nudge is (-7, +2). Committing applies it as a translation of the pending
  // transform: the layer moves with the box and keeps its size (no destructive resize).
  const auto box_center_before_commit = after_shift->reference_position;
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto committed_rect = canvas->active_layer_document_rect();
  CHECK(committed_rect.has_value());
  CHECK(committed_rect->width() == filled_rect->width());
  CHECK(committed_rect->height() == filled_rect->height());
  CHECK(std::abs(QRectF(*committed_rect).center().x() - box_center_before_commit.x()) <= 1.0);
  CHECK(std::abs(QRectF(*committed_rect).center().y() - box_center_before_commit.y()) <= 1.0);

  save_widget_artifact("ui_free_transform_arrow_nudge", window);
}

void ui_transform_numeric_controls_apply_values() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(150, 125)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(40, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  auto* x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  auto* y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformYSpin"));
  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* scale_y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  auto* interpolation = window.findChild<QComboBox*>(QStringLiteral("freeTransformInterpolationCombo"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(x != nullptr);
  CHECK(y != nullptr);
  CHECK(scale_x != nullptr);
  CHECK(scale_y != nullptr);
  CHECK(rotation != nullptr);
  CHECK(interpolation != nullptr);
  CHECK(apply != nullptr);
  // The passive Move box no longer shows the numeric controls (Photoshop): they
  // appear the moment a session is actually active.
  CHECK(!x->isVisible());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(x->isVisible());
  CHECK(interpolation->currentText() == QStringLiteral("Bicubic"));

  x->setValue(x->value() + 32.0);
  y->setValue(y->value() + 18.0);
  scale_x->setValue(150.0);
  scale_y->setValue(125.0);
  rotation->setValue(15.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  apply->click();
  QApplication::processEvents();

  const auto transformed_rect = canvas->active_layer_document_rect();
  CHECK(transformed_rect.has_value());
  CHECK(transformed_rect->width() > filled_rect->width());
  CHECK(transformed_rect->height() > filled_rect->height());
  CHECK(transformed_rect->center().x() > filled_rect->center().x() + 15);
  CHECK(!canvas->free_transform_active());
}

void ui_free_transform_preview_follows_live_layer_style_changes() {
  patchy::Document document(220, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Styled Transform",
                      solid_pixels(60, 40, patchy::PixelFormat::rgba8(), QColor(40, 130, 230, 255)));
  layer.set_bounds(patchy::Rect{60, 50, 60, 40});
  const auto styled_id = layer.id();
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Transform Style Preview"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Styled Transform")));
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  // Drag a handle so the transform preview snapshot gets baked.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 90)),
       canvas->widget_position_for_document_point(QPoint(132, 98)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(90, 70)), QColor(40, 130, 230), 35));

  // Simulate the Layer Style dialog's live preview while the transform is
  // still active: mutate the style and announce it through the async path.
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  auto* styled = doc.find_layer(styled_id);
  CHECK(styled != nullptr);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{210, 20, 20};
  overlay.opacity = 1.0F;
  styled->layer_style().color_overlays.push_back(overlay);
  canvas->document_changed_async_preview();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(90, 70)), QColor(210, 20, 20), 35));

  // A follow-up tweak to the same effect must also show through immediately.
  styled = doc.find_layer(styled_id);
  CHECK(styled != nullptr);
  CHECK(!styled->layer_style().color_overlays.empty());
  styled->layer_style().color_overlays.front().color = patchy::RgbColor{30, 170, 60};
  canvas->document_changed_async_preview();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(90, 70)), QColor(30, 170, 60), 35));
  save_widget_artifact("ui_transform_live_style_preview", window);

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

void ui_options_bar_overflow_button_reveals_hidden_controls() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(150, 125)));
  canvas->set_primary_color(QColor(40, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  // The numeric controls only show during an active session now.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(apply != nullptr);
  auto* xspin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  CHECK(xspin != nullptr);

  // Wide enough to lay every transform control out on a single row.
  window.resize(1500, 800);
  QApplication::processEvents();
  process_events_for(60);
  CHECK(xspin->isVisible());
  CHECK(apply->isVisible());
  const int single_row_height = options_bar->height();

  // Too narrow for one row: the controls must fold onto additional rows and the
  // toolbar must grow taller, keeping every control (including Apply) visible.
  window.resize(720, 800);
  QApplication::processEvents();
  process_events_for(60);
  CHECK(apply->isVisible());
  CHECK(xspin->isVisible());
  const int wrapped_height = options_bar->height();
  CHECK(wrapped_height > single_row_height);

  save_widget_artifact("ui_options_bar_overflow", window);
}

void ui_transform_numeric_controls_accept_negative_scale() {
  patchy::Document document(260, 180, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(260, 180, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(80, 40, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 80, 20), QColor(220, 40, 40, 255));
  fill_pixel_rect(pixels, QRect(0, 20, 80, 20), QColor(40, 80, 220, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Flip Scale", std::move(pixels));
  layer.set_bounds(patchy::Rect{80, 70, 80, 40});
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Negative Transform Scale"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* scale_y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  auto* link = window.findChild<QPushButton*>(QStringLiteral("freeTransformLinkScaleButton"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(scale_x != nullptr);
  CHECK(scale_y != nullptr);
  CHECK(link != nullptr);
  CHECK(apply != nullptr);
  CHECK(scale_x->minimum() <= -100.0);
  CHECK(scale_y->minimum() <= -100.0);
  link->setChecked(false);

  scale_y->setValue(-100.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  CHECK(std::abs(state->scale_y_percent + 100.0) < 0.001);

  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 76)), QColor(40, 80, 220), 50));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 104)), QColor(220, 40, 40), 50));
  save_widget_artifact("ui_transform_negative_scale", window);
}

void ui_transform_numeric_preview_renders_layer_styles() {
  patchy::Document document(360, 260, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(360, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(70, 46, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(54, 8, 14, 30), QColor(35, 85, 210, 255));
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Transform", std::move(pixels));
  styled_layer.set_bounds(patchy::Rect{120, 90, 70, 46});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{230, 35, 45};
  stroke.opacity = 1.0F;
  stroke.size = 8.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  styled_layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(styled_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Styled Transform Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("freeTransformCancelButton"));
  CHECK(scale_x != nullptr);
  CHECK(rotation != nullptr);
  CHECK(cancel != nullptr);

  scale_x->setValue(240.0);
  rotation->setValue(0.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto preview = canvas->grab().toImage();
  const auto count_red_style_pixels = [&](QRect document_rect) {
    int count = 0;
    for (int document_y = document_rect.top(); document_y <= document_rect.bottom(); ++document_y) {
      for (int document_x = document_rect.left(); document_x <= document_rect.right(); ++document_x) {
        const auto widget_point = canvas->widget_position_for_document_point(QPoint(document_x, document_y));
        if (!preview.rect().contains(widget_point)) {
          continue;
        }
        const auto color = preview.pixelColor(widget_point);
        if (color.red() > 190 && color.green() < 90 && color.blue() < 100) {
          ++count;
        }
      }
    }
    return count;
  };
  CHECK(count_red_style_pixels(QRect(154, 88, 14, 54)) > 40);
  CHECK(count_red_style_pixels(QRect(214, 86, 24, 36)) < 10);
  cancel->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

void ui_move_show_transform_controls_click_shows_passive_transform() {
  SettingsValueRestorer saved_show_transform_controls(QStringLiteral("tools/showTransformControls"));
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/showTransformControls"));
  settings.sync();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* show_controls = window.findChild<QCheckBox*>(QStringLiteral("moveShowTransformControlsCheck"));
  CHECK(show_controls != nullptr);
  CHECK(show_controls->text() == QStringLiteral("Show Transform Controls"));
  CHECK(show_controls->isChecked());
  CHECK(canvas->show_transform_controls());

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(140, 115)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(60, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  const auto click = canvas->widget_position_for_document_point(QPoint(100, 90));
  const auto passive_bottom_right = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width(), filled_rect->y() + filled_rect->height()));
  send_mouse(*canvas, QEvent::MouseMove, passive_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_mouse(*canvas, QEvent::MouseMove, click, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);

  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);
  send_mouse(*canvas, QEvent::MouseMove, passive_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_mouse(*canvas, QEvent::MouseMove, click, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  save_widget_artifact("ui_move_show_transform_controls", window);

  const auto jitter = click + QPoint(2, -3);
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, jitter, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, jitter, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);

  const auto outside = canvas->widget_position_for_document_point(QPoint(420, 340));
  send_mouse(*canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);

  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);

  canvas->set_auto_select_layer(false);
  const auto auto_select_off_before = canvas->active_layer_document_rect();
  CHECK(auto_select_off_before.has_value());
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == auto_select_off_before);

  send_mouse(*canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == auto_select_off_before);
  send_mouse(*canvas, QEvent::MouseMove, passive_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  const auto auto_select_off_jitter = click + QPoint(-2, 3);
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, auto_select_off_jitter, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, auto_select_off_jitter, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == auto_select_off_before);

  const auto bottom_right = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width(), filled_rect->y() + filled_rect->height()));
  send_mouse(*canvas, QEvent::MouseButtonPress, bottom_right, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_mouse(*canvas, QEvent::MouseMove, bottom_right + QPoint(70, 45), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, bottom_right + QPoint(70, 45), Qt::LeftButton, Qt::NoButton,
             Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto auto_select_off_after = canvas->active_layer_document_rect();
  CHECK(auto_select_off_after.has_value());
  CHECK(auto_select_off_after->width() > filled_rect->width() + 20);
  CHECK(auto_select_off_after->height() > filled_rect->height() + 10);

  const auto transformed_bottom_right = canvas->widget_position_for_document_point(
      QPoint(auto_select_off_after->x() + auto_select_off_after->width(),
             auto_select_off_after->y() + auto_select_off_after->height()));
  const auto transformed_rotate_handle = canvas->widget_position_for_document_point(
                                             QPoint(auto_select_off_after->x() + auto_select_off_after->width() / 2,
                                                    auto_select_off_after->y())) +
                                         QPoint(0, -32);
  const auto transformed_center = canvas->widget_position_for_document_point(auto_select_off_after->center());
  send_mouse(*canvas, QEvent::MouseMove, transformed_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_mouse(*canvas, QEvent::MouseButtonPress, transformed_center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, transformed_center + QPoint(50, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto moving_preview = canvas->grab().toImage();
  CHECK(color_close(moving_preview.pixelColor(transformed_rotate_handle), Qt::white, 18));
  send_mouse(*canvas, QEvent::MouseButtonRelease, transformed_center + QPoint(50, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_transform_controls_finish_on_tool_layer_and_duplicate_changes() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  QImage image(48, 32, QImage::Format_RGBA8888);
  image.fill(QColor(40, 180, 120, 255));
  QApplication::clipboard()->setImage(image);
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  const auto before_tool_switch = canvas->active_layer_document_rect();
  CHECK(before_tool_switch.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before_tool_switch);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(before_tool_switch->center()),
       canvas->widget_position_for_document_point(before_tool_switch->center() + QPoint(34, 0)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto after_tool_switch = canvas->active_layer_document_rect();
  CHECK(after_tool_switch.has_value());
  CHECK(after_tool_switch->x() >= before_tool_switch->x() + 28);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  const auto before_duplicate = canvas->active_layer_document_rect();
  CHECK(before_duplicate.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(before_duplicate->center()),
       canvas->widget_position_for_document_point(before_duplicate->center() + QPoint(24, 14)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto layers_before_duplicate = layer_list->count();
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(layer_list->count() == layers_before_duplicate + 1);
  CHECK(layer_list->selectedItems().size() == 1);
  const auto duplicated = canvas->active_layer_document_rect();
  CHECK(duplicated.has_value());
  CHECK(duplicated->x() >= before_duplicate->x() + 18);
  CHECK(duplicated->y() >= before_duplicate->y() + 8);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(layer_list->count() >= 2);
  layer_list->setCurrentItem(layer_list->item(1), QItemSelectionModel::ClearAndSelect);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(layer_list->selectedItems().size() == 1);
  QApplication::clipboard()->clear();
}

void ui_layer_via_copy_and_cut_match_photoshop_shortcuts() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(245, 30, 30));
  canvas->set_brush_size(24);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 80)),
       canvas->widget_position_for_document_point(QPoint(180, 80)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(52, 58)),
       canvas->widget_position_for_document_point(QPoint(150, 112)));

  const auto initial_layers = layer_list->count();
  require_action(window, "layerViaCopyAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == initial_layers + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer Via Copy"));
  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  layer_list->clearSelection();
  auto* paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->setCurrentItem(paint_layer);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerViaCutAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer Via Cut"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  const auto revealed = canvas_pixel(*canvas, QPoint(110, 80));
  CHECK(!color_close(revealed, QColor(245, 30, 30), 45));
  CHECK(revealed.red() > 210 && revealed.green() > 210 && revealed.blue() > 210);
  save_widget_artifact("ui_layer_via_copy_cut", window);
}

}  // namespace

std::vector<patchy::test::TestCase> clipboard_free_transform_tests() {
  return {
      {"ui_copy_paste_and_transform_pasted_layer_work", ui_copy_paste_and_transform_pasted_layer_work},
      {"ui_external_clipboard_image_paste_creates_centered_layer",
       ui_external_clipboard_image_paste_creates_centered_layer},
      {"ui_external_clipboard_image_paste_overrides_internal_payload",
       ui_external_clipboard_image_paste_overrides_internal_payload},
      {"ui_free_transform_uses_opaque_pixel_bounds", ui_free_transform_uses_opaque_pixel_bounds},
      {"ui_free_transform_arrow_keys_nudge_bounding_box", ui_free_transform_arrow_keys_nudge_bounding_box},
      {"ui_transform_numeric_controls_apply_values", ui_transform_numeric_controls_apply_values},
      {"ui_free_transform_preview_follows_live_layer_style_changes",
       ui_free_transform_preview_follows_live_layer_style_changes},
      {"ui_options_bar_overflow_button_reveals_hidden_controls",
       ui_options_bar_overflow_button_reveals_hidden_controls},
      {"ui_transform_numeric_controls_accept_negative_scale",
       ui_transform_numeric_controls_accept_negative_scale},
      {"ui_transform_numeric_preview_renders_layer_styles",
       ui_transform_numeric_preview_renders_layer_styles},
      {"ui_move_show_transform_controls_click_shows_passive_transform",
       ui_move_show_transform_controls_click_shows_passive_transform},
      {"ui_transform_controls_finish_on_tool_layer_and_duplicate_changes",
       ui_transform_controls_finish_on_tool_layer_and_duplicate_changes},
      {"ui_layer_via_copy_and_cut_match_photoshop_shortcuts",
       ui_layer_via_copy_and_cut_match_photoshop_shortcuts},
  };
}
