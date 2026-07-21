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

void ui_transformed_text_reedit_preserves_transform() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint box_top_left(120, 120);
  const QPoint box_bottom_right(300, 180);
  drag(*canvas, canvas->widget_position_for_document_point(box_top_left),
       canvas->widget_position_for_document_point(box_bottom_right));
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Rotate me"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto before_transform = canvas->active_layer_document_rect();
  CHECK(before_transform.has_value());
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto before_visible_text = dark_document_bounds(*canvas, before_transform->adjusted(-8, -8, 8, 8));
  CHECK(before_visible_text.has_value());
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  const auto move_text_handle =
      canvas->widget_position_for_document_point(QPoint(before_transform->right() + 1, before_transform->bottom() + 1));
  send_mouse(*canvas, QEvent::MouseMove, move_text_handle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto top_center = canvas->widget_position_for_document_point(
      QPoint(before_visible_text->center().x(), before_visible_text->top()));
  drag(*canvas, top_center + QPoint(0, -32), top_center + QPoint(70, 26));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  auto transformed = canvas->active_layer_document_rect();
  CHECK(transformed.has_value());
  CHECK(transformed->height() > before_visible_text->height() + 35);
  const auto original_text_area = static_cast<qint64>(before_visible_text->width()) * before_visible_text->height();
  for (const auto rotation : {12.0, -12.0, 8.0}) {
    require_action(window, "editFreeTransformAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->free_transform_active());
    const auto state = canvas->transform_controls_state();
    CHECK(state.has_value());
    CHECK(canvas->set_transform_controls_state(state->reference_position, 100.0, 100.0, rotation));
    QApplication::processEvents();
    send_key(*canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(!canvas->free_transform_active());
  }
  transformed = canvas->active_layer_document_rect();
  CHECK(transformed.has_value());
  const auto repeated_transform_area = static_cast<qint64>(transformed->width()) * transformed->height();
  CHECK(repeated_transform_area < original_text_area * 8);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto edit_point = canvas->widget_position_for_document_point(transformed->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, edit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, edit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"),
                                              Qt::FindDirectChildrenOnly);
  CHECK(overlay != nullptr);
  CHECK(overlay->isVisible());
  CHECK(!overlay->geometry().isEmpty());
  CHECK(overlay->geometry() != editor->geometry());
  const auto overlay_controls_image = canvas->grab().toImage();
  CHECK(count_pixels_close(overlay_controls_image, overlay->geometry().adjusted(-2, -2, 2, 2),
                           QColor(245, 248, 252), 18) > 8);
  const auto drag_transformed_resize_handle = [&](int handle_index, QPoint delta) {
    const auto resize_handle_centers = overlay->property("patchy.transformedTextResizeHandleCenters").toList();
    CHECK(resize_handle_centers.size() == 4);
    const auto resize_handle_point = resize_handle_centers[handle_index].toPointF().toPoint();
    const auto text_width_before_resize = editor->property("patchy.documentTextWidth").toInt();
    const auto text_height_before_resize = editor->property("patchy.documentTextHeight").toInt();
    send_mouse(*canvas, QEvent::MouseButtonPress, resize_handle_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, resize_handle_point + delta, Qt::NoButton, Qt::LeftButton);
    const auto live_resize_handle_centers = overlay->property("patchy.transformedTextResizeHandleCenters").toList();
    CHECK(live_resize_handle_centers.size() == 4);
    CHECK((live_resize_handle_centers[handle_index].toPointF() - resize_handle_centers[handle_index].toPointF())
              .manhattanLength() > 4.0);
    send_mouse(*canvas, QEvent::MouseButtonRelease, resize_handle_point + delta, Qt::LeftButton, Qt::NoButton);
    process_events_for(80);
    editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
    overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"),
                                          Qt::FindDirectChildrenOnly);
    CHECK(overlay != nullptr);
    CHECK(overlay->isVisible());
    const auto text_width_after_resize = editor->property("patchy.documentTextWidth").toInt();
    const auto text_height_after_resize = editor->property("patchy.documentTextHeight").toInt();
    CHECK(text_width_after_resize != text_width_before_resize ||
          text_height_after_resize != text_height_before_resize);
  };
  drag_transformed_resize_handle(0, QPoint(-36, -24));
  drag_transformed_resize_handle(2, QPoint(-32, 28));
  drag_transformed_resize_handle(3, QPoint(48, 32));

  QTextCursor selection_cursor(editor->document());
  selection_cursor.setPosition(0);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  const auto unselected_image = canvas->grab().toImage();
  selection_cursor.setPosition(0);
  selection_cursor.setPosition(6, QTextCursor::KeepAnchor);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  const auto selected_image = canvas->grab().toImage();
  int changed_pixels_outside_editor = 0;
  const auto editor_hit_rect = editor->geometry().adjusted(-2, -2, 2, 2);
  const auto overlay_sample_rect = overlay->geometry().intersected(selected_image.rect());
  for (int y = overlay_sample_rect.top(); y <= overlay_sample_rect.bottom(); y += 2) {
    for (int x = overlay_sample_rect.left(); x <= overlay_sample_rect.right(); x += 2) {
      if (editor_hit_rect.contains(QPoint(x, y)) || !unselected_image.rect().contains(QPoint(x, y))) {
        continue;
      }
      const auto before = unselected_image.pixelColor(x, y);
      const auto after = selected_image.pixelColor(x, y);
      const auto delta = std::abs(before.red() - after.red()) + std::abs(before.green() - after.green()) +
                         std::abs(before.blue() - after.blue());
      if (delta > 12) {
        ++changed_pixels_outside_editor;
      }
    }
  }
  CHECK(changed_pixels_outside_editor > 8);

  const auto editor_polygon_points = overlay->property("patchy.transformedTextEditorPolygon").toList();
  CHECK(editor_polygon_points.size() == 4);
  const auto top_left = editor_polygon_points[0].toPointF();
  const auto top_right = editor_polygon_points[1].toPointF();
  const auto bottom_right = editor_polygon_points[2].toPointF();
  const auto bottom_left = editor_polygon_points[3].toPointF();
  const auto polygon_center = (top_left + top_right + bottom_right + bottom_left) / 4.0;
  const auto left_mid = (top_left + bottom_left) / 2.0;
  const auto right_mid = (top_right + bottom_right) / 2.0;
  const auto overlay_drag_start = (polygon_center * 0.65 + left_mid * 0.35).toPoint();
  const auto overlay_drag_end = (polygon_center * 0.65 + right_mid * 0.35).toPoint();
  send_mouse(*canvas, QEvent::MouseButtonPress, overlay_drag_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, overlay_drag_end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, overlay_drag_end, Qt::LeftButton, Qt::NoButton);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->textCursor().hasSelection());

  editor->setPlainText(QStringLiteral("Rotate me again"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto after_reedit = canvas->active_layer_document_rect();
  CHECK(after_reedit.has_value());
  CHECK(after_reedit->height() > before_visible_text->height() + 35);
  CHECK(after_reedit->height() >= transformed->height() - 16);
  save_widget_artifact("ui_transformed_text_reedit", window);
}

void ui_point_text_transform_scales_crisply() {
  // #1: scaling a point-text layer with the free transform must re-rasterize the glyphs at the new
  // size (crisp), not resample the small baked bitmap (blocky).  We scale 4x and verify the glyph
  // edges stay thin -- a bilinear upscale would smear each vertical edge into a ~4px alpha gradient.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto create_widget = canvas->widget_position_for_document_point(QPoint(120, 140));
  send_mouse(*canvas, QEvent::MouseButtonPress, create_widget, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, create_widget, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("HI"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();

  const auto layer_id = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
  CHECK(layer_id.has_value());
  const auto base_bounds = canvas->active_layer_document_rect();
  CHECK(base_bounds.has_value());
  const auto read_text_size = [&]() {
    const auto* l = patchy::ui::MainWindowTestAccess::document(window).find_layer(*layer_id);
    const auto it = l->metadata().find(patchy::kLayerMetadataTextSize);
    return it == l->metadata().end() ? 0 : std::atoi(it->second.c_str());
  };
  const int base_size = read_text_size();
  CHECK(base_size > 0);

  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  auto* scale_x_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* scale_y_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  CHECK(scale_x_spin != nullptr);
  CHECK(scale_y_spin != nullptr);
  scale_x_spin->setValue(400.0);
  scale_y_spin->setValue(400.0);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // Widest run of partial-alpha (anti-aliased edge) pixels on the densest glyph row.  Crisp vector
  // rasterization keeps this to ~1-2px; a 4x bitmap upscale spreads each edge into a ~4px gradient.
  const auto max_edge_ramp = [](const QImage& img) {
    int best_row = -1;
    int best_opaque = -1;
    for (int y = 0; y < img.height(); ++y) {
      int opaque = 0;
      for (int x = 0; x < img.width(); ++x) {
        if (qAlpha(img.pixel(x, y)) >= 235) {
          ++opaque;
        }
      }
      if (opaque > best_opaque) {
        best_opaque = opaque;
        best_row = y;
      }
    }
    int max_ramp = 0;
    int current_ramp = 0;
    if (best_row >= 0) {
      for (int x = 0; x < img.width(); ++x) {
        const auto alpha = qAlpha(img.pixel(x, best_row));
        if (alpha > 20 && alpha < 235) {
          ++current_ramp;
          max_ramp = std::max(max_ramp, current_ramp);
        } else {
          current_ramp = 0;
        }
      }
    }
    return std::pair<int, int>{best_opaque, max_ramp};
  };

  const auto* layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->pixels().format().channels >= 4);
  const auto image = image_from_pixels_for_visuals(layer->pixels());
  // The glyph raster must have grown substantially (proves a real scale happened, not a no-op).  Bounds
  // now hug the inked glyphs, so the height is cap-height, not the line box -- the size-fold check below
  // is the precise proof of the 4x; this is a sanity floor.
  CHECK(image.height() > base_bounds->height() * 2);
  CHECK(image.width() > 120);
  // #2/#3: the 4x scale must be folded into the point size (Photoshop-style), so the stored font size
  // grows ~4x rather than staying at the base size with a 4x matrix.
  const int folded_size = read_text_size();
  CHECK(folded_size >= base_size * 3);
  CHECK(folded_size <= base_size * 5);
  ensure_artifact_dir();
  CHECK(flattened_on_white(layer->pixels())
            .save(QStringLiteral("test-artifacts/ui_point_text_transform_scales_crisply.png")));
  const auto [opaque_after_scale, ramp_after_scale] = max_edge_ramp(image);
  CHECK(opaque_after_scale > 0);
  CHECK(ramp_after_scale <= 3);

  // Re-edit the scaled text: the edit-commit path must also re-rasterize through the transform, so the
  // text stays crisp after editing rather than resampling the base-size bitmap again.
  const auto scaled_rect = canvas->active_layer_document_rect();
  CHECK(scaled_rect.has_value());
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto edit_widget = canvas->widget_position_for_document_point(scaled_rect->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, edit_widget, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, edit_widget, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("NH"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();

  const auto* edited = patchy::ui::MainWindowTestAccess::document(window).find_layer(*layer_id);
  CHECK(edited != nullptr);
  const auto edited_image = image_from_pixels_for_visuals(edited->pixels());
  CHECK(edited_image.height() > base_bounds->height() * 2);
  const auto [opaque_after_edit, ramp_after_edit] = max_edge_ramp(edited_image);
  CHECK(opaque_after_edit > 0);
  CHECK(ramp_after_edit <= 3);
}

void ui_point_text_repeated_transform_keeps_tight_bounds() {
  // Regression: repeated scale up/down must keep the layer bounds hugging the glyphs and the point size
  // folding exactly -- no horizontal runaway / "partial letters way too big".  The over-wide bounds bug
  // (free-transform rect ~3x wider than the text) fed the next transform and compounded; this guards it.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto cw = canvas->widget_position_for_document_point(QPoint(100, 140));
  send_mouse(*canvas, QEvent::MouseButtonPress, cw, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, cw, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("New"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  const auto id = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
  CHECK(id.has_value());
  const auto read_size = [&]() {
    const auto* l = patchy::ui::MainWindowTestAccess::document(window).find_layer(*id);
    const auto it = l->metadata().find(patchy::kLayerMetadataTextSize);
    return it == l->metadata().end() ? 0 : std::atoi(it->second.c_str());
  };
  const auto check_tight = [&]() {
    const auto* l = patchy::ui::MainWindowTestAccess::document(window).find_layer(*id);
    const auto img = image_from_pixels_for_visuals(l->pixels());
    int minx = img.width(), maxx = -1, miny = img.height(), maxy = -1;
    for (int y = 0; y < img.height(); ++y) {
      for (int x = 0; x < img.width(); ++x) {
        if (qAlpha(img.pixel(x, y)) > 20) {
          minx = std::min(minx, x);
          maxx = std::max(maxx, x);
          miny = std::min(miny, y);
          maxy = std::max(maxy, y);
        }
      }
    }
    const int visible_w = maxx >= minx ? maxx - minx + 1 : 0;
    const int visible_h = maxy >= miny ? maxy - miny + 1 : 0;
    // The layer raster must hug the inked glyphs in BOTH axes (no transparent runaway margin).
    CHECK(visible_w > 0);
    CHECK(img.width() - visible_w <= 6);
    CHECK(img.height() - visible_h <= 6);
    // And it must never balloon to an absurd size for a ~3-letter word.
    CHECK(img.width() < 4000);
  };
  const auto scale = [&](double pct) {
    canvas->set_show_transform_controls(true);
    QApplication::processEvents();
    require_action(window, "editFreeTransformAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->free_transform_active());
    window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"))->setValue(pct);
    window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"))->setValue(pct);
    QApplication::processEvents();
    send_key(*canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(!canvas->free_transform_active());
  };
  const int base_size = read_size();
  CHECK(base_size > 0);
  // (The freshly-created layer keeps line-box height; the crisp transform path trims to inked glyphs.)
  scale(200.0);
  check_tight();
  CHECK(std::abs(read_size() - base_size * 2) <= 2);
  scale(50.0);
  check_tight();
  CHECK(std::abs(read_size() - base_size) <= 2);
  scale(300.0);
  check_tight();
  CHECK(std::abs(read_size() - base_size * 3) <= 3);
  scale(50.0);
  check_tight();
}

void ui_point_text_transform_psd_roundtrip_shows_scaled_size() {
  // #2/#3: scaling a point-text layer folds the scale into the point size, so a saved PSD records the
  // larger FontSize (and a residual matrix) -- Photoshop's Character panel then shows the scaled size.
  // Verify by writing the document to PSD bytes and reading them back: the re-imported font size (which
  // the importer takes straight from the EngineData FontSize) must reflect the 2x scale.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto create_widget = canvas->widget_position_for_document_point(QPoint(120, 140));
  send_mouse(*canvas, QEvent::MouseButtonPress, create_widget, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, create_widget, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Roundtrip"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();

  const auto layer_id = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
  CHECK(layer_id.has_value());
  const auto base_size_of = [&](const patchy::Document& doc) -> int {
    const auto* l = doc.find_layer(*layer_id);
    if (l == nullptr) {
      return 0;
    }
    const auto it = l->metadata().find(patchy::kLayerMetadataTextSize);
    return it == l->metadata().end() ? 0 : std::atoi(it->second.c_str());
  };
  const int base_size = base_size_of(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(base_size > 0);

  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"))->setValue(200.0);
  window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"))->setValue(200.0);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // The internal size folded to ~2x.
  const int folded_size = base_size_of(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(folded_size >= base_size * 2 - base_size / 4);
  CHECK(folded_size <= base_size * 2 + base_size / 4);

  // Write to PSD bytes and read back; the re-imported font size must reflect the 2x scale.
  const auto bytes =
      patchy::psd::DocumentIo::write_layered_rgb8(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(!bytes.empty());
  const auto reopened = patchy::psd::DocumentIo::read(bytes);
  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&)> find_text =
      [&](const std::vector<patchy::Layer>& layers) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      const auto it = layer.metadata().find(patchy::kLayerMetadataText);
      if (it != layer.metadata().end() &&
          QString::fromStdString(it->second).trimmed().compare(QStringLiteral("Roundtrip"), Qt::CaseInsensitive) ==
              0) {
        return &layer;
      }
      if (const auto* found = find_text(layer.children()); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  const auto* reimported = find_text(reopened.layers());
  CHECK(reimported != nullptr);
  const auto size_it = reimported->metadata().find(patchy::kLayerMetadataTextSize);
  CHECK(size_it != reimported->metadata().end());
  const int reimported_size = std::atoi(size_it->second.c_str());
  CHECK(reimported_size >= base_size * 2 - base_size / 2);
}

void ui_rotated_box_text_edit_uses_single_transformed_preview() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 120)),
       canvas->widget_position_for_document_point(QPoint(390, 205)));
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Rotated text should stay editable\nwithout drawing twice"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto text_layer_id = document.active_layer_id();
  CHECK(text_layer_id.has_value());
  auto* text_layer = document.find_layer(*text_layer_id);
  CHECK(text_layer != nullptr);
  const auto unrotated_bounds = text_layer->bounds();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  CHECK(rotation != nullptr);
  rotation->setValue(32.0);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  text_layer = document.find_layer(*text_layer_id);
  CHECK(text_layer != nullptr);
  const auto rotated_bounds = text_layer->bounds();
  CHECK(rotated_bounds.width != unrotated_bounds.width || rotated_bounds.height != unrotated_bounds.height);
  CHECK(text_layer->metadata().contains(patchy::kLayerMetadataTextTransform));

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto edit_point = canvas->widget_position_for_document_point(
      QPoint(rotated_bounds.x + rotated_bounds.width / 2, rotated_bounds.y + rotated_bounds.height / 2));
  send_mouse(*canvas, QEvent::MouseButtonPress, edit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, edit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(!editor->property("patchy.textRenderLocalRect").isValid());
  CHECK(!editor->property("patchy.extendedBoxPreview").toBool());
  CHECK(!editor->property("patchy.lineAwareBoxPreview").toBool());
  CHECK(count_internal_text_preview_layers(document) == 1);
  text_layer = document.find_layer(*text_layer_id);
  CHECK(text_layer != nullptr);
  CHECK(!text_layer->visible());

  auto* preview_layer = preview_layer_for_editor(document, *editor);
  CHECK(preview_layer != nullptr);
  const auto initial_preview_id = editor->property("patchy.textPreviewLayerId").toULongLong();
  const auto preview_bounds = preview_layer->bounds();
  CHECK(preview_bounds.width <= rotated_bounds.width + 36);
  CHECK(preview_bounds.height <= rotated_bounds.height + 36);

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  process_events_for(120);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.textPreviewLayerId").toULongLong() == initial_preview_id);
  CHECK(count_internal_text_preview_layers(document) == 1);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(!editor->property("patchy.textRenderLocalRect").isValid());
  const auto caret = editor->property("patchy.previewCaretRect").toRect();
  CHECK(!caret.isEmpty());
  CHECK(caret.height() <= 96);
  preview_layer = preview_layer_for_editor(document, *editor);
  CHECK(preview_layer != nullptr);
  const auto edited_preview_bounds = preview_layer->bounds();
  CHECK(edited_preview_bounds.width <= rotated_bounds.width + 48);
  CHECK(edited_preview_bounds.height <= rotated_bounds.height + 48);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(count_internal_text_preview_layers(document) == 0);
  text_layer = document.find_layer(*text_layer_id);
  CHECK(text_layer != nullptr);
  CHECK(text_layer->visible());

  const auto committed_bounds = text_layer->bounds();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto reedit_point = canvas->widget_position_for_document_point(
      QPoint(committed_bounds.x + committed_bounds.width / 2, committed_bounds.y + committed_bounds.height / 2));
  send_mouse(*canvas, QEvent::MouseButtonPress, reedit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, reedit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(!editor->property("patchy.textRenderLocalRect").isValid());
  CHECK(count_internal_text_preview_layers(document) == 1);
  save_widget_artifact("ui_rotated_box_text_edit_single_preview", *canvas);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_transformed_expensive_text_preview_stays_transformed_while_typing() {
  patchy::Document document(460, 280, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(460, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(220, 150, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(20, 30, 142, 42), QColor(32, 32, 32, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Rotated Glow", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{132, 80, 220, 150});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Rotated";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "36";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  text_layer.metadata()[patchy::kLayerMetadataTextTransform] = "0.8660254 0.5 -0.5 0.8660254 176 82";
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 0.8F;
  glow.size = 64.0F;
  text_layer.layer_style().outer_glows.push_back(glow);
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Transformed Expensive Text Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(220, 130));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.expensiveTextStylePreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  const auto preview_id = editor->property("patchy.textPreviewLayerId").toULongLong();

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  QApplication::processEvents();

  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").toULongLong() == preview_id);
  CHECK(editor->property("patchy.textPreviewPending").toBool());

  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_text_edit_ctrl_t_commits_editor_before_free_transform() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto* type_action = require_action_by_text(window, QStringLiteral("Type"));
  auto* move_action = require_action_by_text(window, QStringLiteral("Move"));

  type_action->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(88, 92)),
       canvas->widget_position_for_document_point(QPoint(260, 150)));
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Transform me"));
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();

  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(canvas->free_transform_active());
  auto* text_resize_handle =
      canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleTopLeft"), Qt::FindDirectChildrenOnly);
  CHECK(text_resize_handle == nullptr || !text_resize_handle->isVisible());
  const auto transform_rect = canvas->active_layer_document_rect();
  CHECK(transform_rect.has_value());
  send_double_click(*canvas, canvas->widget_position_for_document_point(transform_rect->center()));
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(!canvas->free_transform_active());
  CHECK(type_action->isChecked());
  CHECK(!move_action->isChecked());
  CHECK(!canvas->transform_controls_state().has_value());
  text_resize_handle =
      canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleTopLeft"), Qt::FindDirectChildrenOnly);
  CHECK(text_resize_handle != nullptr);
  CHECK(text_resize_handle->isVisible());
  CHECK(text_resize_handle->size() == QSize(9, 9));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  move_action->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  CHECK(canvas->transform_controls_state().has_value());
  const auto passive_transform_rect = canvas->active_layer_document_rect();
  CHECK(passive_transform_rect.has_value());
  send_double_click(*canvas, canvas->widget_position_for_document_point(passive_transform_rect->center()));
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(type_action->isChecked());
  CHECK(!move_action->isChecked());
  CHECK(!canvas->free_transform_active());
  CHECK(!canvas->transform_controls_state().has_value());
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_text_free_transform_clicking_current_move_tool_applies() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto* type_action = require_action_by_text(window, QStringLiteral("Type"));
  auto* move_action = require_action_by_text(window, QStringLiteral("Move"));

  type_action->trigger();
  const auto text_point = canvas->widget_position_for_document_point(QPoint(92, 104));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Rotate me"));
  QApplication::processEvents();

  move_action->trigger();
  QApplication::processEvents();
  CHECK(move_action->isChecked());
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  CHECK(rotation != nullptr);
  rotation->setValue(30.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  move_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto after = canvas->active_layer_document_rect();
  CHECK(after.has_value());
  CHECK(after->width() != before->width() || after->height() != before->height());
}

void ui_text_box_commit_renders_paragraph_alignment() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(Qt::black));
  const QPoint box_top_left(40, 90);
  const QPoint box_bottom_right(340, 150);
  drag(*canvas, canvas->widget_position_for_document_point(box_top_left),
       canvas->widget_position_for_document_point(box_bottom_right));
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  auto* center = window.findChild<QPushButton*>(QStringLiteral("textAlignCenterButton"));
  CHECK(center != nullptr);
  center->click();
  QApplication::processEvents();
  editor->setPlainText(QStringLiteral("Hi"));
  center->click();
  QApplication::processEvents();
  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->setCursorWidth(0);
  QApplication::processEvents();
  save_widget_artifact("ui_text_alignment_center_editing", *canvas);
  const auto text_band = QRect(box_top_left + QPoint(0, 14),
                               QSize(box_bottom_right.x() - box_top_left.x(), 34));
  const auto active_bounds = dark_document_bounds(*canvas, text_band);
  CHECK(active_bounds.has_value());
  CHECK(active_bounds->left() > box_top_left.x() + 90);
  CHECK(active_bounds->right() < box_bottom_right.x() - 90);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  save_widget_artifact("ui_text_alignment_center_committed", *canvas);

  const auto committed_bounds = dark_document_bounds(*canvas, text_band);
  CHECK(committed_bounds.has_value());
  CHECK(committed_bounds->left() > box_top_left.x() + 90);
  CHECK(committed_bounds->right() < box_bottom_right.x() - 90);
  CHECK(std::abs(committed_bounds->left() - active_bounds->left()) <= 4);
  CHECK(std::abs(committed_bounds->right() - active_bounds->right()) <= 4);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  auto* right = window.findChild<QPushButton*>(QStringLiteral("textAlignRightButton"));
  CHECK(right != nullptr);
  const QPoint right_box_top_left(40, 180);
  const QPoint right_box_bottom_right(340, 240);
  drag(*canvas, canvas->widget_position_for_document_point(right_box_top_left),
       canvas->widget_position_for_document_point(right_box_bottom_right));
  QApplication::processEvents();

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Hi"));
  right->click();
  cursor = QTextCursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->setCursorWidth(0);
  QApplication::processEvents();
  save_widget_artifact("ui_text_alignment_right_editing", *canvas);
  const auto right_text_band = QRect(right_box_top_left + QPoint(0, 14),
                                     QSize(right_box_bottom_right.x() - right_box_top_left.x(), 34));
  const auto active_right_bounds = dark_document_bounds(*canvas, right_text_band);
  CHECK(active_right_bounds.has_value());
  CHECK(active_right_bounds->left() > right_box_bottom_right.x() - 90);
  CHECK(active_right_bounds->right() <= right_box_bottom_right.x());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  save_widget_artifact("ui_text_alignment_right_committed", *canvas);

  const auto committed_right_bounds = dark_document_bounds(*canvas, right_text_band);
  CHECK(committed_right_bounds.has_value());
  CHECK(committed_right_bounds->left() > right_box_bottom_right.x() - 90);
  CHECK(committed_right_bounds->right() <= right_box_bottom_right.x());
  CHECK(std::abs(committed_right_bounds->left() - active_right_bounds->left()) <= 4);
  CHECK(std::abs(committed_right_bounds->right() - active_right_bounds->right()) <= 4);
}

void ui_text_character_panel_sets_leading_tracking_and_scales() {
  // The Character panel (options bar > Character...) edits the LIVE session: fixed leading,
  // tracking, and glyph scales apply to the selection, survive the commit as v3 runs, drive
  // the committed raster (leading = baseline advance), and reflect back on re-edit. The panel
  // must not trip the editor's focus-loss auto-commit (is_text_option_widget exemption).
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(Qt::black));
  const auto click_widget_point = canvas->widget_position_for_document_point(QPoint(60, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->setPlainText(QStringLiteral("Char panel top\nChar panel base"));
  editor->selectAll();
  QApplication::processEvents();

  // The panel runs a nested non-modal loop; drive it from a queued lambda. 100 pt at the
  // startup document's 72 ppi = 100 document px of fixed leading.
  auto* character_button = window.findChild<QPushButton*>(QStringLiteral("textCharacterButton"));
  CHECK(character_button != nullptr);
  if (character_button == nullptr) {
    return;
  }
  QTimer::singleShot(0, [&window] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* auto_leading = dialog->findChild<QCheckBox*>(QStringLiteral("textCharacterAutoLeading"));
    auto* leading = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("textCharacterLeadingSpin"));
    auto* tracking = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterTrackingSpin"));
    auto* h_scale = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterHScaleSpin"));
    CHECK(auto_leading != nullptr && leading != nullptr && tracking != nullptr && h_scale != nullptr);
    if (auto_leading == nullptr || leading == nullptr || tracking == nullptr || h_scale == nullptr) {
      dialog->reject();
      return;
    }
    auto_leading->setChecked(false);
    leading->setValue(100.0);
    tracking->setValue(100);
    h_scale->setValue(120);
    QApplication::processEvents();
    dialog->reject();
  });
  character_button->click();
  QApplication::processEvents();
  // The panel interaction must have left the session alive (no focus-loss commit).
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::Layer* committed = nullptr;
  for (auto& layer : document.layers()) {
    if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
        it != layer.metadata().end() && it->second.find("Char panel top") != std::string::npos) {
      committed = &layer;
    }
  }
  CHECK(committed != nullptr);
  if (committed == nullptr) {
    return;
  }
  CHECK(committed->metadata().at(patchy::kLayerMetadataTextLayoutMode) == patchy::kTextLayoutModePhotoshop);
  const auto runs = QString::fromStdString(committed->metadata().at(patchy::kLayerMetadataTextRuns));
  CHECK(runs.startsWith(QStringLiteral("v3\n")));
  bool found_run = false;
  for (const auto& line : runs.split(QLatin1Char('\n')).mid(1)) {
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 11) {
      continue;
    }
    found_run = true;
    CHECK(std::abs(fields[7].toDouble() - 100.0) < 0.5);   // fixed leading, document px
    CHECK(std::abs(fields[8].toDouble() - 100.0) < 0.5);   // tracking
    CHECK(std::abs(fields[9].toDouble() - 1.2) < 0.005);   // horizontal scale
    CHECK(std::abs(fields[10].toDouble() - 1.0) < 0.005);  // vertical scale
  }
  CHECK(found_run);
  // The committed raster's baseline advance IS the fixed leading.
  const auto bands = alpha_row_bands(committed->pixels());
  CHECK(bands.size() == 2);
  if (bands.size() == 2) {
    CHECK(std::abs((bands[1].bottom - bands[0].bottom) - 100) <= 2);
  }

  // Re-edit: the panel reflects the committed values back.
  document.set_active_layer(committed->id());
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto reedit_point = canvas->widget_position_for_document_point(
      QPoint(committed->bounds().x + committed->bounds().width / 2, committed->bounds().y + 8));
  send_mouse(*canvas, QEvent::MouseButtonPress, reedit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, reedit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(200);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr);
  QTimer::singleShot(0, [&window] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* auto_leading = dialog->findChild<QCheckBox*>(QStringLiteral("textCharacterAutoLeading"));
    auto* leading = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("textCharacterLeadingSpin"));
    auto* tracking = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterTrackingSpin"));
    auto* h_scale = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterHScaleSpin"));
    if (auto_leading != nullptr && leading != nullptr && tracking != nullptr && h_scale != nullptr) {
      CHECK(!auto_leading->isChecked());
      CHECK(std::abs(leading->value() - 100.0) < 0.05);
      CHECK(tracking->value() == 100);
      CHECK(h_scale->value() == 120);
    }
    dialog->reject();
  });
  character_button->click();
  QApplication::processEvents();
  send_key(*canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")), Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_text_character_panel_disables_without_session() {
  // With no live editor session the Character panel grays out and shows its click-in-text
  // hint, and the state tracks session boundaries LIVE while the non-modal dialog stays
  // open: a committed session used to leave the controls enabled, silently no-oping every
  // edit (the apply functions early-return without an editor).
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(Qt::black));
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  auto* character_button = window.findChild<QPushButton*>(QStringLiteral("textCharacterButton"));
  CHECK(character_button != nullptr);
  if (character_button == nullptr) {
    return;
  }
  // The panel runs a nested non-modal loop; drive the whole scenario from a queued lambda.
  bool checks_ran = false;
  QTimer::singleShot(0, [&window, canvas, &checks_ran] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* hint = dialog->findChild<QLabel*>(QStringLiteral("textCharacterHint"));
    auto* auto_leading = dialog->findChild<QCheckBox*>(QStringLiteral("textCharacterAutoLeading"));
    auto* leading = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("textCharacterLeadingSpin"));
    auto* tracking = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterTrackingSpin"));
    auto* h_scale = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterHScaleSpin"));
    auto* v_scale = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterVScaleSpin"));
    CHECK(hint != nullptr && auto_leading != nullptr && leading != nullptr && tracking != nullptr &&
          h_scale != nullptr && v_scale != nullptr);
    if (hint == nullptr || auto_leading == nullptr || leading == nullptr || tracking == nullptr ||
        h_scale == nullptr || v_scale == nullptr) {
      dialog->reject();
      return;
    }
    // Opened with no session: everything grayed, hint explains why.
    CHECK(hint->isVisible());
    CHECK(!auto_leading->isEnabled());
    CHECK(!leading->isEnabled());
    CHECK(!tracking->isEnabled());
    CHECK(!h_scale->isEnabled());
    CHECK(!v_scale->isEnabled());

    // Start a session while the dialog stays open: controls come alive, hint hides.
    // add_text_at is the exact call a Type-tool canvas click funnels into; a synthetic
    // click sent while the dialog is the active window loses its drag state to an
    // offscreen-platform focus bounce (canvas focusOutEvent clears dragging_text_rect_).
    patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(60, 90));
    QApplication::processEvents();
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    if (editor != nullptr) {
      editor->setPlainText(QStringLiteral("Live again"));
      QApplication::processEvents();
      CHECK(!hint->isVisible());
      CHECK(auto_leading->isEnabled());
      CHECK(tracking->isEnabled());
      CHECK(h_scale->isEnabled());
      CHECK(v_scale->isEnabled());
    }

    // Commit the session (tool switch) with the dialog still open: back to grayed + hint.
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    QApplication::processEvents();
    CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
    CHECK(hint->isVisible());
    CHECK(!auto_leading->isEnabled());
    CHECK(!leading->isEnabled());
    CHECK(!tracking->isEnabled());
    CHECK(!h_scale->isEnabled());
    CHECK(!v_scale->isEnabled());
    checks_ran = true;
    dialog->reject();
  });
  character_button->click();
  QApplication::processEvents();
  CHECK(checks_ran);
}

void ui_point_text_commit_renders_center_alignment() {
  // Regression: point text lays out against an unconstrained width, which made Qt silently ignore
  // paragraph alignment -- a centered multi-line point-text layer committed with every line flush
  // left even though the toolbar (and the stored paragraph runs) still said "center".
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(Qt::black));
  const QPoint click_document_point(60, 90);
  const auto click_widget_point = canvas->widget_position_for_document_point(click_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, click_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("The widest line by far\nHi"));
  editor->selectAll();
  auto* center = window.findChild<QPushButton*>(QStringLiteral("textAlignCenterButton"));
  CHECK(center != nullptr);
  center->click();
  QApplication::processEvents();
  save_widget_artifact("ui_point_text_center_alignment_editing", *canvas);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  save_widget_artifact("ui_point_text_center_alignment_committed", *canvas);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::Layer* committed = nullptr;
  for (auto& layer : document.layers()) {
    if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
        it != layer.metadata().end() && it->second.find("The widest line by far") != std::string::npos) {
      committed = &layer;
    }
  }
  CHECK(committed != nullptr);
  if (committed == nullptr) {
    return;
  }
  // New text layers are named by their content -- no "Text: " prefix (the list shows a type badge).
  CHECK(committed->name() == "The widest line by far H...");
  CHECK(committed->metadata().contains(patchy::kLayerMetadataTextParagraphRuns));
  CHECK(QString::fromStdString(committed->metadata().at(patchy::kLayerMetadataTextParagraphRuns))
            .contains(QStringLiteral("center")));

  const auto bands = alpha_row_bands(committed->pixels());
  CHECK(bands.size() == 2);
  if (bands.size() != 2) {
    return;
  }
  const auto first_line = alpha_pixel_bounds_in_rows(committed->pixels(), bands[0].top, bands[0].bottom);
  const auto second_line = alpha_pixel_bounds_in_rows(committed->pixels(), bands[1].top, bands[1].bottom);
  CHECK(first_line.has_value());
  CHECK(second_line.has_value());
  if (!first_line.has_value() || !second_line.has_value()) {
    return;
  }
  // "Hi" is far narrower than the long line; centered layout puts its midpoint on the same axis,
  // while the regression left both lines flush against the left edge.
  CHECK(second_line->width() < first_line->width() - 40);
  const auto first_center = first_line->left() + first_line->width() / 2.0;
  const auto second_center = second_line->left() + second_line->width() / 2.0;
  CHECK(std::abs(first_center - second_center) <= 6.0);
  CHECK(second_line->left() > first_line->left() + 20);
}

}  // namespace

std::vector<patchy::test::TestCase> text_transform_commit_tests_part1() {
  return {
      {"ui_transformed_text_reedit_preserves_transform",
       ui_transformed_text_reedit_preserves_transform},
      {"ui_point_text_transform_scales_crisply", ui_point_text_transform_scales_crisply},
      {"ui_point_text_transform_psd_roundtrip_shows_scaled_size",
       ui_point_text_transform_psd_roundtrip_shows_scaled_size},
      {"ui_point_text_repeated_transform_keeps_tight_bounds",
       ui_point_text_repeated_transform_keeps_tight_bounds},
      {"ui_rotated_box_text_edit_uses_single_transformed_preview",
       ui_rotated_box_text_edit_uses_single_transformed_preview},
      {"ui_transformed_expensive_text_preview_stays_transformed_while_typing",
       ui_transformed_expensive_text_preview_stays_transformed_while_typing},
      {"ui_text_edit_ctrl_t_commits_editor_before_free_transform",
       ui_text_edit_ctrl_t_commits_editor_before_free_transform},
      {"ui_text_free_transform_clicking_current_move_tool_applies",
       ui_text_free_transform_clicking_current_move_tool_applies},
      {"ui_text_box_commit_renders_paragraph_alignment", ui_text_box_commit_renders_paragraph_alignment},
      {"ui_point_text_commit_renders_center_alignment", ui_point_text_commit_renders_center_alignment},
      {"ui_text_character_panel_sets_leading_tracking_and_scales",
       ui_text_character_panel_sets_leading_tracking_and_scales},
      {"ui_text_character_panel_disables_without_session",
       ui_text_character_panel_disables_without_session},
  };
}
