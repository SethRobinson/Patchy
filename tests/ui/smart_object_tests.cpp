#include "ui/canvas_widget.hpp"
#include "ui/qt_paths.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/vector_shape.hpp"
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
#include "psd/psd_descriptor.hpp"
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
#include "formats/svg_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_api.hpp"
#include <QJSEngine>
#include "ui/qt_paths.hpp"
#include "unicode_path_names.hpp"
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

void ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);

  const auto long_name = QStringLiteral(
      "Placed artwork with an intentionally long layer name that must yield space to its badges");
  layer->set_name(long_name.toStdString());
  layer->set_blend_mode(patchy::BlendMode::LinearDodge);
  patchy::PixelBuffer mask_pixels(document.width(), document.height(), patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  layer->set_mask(patchy::LayerMask{patchy::Rect{0, 0, document.width(), document.height()},
                                    std::move(mask_pixels), 0, true});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.75F;
  shadow.distance = 4.0F;
  shadow.size = 3.0F;
  layer->layer_style().drop_shadows.push_back(shadow);

  auto* layers_dock = window.findChild<QDockWidget*>(QStringLiteral("layersDock"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers_dock != nullptr);
  CHECK(layer_list != nullptr);
  const int narrow_width = layers_dock->minimumWidth();
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, narrow_width);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  CHECK(layers_dock->width() == narrow_width);

  auto* item = require_layer_item(*layer_list, long_name);
  auto* row = layer_list->itemWidget(item);
  CHECK(row != nullptr);
  auto* name = row->findChild<QLabel*>(QStringLiteral("layerRowName"));
  auto* details = row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  auto* fx_badge = row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton"));
  auto* smart_badge = row->findChild<QToolButton*>(QStringLiteral("layerSmartObjectBadgeButton"));
  CHECK(name != nullptr);
  CHECK(details != nullptr);
  CHECK(fx_badge != nullptr);
  CHECK(smart_badge != nullptr);

  const auto assert_badges_visible = [&] {
    layer_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
    layer_list->horizontalScrollBar()->setValue(layer_list->horizontalScrollBar()->minimum());
    QApplication::processEvents();
    auto* viewport = layer_list->viewport();
    CHECK(viewport != nullptr);
    const QRect fx_rect(fx_badge->mapTo(viewport, QPoint()), fx_badge->size());
    const QRect smart_rect(smart_badge->mapTo(viewport, QPoint()), smart_badge->size());
    CHECK(fx_badge->isEnabled());
    CHECK(smart_badge->isEnabled());
    CHECK(viewport->rect().contains(fx_rect));
    CHECK(viewport->rect().contains(smart_rect));
    CHECK(!fx_rect.intersects(smart_rect));
    CHECK(fx_rect.right() < smart_rect.left());
    auto* fx_hit = viewport->childAt(fx_rect.center());
    auto* smart_hit = viewport->childAt(smart_rect.center());
    CHECK(fx_hit == fx_badge || (fx_hit != nullptr && fx_badge->isAncestorOf(fx_hit)));
    CHECK(smart_hit == smart_badge || (smart_hit != nullptr && smart_badge->isAncestorOf(smart_hit)));
  };

  assert_badges_visible();
  CHECK(name->text() == long_name);
  CHECK(name->fontMetrics().horizontalAdvance(name->text()) > name->width());
  CHECK(details->fontMetrics().horizontalAdvance(details->text()) > details->width());
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, narrow_width + 180);
  QApplication::processEvents();
  assert_badges_visible();
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, narrow_width);
  QApplication::processEvents();
  CHECK(layers_dock->width() == narrow_width);
  assert_badges_visible();
  save_widget_artifact("ui_layer_fx_smart_badges_narrow_panel", *layers_dock);
}

// The smart-object badge is a clickable icon button on the row's details line (it
// replaced the old "smart" text): clicking it opens the smart object's contents,
// exactly like Edit Smart Object Contents (row double-click opens layer styles).
void ui_layer_smart_object_badge_button_opens_contents() {
  patchy::ui::MainWindow window;
  show_window(window);
  open_smart_object_fixture(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("small")));
  CHECK(row != nullptr);
  auto* smart_badge = row->findChild<QToolButton*>(QStringLiteral("layerSmartObjectBadgeButton"));
  CHECK(smart_badge != nullptr);
  CHECK(!smart_badge->property("smartObjectLinked").toBool());
  CHECK(smart_badge->toolTip().contains(QStringLiteral("Smart object")));
  auto* details = row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(!details->text().contains(QStringLiteral("smart")));

  send_mouse(*smart_badge, QEvent::MouseButtonPress, smart_badge->rect().center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*smart_badge, QEvent::MouseButtonRelease, smart_badge->rect().center(), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(tabs->count() == tab_count_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  CHECK(tabs->tabText(tabs->currentIndex()).contains(QStringLiteral("small.png (embedded in")));
}

// "Convert to Normal Layer (Rasterize)" in the Smart Objects menus is a
// discoverable alias for Rasterize: it demotes the smart object to a plain
// pixel layer while keeping its rendered pixels.
void ui_smart_object_to_normal_layer_action_rasterizes() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* smart_layer = document.find_layer(layer_id);
  CHECK(smart_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*smart_layer));
  const auto pixels_before = smart_layer->pixels();

  auto* to_normal = require_action(window, "layerSmartObjectToNormalAction");
  CHECK(to_normal->text().contains(QStringLiteral("Convert to Normal Layer")));
  to_normal->trigger();
  QApplication::processEvents();

  const auto* rasterized = document.find_layer(layer_id);
  CHECK(rasterized != nullptr);
  CHECK(!patchy::layer_is_smart_object(*rasterized));
  CHECK(rasterized->pixels().width() == pixels_before.width());
  CHECK(rasterized->pixels().height() == pixels_before.height());

  // The layer row lost its smart-object badge after the conversion.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  for (int i = 0; i < layer_list->count(); ++i) {
    auto* row = layer_list->itemWidget(layer_list->item(i));
    CHECK(row == nullptr ||
          row->findChild<QToolButton*>(QStringLiteral("layerSmartObjectBadgeButton")) == nullptr);
  }
}

// Painting on a smart object pops the paint prompt (paintSmartObjectMessageBox)
// instead of the old status-bar refusal. Rasterize demotes just the painted
// layer in one undo step; the triggering press is consumed (the modal swallowed
// its release), so the user strokes again on the now-plain layer.
void ui_smart_object_paint_prompt_rasterize_choice() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 20, 20));
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  const QPoint stroke_document_point(48, 48);  // inside the placed content
  const auto before_stroke = canvas_pixel(*canvas, stroke_document_point);
  const auto stroke_widget_point = canvas->widget_position_for_document_point(stroke_document_point);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("paintSmartObjectMessageBox")));
    CHECK(box != nullptr);
    if (box == nullptr) {
      return;
    }
    saw_dialog = true;
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
        button->click();
        return;
      }
    }
    CHECK(false);
  });
  send_mouse(*canvas, QEvent::MouseButtonPress, stroke_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, stroke_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_dialog);

  const auto* rasterized = document.find_layer(layer_id);
  CHECK(rasterized != nullptr);
  CHECK(!patchy::layer_is_smart_object(*rasterized));
  CHECK(color_close(canvas_pixel(*canvas, stroke_document_point), before_stroke, 0));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Paint again")));

  // A second stroke lands now that the layer is a plain pixel layer.
  send_mouse(*canvas, QEvent::MouseButtonPress, stroke_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, stroke_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!color_close(canvas_pixel(*canvas, stroke_document_point), before_stroke, 0));

  // Undo the stroke, then the rasterize: the smart object comes back.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = document.find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(patchy::layer_is_smart_object(*restored));
}

// The prompt's Edit Contents button opens the embedded source as a child tab,
// exactly like the badge click, leaving the smart object and its pixels alone.
void ui_smart_object_paint_prompt_edit_contents_choice() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 20, 20));
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();

  const QPoint stroke_document_point(48, 48);
  const auto before_stroke = canvas_pixel(*canvas, stroke_document_point);
  const auto stroke_widget_point = canvas->widget_position_for_document_point(stroke_document_point);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("paintSmartObjectMessageBox")));
    CHECK(box != nullptr);
    if (box == nullptr) {
      return;
    }
    saw_dialog = true;
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
    CHECK(false);
  });
  send_mouse(*canvas, QEvent::MouseButtonPress, stroke_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, stroke_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_dialog);

  CHECK(tabs->count() == tab_count_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*layer));
  CHECK(color_close(canvas_pixel(*canvas, stroke_document_point), before_stroke, 0));
}

// Cancel (and Esc, which lands on the same button) leaves everything untouched.
void ui_smart_object_paint_prompt_cancel_choice() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 20, 20));
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  const QPoint stroke_document_point(48, 48);
  const auto before_stroke = canvas_pixel(*canvas, stroke_document_point);
  const auto stroke_widget_point = canvas->widget_position_for_document_point(stroke_document_point);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("paintSmartObjectMessageBox")));
    CHECK(box != nullptr);
    if (box == nullptr) {
      return;
    }
    saw_dialog = true;
    auto* cancel = box->button(QMessageBox::Cancel);
    CHECK(cancel != nullptr);
    cancel->click();
  });
  send_mouse(*canvas, QEvent::MouseButtonPress, stroke_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, stroke_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_dialog);

  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*layer));
  CHECK(color_close(canvas_pixel(*canvas, stroke_document_point), before_stroke, 0));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before);
  CHECK(tabs->count() == tab_count_before);
}

// Preview-locked smart objects (warp, filters, ...) can't open their contents,
// so the prompt omits Edit Contents and offers only Rasterize / Cancel.
void ui_smart_object_paint_prompt_locked_omits_edit_contents() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  layer->metadata()[patchy::kLayerMetadataSmartObjectLock] = "warp";
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();

  const QPoint stroke_document_point(48, 48);
  const auto stroke_widget_point = canvas->widget_position_for_document_point(stroke_document_point);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("paintSmartObjectMessageBox")));
    CHECK(box != nullptr);
    if (box == nullptr) {
      return;
    }
    saw_dialog = true;
    bool has_rasterize = false;
    for (auto* button : box->buttons()) {
      CHECK(box->buttonRole(button) != QMessageBox::AcceptRole);
      has_rasterize = has_rasterize || box->buttonRole(button) == QMessageBox::DestructiveRole;
    }
    CHECK(has_rasterize);
    auto* cancel = box->button(QMessageBox::Cancel);
    CHECK(cancel != nullptr);
    cancel->click();
  });
  send_mouse(*canvas, QEvent::MouseButtonPress, stroke_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, stroke_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_dialog);

  const auto* untouched = document.find_layer(layer_id);
  CHECK(untouched != nullptr);
  CHECK(patchy::layer_is_smart_object(*untouched));
  CHECK(patchy::smart_object_lock_reason(*untouched) == "warp");
}

// Linked (external-file) smart objects get their own badge icon and tooltip so
// they read differently from embedded ones in the panel.
void ui_layer_smart_object_badge_shows_linked_variant() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Embedded",
                           solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(200, 60, 60, 255)));
  document.add_pixel_layer("Linked", solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(60, 200, 60, 255)));
  patchy::SmartObjectPlacement placement;
  placement.uuid = "11111111-2222-3333-4444-555555555555";
  placement.transform = {0.0, 0.0, 32.0, 0.0, 32.0, 32.0, 0.0, 32.0};
  placement.width = 32.0;
  placement.height = 32.0;
  patchy::set_layer_smart_object_metadata(document.layers().front(), placement, "placed-embedded", "SoLd", "",
                                          patchy::kSmartObjectRasterStatusPhotoshop);
  patchy::set_layer_smart_object_metadata(document.layers().back(), placement, "placed-linked", "SoLd", "external",
                                          patchy::kSmartObjectRasterStatusPhotoshop);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Smart Badges"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* embedded_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Embedded")));
  auto* linked_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Linked")));
  CHECK(embedded_row != nullptr);
  CHECK(linked_row != nullptr);
  auto* embedded_badge = embedded_row->findChild<QToolButton*>(QStringLiteral("layerSmartObjectBadgeButton"));
  auto* linked_badge = linked_row->findChild<QToolButton*>(QStringLiteral("layerSmartObjectBadgeButton"));
  CHECK(embedded_badge != nullptr);
  CHECK(linked_badge != nullptr);
  CHECK(!embedded_badge->property("smartObjectLinked").toBool());
  CHECK(linked_badge->property("smartObjectLinked").toBool());
  CHECK(embedded_badge->toolTip().contains(QStringLiteral("Smart object")));
  CHECK(linked_badge->toolTip().contains(QStringLiteral("Linked smart object")));

  // Both icons render real (non-empty) art -- a typo'd qrc alias renders empty
  // silently -- and the linked variant is visually distinct from the embedded one.
  const auto embedded_image = embedded_badge->icon().pixmap(QSize(16, 16)).toImage();
  const auto linked_image = linked_badge->icon().pixmap(QSize(16, 16)).toImage();
  const auto opaque_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (image.pixelColor(x, y).alpha() > 32) {
          ++count;
        }
      }
    }
    return count;
  };
  CHECK(opaque_pixels(embedded_image) > 20);
  CHECK(opaque_pixels(linked_image) > 20);
  CHECK(embedded_image != linked_image);
  save_widget_artifact("ui_layer_smart_object_badge_linked_variant", window);
}

void ui_smart_object_edit_contents_commit_rerenders_parent() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto parent_tab_index = tabs->currentIndex();
  const auto tab_count_before = tabs->count();
  auto& parent_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto uuid = patchy::smart_object_source_uuid(*parent_document.find_layer(layer_id));
  const auto placed_uuid =
      patchy::smart_object_placed_uuid(*parent_document.find_layer(layer_id));
  const auto* source = parent_document.metadata().smart_objects.find(uuid);
  CHECK(source != nullptr && source->file_bytes != nullptr);
  const auto original_bytes = source->file_bytes;
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto original_center = [&]() -> std::array<std::uint8_t, 4> {
    const auto& pixels = parent_document.find_layer(layer_id)->pixels();
    const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
    CHECK(px != nullptr);
    return {px[0], px[1], px[2], px[3]};
  }();

  // Double-clicking a smart object's row opens the LAYER STYLES dialog like any
  // other layer (the badge button / Smart Objects menus open the contents), so
  // the double-click must NOT spawn a child tab.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->currentItem() != nullptr);
  const auto row_center = layer_list->visualItemRect(layer_list->currentItem()).center();
  bool saw_style_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    saw_style_dialog = true;
    dialog->reject();
  });
  QMouseEvent double_click(QEvent::MouseButtonDblClick, QPointF(row_center),
                           layer_list->viewport()->mapToGlobal(row_center), Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
  QApplication::sendEvent(layer_list->viewport(), &double_click);
  QApplication::processEvents();
  CHECK(saw_style_dialog);
  CHECK(tabs->count() == tab_count_before);

  // Edit Smart Object Contents opens the contents as a linked child tab.
  patchy::ui::MainWindowTestAccess::document(window).set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == tab_count_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  CHECK(tabs->tabText(tabs->currentIndex()).contains(QStringLiteral("small.png (embedded in")));
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 32);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).height() == 24);

  // Focus-if-open: reopening from the parent focuses the existing child tab.
  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::document(window).set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == tab_count_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));

  // Edit the child (fill green) and commit with Save; the child marks clean.
  auto& child_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(!child_document.layers().empty());
  auto& child_layer = child_document.layers().front();
  child_layer.set_pixels(solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(20, 200, 40, 255)));
  child_layer.set_bounds(patchy::Rect{0, 0, 32, 24});
  patchy::ui::MainWindowTestAccess::canvas(window)->document_changed();
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // The parent preview re-rendered from the committed bytes, as ONE undo step.
  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  auto& parent_after = patchy::ui::MainWindowTestAccess::document(window);
  const auto* committed_layer = parent_after.find_layer(layer_id);
  CHECK(committed_layer != nullptr);
  const auto& committed_pixels = committed_layer->pixels();
  const auto* center_px = committed_pixels.pixel(committed_pixels.width() / 2, committed_pixels.height() / 2);
  CHECK(center_px != nullptr);
  CHECK(center_px[0] < 60 && center_px[1] > 150 && center_px[2] < 80);
  const auto raster_status = std::as_const(*committed_layer).metadata().find(
      patchy::kLayerMetadataSmartObjectRasterStatus);
  CHECK(raster_status != std::as_const(*committed_layer).metadata().end() &&
        raster_status->second == patchy::kSmartObjectRasterStatusPatchy);
  const auto refreshed_uuid = patchy::smart_object_source_uuid(*committed_layer);
  const auto refreshed_placed_uuid =
      patchy::smart_object_placed_uuid(*committed_layer);
  CHECK(refreshed_uuid != uuid);
  CHECK(refreshed_placed_uuid != placed_uuid);
  CHECK(parent_after.metadata().smart_objects.find(uuid) == nullptr);
  const auto* source_after =
      parent_after.metadata().smart_objects.find(refreshed_uuid);
  CHECK(source_after != nullptr && source_after->file_bytes != nullptr);
  CHECK(source_after->file_bytes != original_bytes);
  CHECK(source_after->dirty);
  // E4 acceptance artifact: Photoshop must open this, edit its contents, and resave
  // clean (verified by the COM gate; see docs/smart-objects.md).
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      parent_after, std::filesystem::path("test-artifacts/ui_smart_object_committed.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // Undo restores both the preview and the embedded bytes (snapshots share the
  // original payload, so pointer equality holds).
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  auto& parent_undone = patchy::ui::MainWindowTestAccess::document(window);
  const auto* undone_source = parent_undone.metadata().smart_objects.find(uuid);
  CHECK(undone_source != nullptr);
  CHECK(undone_source->file_bytes == original_bytes);
  const auto* undone_layer = parent_undone.find_layer(layer_id);
  CHECK(undone_layer != nullptr);
  const auto& undone_pixels = undone_layer->pixels();
  const auto* undone_px = undone_pixels.pixel(undone_pixels.width() / 2, undone_pixels.height() / 2);
  CHECK(undone_px != nullptr);
  CHECK(undone_px[0] == original_center[0] && undone_px[1] == original_center[1] &&
        undone_px[2] == original_center[2] && undone_px[3] == original_center[3]);
  // The still-open child must follow parent history across UUID replacements.
  patchy::ui::MainWindowTestAccess::redo(window);
  patchy::ui::MainWindowTestAccess::undo(window);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == tab_count_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);

}

void ui_smart_object_locked_refusal_and_parent_close_prompt() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();  // a fresh window may hold an Untitled tab too
  const auto parent_tab_index = tabs->currentIndex();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A preview-locked smart object refuses Edit Contents with an explanation.
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  layer->metadata()[patchy::kLayerMetadataSmartObjectLock] = "filters";
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == tab_count_before);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Smart Filters")));
  layer->metadata().erase(patchy::kLayerMetadataSmartObjectLock);

  // Editable again: open the child, then close the PARENT tab. The children prompt
  // appears; accepting it closes the child first, then the parent.
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == tab_count_before + 1);
  int close_poll_attempts = 0;
  bool saw_children_prompt = false;
  QTimer close_poller;
  QObject::connect(&close_poller, &QTimer::timeout, [&close_poll_attempts, &saw_children_prompt, &close_poller] {
    if (++close_poll_attempts > 500) {
      close_poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box != nullptr && box->objectName() == QStringLiteral("closeSmartObjectChildrenMessageBox") &&
          box->isVisible()) {
        saw_children_prompt = true;
        box->button(QMessageBox::Yes)->click();
        close_poller.stop();
        return;
      }
    }
  });
  close_poller.start(10);
  const bool closed = patchy::ui::MainWindowTestAccess::close_document_tab(window, parent_tab_index);
  QApplication::processEvents();
  close_poller.stop();
  CHECK(closed);
  CHECK(saw_children_prompt);
  CHECK(tabs->count() == tab_count_before - 1);  // child and parent both gone
}

void ui_smart_object_replace_contents_repoints_shared_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto old_uuid = patchy::smart_object_source_uuid(*document.find_layer(layer_id));
  const auto* old_source = document.metadata().smart_objects.find(old_uuid);
  CHECK(old_source != nullptr && old_source->file_bytes != nullptr);
  const auto original_bytes = old_source->file_bytes;

  // Duplicate the layer: both instances share the source uuid (Photoshop's rule).
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  std::vector<patchy::LayerId> shared_ids;
  std::array<double, 2> centers_x{};
  std::array<double, 2> centers_y{};
  for (const auto& candidate : std::as_const(document).layers()) {
    if (patchy::layer_is_smart_object(candidate) && patchy::smart_object_source_uuid(candidate) == old_uuid) {
      const auto placement = patchy::smart_object_placement_from_layer(candidate);
      CHECK(placement.has_value());
      CHECK(shared_ids.size() < 2U);
      centers_x[shared_ids.size()] =
          (placement->transform[0] + placement->transform[2] + placement->transform[4] + placement->transform[6]) /
          4.0;
      centers_y[shared_ids.size()] =
          (placement->transform[1] + placement->transform[3] + placement->transform[5] + placement->transform[7]) /
          4.0;
      shared_ids.push_back(candidate.id());
    }
  }
  CHECK(shared_ids.size() == 2U);

  // Author a 10x8 blue replacement png.
  ensure_artifact_dir();
  const auto replacement_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-replacement.png")))
          .absoluteFilePath();
  QImage replacement(10, 8, QImage::Format_RGBA8888);
  replacement.fill(QColor(30, 60, 220, 255));
  CHECK(replacement.save(replacement_path));

  // Qt-authored pngs carry a pHYs density chunk, and the E5 rule preserves the
  // content-inch map, so the expected quad size scales by old_dpi/new_dpi. Derive
  // the density exactly as the replace path does (the absolute rule is pinned by
  // smart_object_rescaled_placement_matches_photoshop_replace_rule in test_main).
  const double replacement_dpi = [&] {
    QFile replacement_file(replacement_path);
    CHECK(replacement_file.open(QIODevice::ReadOnly));
    const auto raw = replacement_file.readAll();
    patchy::SmartObjectSource probe;
    probe.kind = patchy::SmartObjectSourceKind::Embedded;
    probe.filename = "so-replacement.png";
    probe.filetype = "png ";
    probe.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end());
    return patchy::ui::smart_object_source_dpi(probe);
  }();
  const double expected_width = 10.0 * 72.0 / replacement_dpi;
  const double expected_height = 8.0 * 72.0 / replacement_dpi;

  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  document.set_active_layer(shared_ids.back());
  patchy::ui::MainWindowTestAccess::replace_smart_object_contents_with_path(window, replacement_path);
  QApplication::processEvents();

  // Every layer that referenced the old uuid repointed to ONE fresh element; the old
  // element is gone; names swapped the old stem for the new one; each quad kept its
  // own center at the new 10x8 content size (E5 semantics).
  CHECK(document.metadata().smart_objects.find(old_uuid) == nullptr);
  std::string new_uuid;
  for (std::size_t i = 0; i < shared_ids.size(); ++i) {
    const auto* updated = document.find_layer(shared_ids[i]);
    CHECK(updated != nullptr);
    CHECK(patchy::layer_is_smart_object(*updated));
    const auto uuid = patchy::smart_object_source_uuid(*updated);
    CHECK(!uuid.empty() && uuid != old_uuid);
    if (new_uuid.empty()) {
      new_uuid = uuid;
    }
    CHECK(uuid == new_uuid);
    CHECK(updated->name().rfind("so-replacement", 0) == 0);
    CHECK(patchy::layer_smart_object_block_dirty(*updated));
    const auto placement = patchy::smart_object_placement_from_layer(*updated);
    CHECK(placement.has_value());
    CHECK(placement->width == 10.0 && placement->height == 8.0);
    CHECK(std::abs(placement->transform[2] - placement->transform[0] - expected_width) < 1e-6);
    CHECK(std::abs(placement->transform[7] - placement->transform[1] - expected_height) < 1e-6);
    const auto center_x =
        (placement->transform[0] + placement->transform[2] + placement->transform[4] + placement->transform[6]) / 4.0;
    const auto center_y =
        (placement->transform[1] + placement->transform[3] + placement->transform[5] + placement->transform[7]) / 4.0;
    CHECK(std::abs(center_x - centers_x[i]) < 1e-9);
    CHECK(std::abs(center_y - centers_y[i]) < 1e-9);
    const auto& pixels = updated->pixels();
    const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
    CHECK(px != nullptr);
    CHECK(px[2] > 150 && px[0] < 90);  // blue replacement rendered
  }
  const auto* new_source = document.metadata().smart_objects.find(new_uuid);
  CHECK(new_source != nullptr);
  CHECK(new_source->filename == "so-replacement.png");
  CHECK(new_source->filetype == "png ");
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);
  // E4 acceptance artifact: a replaced (fresh uuid, regenerated SoLd, pruned old
  // element) document Photoshop must open and resave clean.
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts/ui_smart_object_replaced.psd"));

  // One undo restores the old element, uuids, and names.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  auto& undone_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* restored_source = undone_document.metadata().smart_objects.find(old_uuid);
  CHECK(restored_source != nullptr);
  CHECK(restored_source->file_bytes == original_bytes);
  const auto* restored_layer = undone_document.find_layer(layer_id);
  CHECK(restored_layer != nullptr);
  CHECK(restored_layer->name() == "small");
  CHECK(patchy::smart_object_source_uuid(*restored_layer) == old_uuid);
}

void ui_smart_object_nested_contents_edit_commits_up_the_chain() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto base_tab_count = tabs->count();  // a fresh window may hold an Untitled tab too
  const auto parent_tab_index = tabs->currentIndex();

  // Embed the fixture PSD itself as the contents: the smart object now contains a
  // document that carries its own smart object (nested by construction).
  const auto fixture_path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd").wstring());
  patchy::ui::MainWindowTestAccess::replace_smart_object_contents_with_path(window, fixture_path);
  QApplication::processEvents();
  auto& parent_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* replaced = parent_document.find_layer(layer_id);
  CHECK(replaced != nullptr);
  const auto* nested_source =
      parent_document.metadata().smart_objects.find(patchy::smart_object_source_uuid(*replaced));
  CHECK(nested_source != nullptr);
  CHECK(nested_source->filetype == "8BPS");

  // Open the child (the embedded PSD), then the grandchild (its embedded png).
  parent_document.set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == base_tab_count + 1);
  const auto child_tab_index = tabs->currentIndex();
  auto& child_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(child_document.width() == 96 && child_document.height() == 96);
  const patchy::Layer* child_smart = nullptr;
  for (const auto& candidate : std::as_const(child_document).layers()) {
    if (patchy::layer_is_smart_object(candidate)) {
      child_smart = &candidate;
    }
  }
  CHECK(child_smart != nullptr);
  const auto child_smart_id = child_smart->id();
  child_document.set_active_layer(child_smart_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == base_tab_count + 2);
  auto& grandchild_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(grandchild_document.width() == 32 && grandchild_document.height() == 24);

  // Commit the grandchild (fill magenta): the CHILD re-renders and marks modified.
  auto& grandchild_layer = grandchild_document.layers().front();
  grandchild_layer.set_pixels(solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(220, 20, 200, 255)));
  grandchild_layer.set_bounds(patchy::Rect{0, 0, 32, 24});
  patchy::ui::MainWindowTestAccess::canvas(window)->document_changed();
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();
  tabs->setCurrentIndex(child_tab_index);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  {
    auto& refreshed_child = patchy::ui::MainWindowTestAccess::document(window);
    const auto* refreshed_smart = refreshed_child.find_layer(child_smart_id);
    CHECK(refreshed_smart != nullptr);
    const auto& pixels = refreshed_smart->pixels();
    const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
    CHECK(px != nullptr);
    CHECK(px[0] > 150 && px[1] < 90 && px[2] > 150);  // magenta
  }

  // Commit the child: the ROOT document re-renders through the nested chain.
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();
  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  auto& root_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* root_layer = root_document.find_layer(layer_id);
  CHECK(root_layer != nullptr);
  bool found_magenta = false;
  const auto& root_pixels = root_layer->pixels();
  for (std::int32_t y = 0; y < root_pixels.height() && !found_magenta; ++y) {
    for (std::int32_t x = 0; x < root_pixels.width() && !found_magenta; ++x) {
      const auto* px = root_pixels.pixel(x, y);
      if (px != nullptr && px[3] > 200 && px[0] > 150 && px[1] < 90 && px[2] > 150) {
        found_magenta = true;
      }
    }
  }
  CHECK(found_magenta);
  // E4 acceptance artifact: PSD-in-PSD nesting Photoshop must open and resave clean.
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      root_document, std::filesystem::path("test-artifacts/ui_smart_object_nested.psd"));
}

void ui_smart_object_convert_composites_identically_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(64, 48, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("base", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(255, 255, 255, 255)));
  patchy::Layer red(built.allocate_layer_id(), "red",
                    solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(220, 30, 30, 255)));
  red.set_bounds(patchy::Rect{8, 6, 16, 12});
  built.add_layer(std::move(red));
  patchy::Layer blue(built.allocate_layer_id(), "blue",
                     solid_pixels(10, 8, patchy::PixelFormat::rgba8(), QColor(30, 60, 220, 255)));
  blue.set_bounds(patchy::Rect{20, 10, 10, 8});
  built.add_layer(std::move(blue));
  window.add_document_session(std::move(built), QStringLiteral("Convert"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto before = patchy::ui::qimage_from_document(document, true);

  // Select the two content layers (rows are top-to-bottom: blue, red, base).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() == 3);
  layer_list->clearSelection();
  layer_list->setCurrentItem(layer_list->item(0));
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();

  CHECK(document.layers().size() == 2U);
  const auto& smart = document.layers().back();
  CHECK(patchy::layer_is_smart_object(smart));
  CHECK(patchy::smart_object_lock_reason(smart).empty());
  CHECK(smart.name() == "blue");  // the topmost selected layer keeps its slot and name
  const auto placement = patchy::smart_object_placement_from_layer(smart);
  CHECK(placement.has_value());
  CHECK(placement->transform[0] == 8.0 && placement->transform[1] == 6.0);
  CHECK(placement->transform[4] == 30.0 && placement->transform[5] == 18.0);
  CHECK(placement->width == 22.0 && placement->height == 12.0);
  const auto* source = document.metadata().smart_objects.find(placement->uuid);
  CHECK(source != nullptr && source->filetype == "8BPB" && source->file_bytes != nullptr);
  const auto child = patchy::psd::DocumentIo::read({source->file_bytes->data(), source->file_bytes->size()});
  CHECK(child.width() == 22 && child.height() == 12);
  CHECK(child.layers().size() == 2U);
  const bool has_authored_sold =
      std::any_of(smart.unknown_psd_blocks().begin(), smart.unknown_psd_blocks().end(),
                  [](const patchy::UnknownPsdBlock& block) { return block.key == "SoLd"; });
  CHECK(has_authored_sold);
  const auto after = patchy::ui::qimage_from_document(document, true);
  CHECK(before == after);  // the preview composites pixel-identically

  // E4 acceptance artifact: a Patchy-AUTHORED smart object Photoshop must accept.
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts/ui_smart_object_converted.psd"));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == 3U);
}

// Masked layers away from the canvas origin: a linked mask and an unlinked one both
// land in the child document shifted exactly once, so the converted layer composites
// identically. The linked mask used to shift twice (by hand, then again inside
// translate_moved_layer_metadata), which moved it off its layer.
void ui_smart_object_convert_keeps_masks_in_place() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(96, 64, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("base", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(255, 255, 255, 255)));
  const auto add_masked = [&built](const char* name, QColor color, patchy::Rect bounds, patchy::Rect mask_bounds,
                                   bool linked) {
    patchy::Layer layer(built.allocate_layer_id(), name,
                        solid_pixels(bounds.width, bounds.height, patchy::PixelFormat::rgba8(), color));
    layer.set_bounds(bounds);
    patchy::LayerMask mask;
    mask.bounds = mask_bounds;
    mask.pixels = patchy::PixelBuffer(mask_bounds.width, mask_bounds.height, patchy::PixelFormat::gray8());
    mask.pixels.clear(255);
    mask.default_color = 0;
    layer.set_mask(std::move(mask));
    patchy::set_layer_mask_linked(layer, linked);
    built.add_layer(std::move(layer));
  };
  // The red layer shows its left half, the blue one its top half.
  add_masked("linked", QColor(220, 30, 30, 255), patchy::Rect{30, 20, 40, 30}, patchy::Rect{30, 20, 20, 30}, true);
  add_masked("unlinked", QColor(30, 60, 220, 255), patchy::Rect{60, 8, 20, 20}, patchy::Rect{60, 8, 20, 10}, false);
  window.add_document_session(std::move(built), QStringLiteral("ConvertMasked"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto before = patchy::ui::qimage_from_document(document, true);
  CHECK(before.pixelColor(35, 30).red() > 200 && before.pixelColor(35, 30).green() < 60);  // red, inside its mask
  CHECK(before.pixelColor(60, 30) == QColor(255, 255, 255, 255));                           // red, masked out

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() == 3);
  layer_list->clearSelection();
  layer_list->setCurrentItem(layer_list->item(0));
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();

  CHECK(document.layers().size() == 2U);
  const auto& smart = document.layers().back();
  CHECK(patchy::layer_is_smart_object(smart));
  const auto placement = patchy::smart_object_placement_from_layer(smart);
  CHECK(placement.has_value());
  if (!placement.has_value()) {
    return;
  }
  const auto origin_x = static_cast<int>(placement->transform[0]);
  const auto origin_y = static_cast<int>(placement->transform[1]);
  CHECK(origin_x > 0 && origin_y > 0);  // the child origin is off the canvas origin, so a second shift would show
  const auto* source = document.metadata().smart_objects.find(placement->uuid);
  CHECK(source != nullptr && source->file_bytes != nullptr);
  if (source == nullptr || source->file_bytes == nullptr) {
    return;
  }
  const auto child = patchy::psd::DocumentIo::read({source->file_bytes->data(), source->file_bytes->size()});
  CHECK(child.layers().size() == 2U);
  for (const auto& layer : child.layers()) {
    const bool linked = layer.name() == "linked";
    const auto expected_layer = linked ? patchy::Rect{30, 20, 40, 30} : patchy::Rect{60, 8, 20, 20};
    const auto expected_mask = linked ? patchy::Rect{30, 20, 20, 30} : patchy::Rect{60, 8, 20, 10};
    CHECK(layer.bounds().x == expected_layer.x - origin_x && layer.bounds().y == expected_layer.y - origin_y);
    CHECK(layer.mask().has_value());
    if (layer.mask().has_value()) {
      CHECK(layer.mask()->bounds.x == expected_mask.x - origin_x);
      CHECK(layer.mask()->bounds.y == expected_mask.y - origin_y);
    }
    CHECK(patchy::layer_mask_linked(layer) == linked);
  }
  CHECK(patchy::ui::qimage_from_document(document, true) == before);
}

// Builds base / red / blue / cover, then converts red + blue into a Smart
// Object named "blue" that sits between base and cover.
patchy::LayerId build_convert_to_layers_smart_object(patchy::ui::MainWindow& window) {
  patchy::Document built(64, 48, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("base", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(255, 255, 255, 255)));
  patchy::Layer red(built.allocate_layer_id(), "red",
                    solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(220, 30, 30, 255)));
  red.set_bounds(patchy::Rect{8, 6, 16, 12});
  built.add_layer(std::move(red));
  patchy::Layer blue(built.allocate_layer_id(), "blue",
                     solid_pixels(10, 8, patchy::PixelFormat::rgba8(), QColor(30, 60, 220, 255)));
  blue.set_bounds(patchy::Rect{20, 10, 10, 8});
  blue.set_blend_mode(patchy::BlendMode::Multiply);
  built.add_layer(std::move(blue));
  patchy::Layer cover(built.allocate_layer_id(), "cover",
                      solid_pixels(4, 4, patchy::PixelFormat::rgba8(), QColor(20, 180, 60, 255)));
  cover.set_bounds(patchy::Rect{56, 40, 4, 4});
  built.add_layer(std::move(cover));
  window.add_document_session(std::move(built), QStringLiteral("ConvertToLayers"));

  // Rows are top-to-bottom: cover, blue, red, base.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() == 4);
  layer_list->clearSelection();
  layer_list->setCurrentItem(layer_list->item(1));
  layer_list->item(1)->setSelected(true);
  layer_list->item(2)->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.layers().size() == 3U);
  CHECK(patchy::layer_is_smart_object(document.layers()[1]));
  return document.layers()[1].id();
}

// Layer > Smart Objects > Convert to Layers (GitHub issue 35): the contents'
// layers come back as a folder in the Smart Object's slot, at their exact
// positions and with their own names and blend modes, compositing identically,
// in one undo step.
void ui_smart_object_convert_to_layers_restores_layers_in_place() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto smart_id = build_convert_to_layers_smart_object(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.active_layer_id() == smart_id);
  const auto before = patchy::ui::qimage_from_document(document, true);
  const auto source_uuid = patchy::smart_object_source_uuid(*document.find_layer(smart_id));
  CHECK(document.metadata().smart_objects.find(source_uuid) != nullptr);

  auto* action = require_action(window, "layerSmartObjectToLayersAction");
  CHECK(action->text() == QStringLiteral("Convert to Layers"));
  action->trigger();
  QApplication::processEvents();

  CHECK(document.layers().size() == 3U);
  CHECK(document.layers()[0].name() == "base");
  CHECK(document.layers()[2].name() == "cover");
  const auto& folder = document.layers()[1];
  CHECK(folder.kind() == patchy::LayerKind::Group);
  CHECK(folder.name() == "blue");
  CHECK(folder.blend_mode() == patchy::BlendMode::Normal);  // isolated, like the Smart Object
  CHECK(document.active_layer_id() == folder.id());
  CHECK(folder.children().size() == 2U);
  const auto& red = folder.children()[0];
  const auto& blue = folder.children()[1];
  CHECK(red.name() == "red");
  CHECK(blue.name() == "blue");  // no "copy" suffix although the Smart Object had the name
  const auto same_rect = [](patchy::Rect a, patchy::Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  };
  CHECK(same_rect(red.bounds(), patchy::Rect{8, 6, 16, 12}));
  CHECK(same_rect(blue.bounds(), patchy::Rect{20, 10, 10, 8}));
  CHECK(blue.blend_mode() == patchy::BlendMode::Multiply);
  CHECK(!patchy::layer_is_smart_object(red) && !patchy::layer_is_smart_object(blue));
  const auto after = patchy::ui::qimage_from_document(document, true);
  CHECK(before == after);
  // The orphaned source stays in the store, like Rasterize leaves it.
  CHECK(document.metadata().smart_objects.find(source_uuid) != nullptr);

  // The layered result must open in Photoshop like any other Patchy PSD, so the
  // writer leaves the unreferenced element (and its emptied lnk2) out.
  ensure_artifact_dir();
  const std::filesystem::path artifact("test-artifacts/ui_smart_object_converted_to_layers.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact);
  {
    std::ifstream stream(artifact, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(patchy::psd::DocumentIo::read({bytes.data(), bytes.size()}).metadata().smart_objects.empty());
  }

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == 3U);
  CHECK(patchy::layer_is_smart_object(document.layers()[1]));
  CHECK(document.metadata().smart_objects.find(source_uuid) != nullptr);

  // Not a Smart Object: refused without touching the document.
  auto& base = document.layers()[0];
  document.set_active_layer(base.id());
  action->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == 3U);
  CHECK(patchy::layer_is_smart_object(document.layers()[1]));
}

// Rasterize and Delete keep the now-unreferenced source in the store (Photoshop
// keeps orphans too, and Undo brings the layer back), but the PSD writer leaves
// a Patchy-authored element nothing references out of the file: Photoshop 2026
// refuses to open such a file ("program error"; docs/smart-objects.md).
void ui_smart_object_orphaned_source_is_not_written() {
  ensure_artifact_dir();
  for (const auto* action_name : {"layerSmartObjectToNormalAction", "layerDeleteAction"}) {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto smart_id = build_convert_to_layers_smart_object(window);
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto source_uuid = patchy::smart_object_source_uuid(*document.find_layer(smart_id));
    require_action(window, action_name)->trigger();
    QApplication::processEvents();
    const auto* remaining = document.find_layer(smart_id);
    CHECK(remaining == nullptr || !patchy::layer_is_smart_object(*remaining));
    CHECK(document.metadata().smart_objects.find(source_uuid) != nullptr);

    const auto artifact = std::filesystem::path("test-artifacts") /
                          (std::string("ui_smart_object_orphan_") +
                           (std::string_view(action_name) == "layerDeleteAction" ? "deleted" : "rasterized") + ".psd");
    patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact);
    std::ifstream stream(artifact, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(!bytes.empty());
    const auto reread = patchy::psd::DocumentIo::read({bytes.data(), bytes.size()});
    CHECK(reread.metadata().smart_objects.find(source_uuid) == nullptr);
    CHECK(reread.metadata().smart_objects.empty());  // no empty lnk2 block either
    CHECK(document.metadata().smart_objects.find(source_uuid) != nullptr);  // writing never mutates

    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    const auto* restored = document.find_layer(smart_id);
    CHECK(restored != nullptr && patchy::layer_is_smart_object(*restored));
  }
}

// A scaled placement maps the unpacked layers through the same transform the
// preview renders through.
void ui_smart_object_convert_to_layers_follows_scaled_placement() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto smart_id = build_convert_to_layers_smart_object(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  {
    auto* smart = document.find_layer(smart_id);
    CHECK(smart != nullptr);
    auto placement = patchy::smart_object_placement_from_layer(*smart);
    CHECK(placement.has_value());
    // The 22 x 12 contents at twice their size, still anchored at (8, 6).
    placement->transform = {8.0, 6.0, 52.0, 6.0, 52.0, 30.0, 8.0, 30.0};
    patchy::store_smart_object_placement(*smart, *placement);
    patchy::mark_layer_smart_object_block_dirty(*smart);
    CHECK(patchy::ui::refresh_smart_object_layer_preview(
        document, *smart, patchy::ui::CanvasWidget::TransformInterpolation::Bicubic, false));
  }
  const auto before = patchy::ui::qimage_from_document(document, true);

  require_action(window, "layerSmartObjectToLayersAction")->trigger();
  QApplication::processEvents();

  const auto& folder = document.layers()[1];
  CHECK(folder.kind() == patchy::LayerKind::Group);
  CHECK(folder.children().size() == 2U);
  const auto rect_near = [](patchy::Rect actual, patchy::Rect expected) {
    return std::abs(actual.x - expected.x) <= 1 && std::abs(actual.y - expected.y) <= 1 &&
           std::abs(actual.width - expected.width) <= 2 && std::abs(actual.height - expected.height) <= 2;
  };
  // Child (0, 0, 16, 12) and (12, 4, 10, 8) through scale 2 about the origin, then (8, 6).
  CHECK(rect_near(folder.children()[0].bounds(), patchy::Rect{8, 6, 32, 24}));
  CHECK(rect_near(folder.children()[1].bounds(), patchy::Rect{32, 14, 20, 16}));
  const auto after = patchy::ui::qimage_from_document(document, true);
  CHECK(color_close(after.pixelColor(16, 12), before.pixelColor(16, 12), 6));   // red only
  CHECK(color_close(after.pixelColor(36, 22), before.pixelColor(36, 22), 6));   // blue multiplied over red
  CHECK(color_close(after.pixelColor(48, 28), before.pixelColor(48, 28), 6));   // blue only
  CHECK(color_close(after.pixelColor(4, 40), QColor(255, 255, 255), 2));        // untouched base
}

void ui_smart_object_edit_commit_keeps_canvas_transparency() {
  // The July 2026 "transparent parts turn black" repro: convert a shape on a
  // transparent canvas to a smart object, edit the contents, save. The commit decodes
  // the freshly serialized child PSB preferring its stored composite; the pre-fix
  // writer matted that composite onto black (3 channels, no alpha), so every
  // transparent pixel baked into the parent preview as opaque black.
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(64, 48, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Background",
                        solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(255, 255, 255, 255)));
  patchy::PixelBuffer shape_pixels(64, 48, patchy::PixelFormat::rgba8());
  shape_pixels.clear(0);
  for (std::int32_t y = 6; y < 18; ++y) {
    for (std::int32_t x = 8; x < 24; ++x) {
      auto* px = shape_pixels.pixel(x, y);
      px[0] = 220;
      px[1] = 30;
      px[2] = 30;
      px[3] = 255;
    }
  }
  patchy::Layer shape(built.allocate_layer_id(), "Paint Layer", std::move(shape_pixels));
  shape.set_bounds(patchy::Rect{0, 0, 64, 48});
  built.add_layer(std::move(shape));
  window.add_document_session(std::move(built), QStringLiteral("Transparent SO"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto parent_tab_index = tabs->currentIndex();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() == 2);
  layer_list->clearSelection();
  layer_list->setCurrentItem(layer_list->item(0));  // the shape row (topmost)
  layer_list->item(0)->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == 2U);
  const auto layer_id = document.layers().back().id();
  CHECK(patchy::layer_is_smart_object(std::as_const(document).layers().back()));

  const auto alpha_at_document_point = [&](const patchy::Layer& layer, std::int32_t doc_x,
                                           std::int32_t doc_y) -> int {
    const auto bounds = layer.bounds();
    const auto* px = layer.pixels().pixel(doc_x - bounds.x, doc_y - bounds.y);
    CHECK(px != nullptr);
    return px[3];
  };
  CHECK(alpha_at_document_point(*std::as_const(document).find_layer(layer_id), 2, 2) == 0);

  // Edit the contents (recolor the shape, keeping its transparent surround) and
  // commit with Save; any content edit triggers the decode-and-re-render.
  document.set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  auto& child_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(!child_document.layers().empty());
  auto& child_layer = child_document.layers().front();
  patchy::PixelBuffer recolored(child_document.width(), child_document.height(), patchy::PixelFormat::rgba8());
  recolored.clear(0);
  for (std::int32_t y = 6; y < 18 && y < recolored.height(); ++y) {
    for (std::int32_t x = 8; x < 24 && x < recolored.width(); ++x) {
      auto* px = recolored.pixel(x, y);
      px[0] = 20;
      px[1] = 200;
      px[2] = 40;
      px[3] = 255;
    }
  }
  child_layer.set_pixels(std::move(recolored));
  child_layer.set_bounds(patchy::Rect{0, 0, child_document.width(), child_document.height()});
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();

  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  auto& parent_after = patchy::ui::MainWindowTestAccess::document(window);
  const auto* committed = std::as_const(parent_after).find_layer(layer_id);
  CHECK(committed != nullptr);
  // THE regression: the transparent surround must stay transparent (it baked to
  // opaque black before the fix)...
  CHECK(alpha_at_document_point(*committed, 2, 2) == 0);
  CHECK(alpha_at_document_point(*committed, 60, 44) == 0);
  // ...while the recolored shape re-rendered opaque.
  const auto bounds = committed->bounds();
  const auto* shape_px = committed->pixels().pixel(12 - bounds.x, 10 - bounds.y);
  CHECK(shape_px != nullptr);
  CHECK(shape_px[3] == 255);
  CHECK(shape_px[0] < 60 && shape_px[1] > 150 && shape_px[2] < 80);
  // The white Background still shows through the surround in the composite.
  const auto composite = patchy::ui::qimage_from_document(parent_after, true);
  CHECK(composite.pixelColor(2, 2).red() > 240 && composite.pixelColor(2, 2).green() > 240);

  // E4-style acceptance artifact: Photoshop must open and resave this cleanly (the
  // child PSB now carries a 4-channel "Transparency" composite).
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      parent_after, std::filesystem::path("test-artifacts/ui_smart_object_transparent_commit.psd"));
}

std::vector<std::uint8_t> read_smart_object_fixture_bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

void ui_smart_object_legacy_black_composite_decodes_transparent() {
  // Contents saved by the pre-fix writer (the committed fixture: 3-channel merged
  // composite matted onto black, layered data with real alpha) must decode through
  // the layered-render fallback instead of trusting the opaque composite.
  const auto path = patchy::test::committed_psd_fixture_path("patchy-legacy-black-composite.psb");
  CHECK(std::filesystem::exists(path));
  auto bytes = read_smart_object_fixture_bytes(path);
  CHECK(!bytes.empty());

  patchy::SmartObjectSource source;
  source.kind = patchy::SmartObjectSourceKind::Embedded;
  source.filename = "patchy-legacy-black-composite.psb";
  source.filetype = "8BPB";
  source.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));

  const auto image = patchy::ui::decode_smart_object_source_image(source);
  CHECK(image.has_value());
  CHECK(image->width() == 8 && image->height() == 6);
  CHECK(image->pixelColor(0, 0).alpha() == 0);    // transparent canvas corner, was opaque black
  CHECK(image->pixelColor(3, 2).alpha() == 255);  // the opaque block
  CHECK(image->pixelColor(3, 2).red() > 200);
  CHECK(std::abs(image->pixelColor(5, 3).alpha() - 128) <= 1);  // semi pixel keeps its coverage
}

void ui_smart_object_psbtest_repro_decodes_transparent_if_available() {
  // Seth's actual July 2026 repro file: a Convert-to-Smart-Object child saved by the
  // pre-fix writer whose composite turned the parent's transparent parts black.
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/Paint Layer.psb");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] PSBtest/Paint Layer.psb not present\n";
    return;
  }
  auto bytes = read_smart_object_fixture_bytes(path);
  CHECK(!bytes.empty());

  patchy::SmartObjectSource source;
  source.kind = patchy::SmartObjectSourceKind::Embedded;
  source.filename = "Paint Layer.psb";
  source.filetype = "8BPB";
  source.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));

  const auto image = patchy::ui::decode_smart_object_source_image(source);
  CHECK(image.has_value());
  bool any_transparent = false;
  bool any_opaque = false;
  for (int y = 0; y < image->height(); ++y) {
    for (int x = 0; x < image->width(); ++x) {
      const auto alpha = image->pixelColor(x, y).alpha();
      any_transparent = any_transparent || alpha == 0;
      any_opaque = any_opaque || alpha == 255;
    }
  }
  CHECK(any_transparent);  // the painted shape's surround, black before the fix
  CHECK(any_opaque);       // the shape itself
}

void ui_smart_object_place_embedded_centers_and_fits() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(100, 80, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("base", solid_pixels(100, 80, patchy::PixelFormat::rgba8(), QColor(255, 255, 255, 255)));
  built.print_settings().horizontal_ppi = 72.0;
  built.print_settings().vertical_ppi = 72.0;
  window.add_document_session(std::move(built), QStringLiteral("Place"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  ensure_artifact_dir();
  const auto small_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-place-small.png")))
          .absoluteFilePath();
  QImage small_image(20, 10, QImage::Format_RGBA8888);
  small_image.fill(QColor(20, 200, 40, 255));
  small_image.setDotsPerMeterX(2835);  // ~72 dpi, so physical size == pixel size
  small_image.setDotsPerMeterY(2835);
  CHECK(small_image.save(small_path));
  patchy::ui::MainWindowTestAccess::place_embedded_file_with_path(window, small_path);
  QApplication::processEvents();
  CHECK(document.layers().size() == 2U);
  const auto placement = patchy::smart_object_placement_from_layer(document.layers().back());
  CHECK(placement.has_value());
  // Smaller than the canvas: placed at its physical size, centered (the png carries
  // its own density, so allow sub-pixel slack around the nominal 20x10).
  const auto center_x = (placement->transform[0] + placement->transform[4]) / 2.0;
  const auto center_y = (placement->transform[1] + placement->transform[5]) / 2.0;
  CHECK(std::abs(center_x - 50.0) < 0.01 && std::abs(center_y - 40.0) < 0.01);
  CHECK(std::abs(placement->transform[4] - placement->transform[0] - 20.0) < 1.0);
  CHECK(placement->width == 20.0 && placement->height == 10.0);
  CHECK(document.metadata().smart_objects.find(placement->uuid) != nullptr);

  const auto large_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-place-large.png")))
          .absoluteFilePath();
  QImage large_image(200, 160, QImage::Format_RGBA8888);
  large_image.fill(QColor(220, 30, 30, 255));
  large_image.setDotsPerMeterX(2835);
  large_image.setDotsPerMeterY(2835);
  CHECK(large_image.save(large_path));
  patchy::ui::MainWindowTestAccess::place_embedded_file_with_path(window, large_path);
  QApplication::processEvents();
  CHECK(document.layers().size() == 3U);
  const auto fitted = patchy::smart_object_placement_from_layer(document.layers().back());
  CHECK(fitted.has_value());
  // Larger than the canvas: scaled down to fit, centered (200x160 into 100x80 = 0.5).
  CHECK(std::abs(fitted->transform[0] - 0.0) < 0.5 && std::abs(fitted->transform[1] - 0.0) < 0.5);
  CHECK(std::abs(fitted->transform[4] - 100.0) < 0.5 && std::abs(fitted->transform[5] - 80.0) < 0.5);
  CHECK(fitted->width == 200.0 && fitted->height == 160.0);
  // E4 acceptance artifact: Patchy-AUTHORED placed smart objects PS must accept.
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts/ui_smart_object_placed.psd"));
}

void ui_smart_object_via_copy_diverges_and_transform_rerenders() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto parent_tab_index = tabs->currentIndex();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto original_uuid = patchy::smart_object_source_uuid(*document.find_layer(layer_id));

  // New Smart Object via Copy: the element clones under a FRESH uuid (E8), so the
  // copy stops tracking the original's contents.
  require_action(window, "layerSmartObjectViaCopyAction")->trigger();
  QApplication::processEvents();
  const patchy::Layer* copy_layer = nullptr;
  for (const auto& candidate : std::as_const(document).layers()) {
    if (patchy::layer_is_smart_object(candidate) && candidate.id() != layer_id) {
      copy_layer = &candidate;
    }
  }
  CHECK(copy_layer != nullptr);
  const auto copy_id = copy_layer->id();
  const auto copy_uuid = patchy::smart_object_source_uuid(*copy_layer);
  CHECK(!copy_uuid.empty() && copy_uuid != original_uuid);
  const auto* original_source = document.metadata().smart_objects.find(original_uuid);
  const auto* copy_source = document.metadata().smart_objects.find(copy_uuid);
  CHECK(original_source != nullptr && copy_source != nullptr);
  CHECK(copy_source->file_bytes != nullptr && *copy_source->file_bytes == *original_source->file_bytes);

  // Editing the ORIGINAL's contents leaves the via-copy layer untouched.
  document.set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  auto& child_document = patchy::ui::MainWindowTestAccess::document(window);
  auto& child_layer = child_document.layers().front();
  // Magenta: a color the fixture's own artwork cannot plausibly contain.
  child_layer.set_pixels(solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(220, 20, 200, 255)));
  child_layer.set_bounds(patchy::Rect{0, 0, 32, 24});
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();
  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  auto& parent_after = patchy::ui::MainWindowTestAccess::document(window);
  const auto& original_pixels = parent_after.find_layer(layer_id)->pixels();
  const auto* original_px = original_pixels.pixel(original_pixels.width() / 2, original_pixels.height() / 2);
  CHECK(original_px != nullptr);
  CHECK(original_px[0] > 150 && original_px[1] < 90 && original_px[2] > 150);  // re-rendered magenta
  const auto& copy_pixels = parent_after.find_layer(copy_id)->pixels();
  const auto* copy_px = copy_pixels.pixel(copy_pixels.width() / 2, copy_pixels.height() / 2);
  CHECK(copy_px != nullptr);
  CHECK(!(copy_px[0] > 150 && copy_px[1] < 90 && copy_px[2] > 150));  // the copy did NOT change

  // Free transform re-renders through the quad: double the copy's size about its
  // reference and check the placement + pixels followed crisply. Select through the
  // LIST so the canvas selection follows (document set_active_layer alone doesn't).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* copy_item = require_layer_item(*layer_list, QStringLiteral("small copy"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(copy_item);
  copy_item->setSelected(true);
  QApplication::processEvents();
  CHECK(parent_after.active_layer_id() == copy_id);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto before_placement = patchy::smart_object_placement_from_layer(*parent_after.find_layer(copy_id));
  CHECK(before_placement.has_value());
  CHECK(canvas->begin_free_transform());
  const auto controls = canvas->transform_controls_state();
  CHECK(controls.has_value());
  CHECK(canvas->set_transform_controls_state(controls->reference_position, 200.0, 200.0, 0.0));
  QApplication::processEvents();
  canvas->finish_free_transform();
  QApplication::processEvents();
  const auto* transformed = parent_after.find_layer(copy_id);
  CHECK(transformed != nullptr);
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
  // The re-rendered pixels track the doubled quad.
  CHECK(std::abs(static_cast<double>(transformed->bounds().width) - after_width) < 2.0);
}

void ui_smart_object_image_size_scales_placement_and_rerenders() {
  // Photoshop resizes a document containing smart objects without asking for a
  // rasterize first, so Patchy does too: the placement quad rides the resize and the
  // preview comes back from the full-resolution source instead of staying the
  // bilinear-scaled pixels the resize left behind.
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto before =
      patchy::smart_object_placement_from_layer(*std::as_const(document).find_layer(layer_id));
  CHECK(before.has_value());
  const auto old_width = document.width();
  const auto old_height = document.height();

  accept_image_size_dialog(old_width * 2, old_height * 2);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("Rasterize")));
  CHECK(document.width() == old_width * 2 && document.height() == old_height * 2);

  const auto* resized = std::as_const(document).find_layer(layer_id);
  CHECK(resized != nullptr);
  const auto after = patchy::smart_object_placement_from_layer(*resized);
  CHECK(after.has_value());
  for (std::size_t i = 0; i < 8U; ++i) {
    CHECK(std::abs(after->transform[i] - before->transform[i] * 2.0) < 1e-9);
  }
  // The CONTENT is untouched: only where it lands moved, so the source stays the same
  // pixel size and density and the layer stays non-destructive.
  CHECK(after->width == before->width && after->height == before->height);
  CHECK(after->resolution == before->resolution);
  CHECK(after->uuid == before->uuid);
  CHECK(patchy::layer_smart_object_block_dirty(*resized));
  const auto& metadata = std::as_const(*resized).metadata();
  const auto raster_status = metadata.find(patchy::kLayerMetadataSmartObjectRasterStatus);
  CHECK(raster_status != metadata.end() &&
        raster_status->second == patchy::kSmartObjectRasterStatusPatchy);

  // Byte-compare against a direct render of the embedded source through the scaled
  // quad: that is what "re-rendered, not resampled" means.
  const auto* source = std::as_const(document).metadata().smart_objects.find(after->uuid);
  CHECK(source != nullptr);
  const auto source_image = patchy::ui::decode_smart_object_source_image(*source);
  CHECK(source_image.has_value());
  const auto expected = patchy::ui::render_smart_object_pixels(*source_image, *after,
                                                               canvas->transform_interpolation());
  CHECK(expected.has_value());
  const auto bounds = resized->bounds();
  CHECK(bounds.x == expected->bounds.x && bounds.y == expected->bounds.y);
  CHECK(bounds.width == expected->bounds.width && bounds.height == expected->bounds.height);
  const auto& pixels = std::as_const(*resized).pixels();
  bool pixels_match = pixels.format().channels == 4 && pixels.width() == expected->image.width() &&
                      pixels.height() == expected->image.height();
  for (std::int32_t y = 0; pixels_match && y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* actual = pixels.pixel(x, y);
      const auto sample = expected->image.pixelColor(x, y);
      if (actual[0] != sample.red() || actual[1] != sample.green() || actual[2] != sample.blue() ||
          actual[3] != sample.alpha()) {
        pixels_match = false;
        break;
      }
    }
  }
  CHECK(pixels_match);

  // Canvas Size is the same gate: the quad translates by the anchor offset.
  const auto quad_before_canvas = after->transform;
  accept_canvas_size_dialog(document.width() + 40, document.height() + 20);
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("Rasterize")));
  const auto shifted =
      patchy::smart_object_placement_from_layer(*std::as_const(document).find_layer(layer_id));
  CHECK(shifted.has_value());
  for (std::size_t i = 0; i < 8U; i += 2U) {
    CHECK(std::abs(shifted->transform[i] - (quad_before_canvas[i] + 20.0)) < 1e-9);
    CHECK(std::abs(shifted->transform[i + 1U] - (quad_before_canvas[i + 1U] + 10.0)) < 1e-9);
  }

  // The regenerated SoLd has to carry the new quad, or a resave puts the placed
  // content back where it was before the resize.
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(std::as_const(document));
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const patchy::Layer* reread_layer = nullptr;
  for (const auto& candidate : std::as_const(reread).layers()) {
    if (patchy::layer_is_smart_object(candidate)) {
      reread_layer = &candidate;
    }
  }
  CHECK(reread_layer != nullptr);
  const auto saved = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(saved.has_value());
  for (std::size_t i = 0; i < 8U; ++i) {
    CHECK(std::abs(saved->transform[i] - shifted->transform[i]) < 1e-6);
  }
}

void ui_smart_object_external_edit_from_disk_saves_and_refreshes_parent() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto parent_tab_index = tabs->currentIndex();
  auto& parent_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto uuid = patchy::smart_object_source_uuid(*parent_document.find_layer(layer_id));

  // Synthesize a LINKED smart object: write the embedded png to disk, then convert
  // the source to an ExternalFile reference pointing at it.
  ensure_artifact_dir();
  const auto linked_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-linked.png")))
          .absoluteFilePath();
  {
    auto* source = parent_document.metadata().smart_objects.find(uuid);
    CHECK(source != nullptr && source->file_bytes != nullptr);
    QFile out(linked_path);
    CHECK(out.open(QIODevice::WriteOnly));
    out.write(reinterpret_cast<const char*>(source->file_bytes->data()),
              static_cast<qint64>(source->file_bytes->size()));
    out.close();
    source->kind = patchy::SmartObjectSourceKind::ExternalFile;
    source->file_bytes = nullptr;
    source->original_element_bytes = nullptr;
    source->external_original_path = QDir::toNativeSeparators(linked_path).toStdString();
    source->filename = "so-linked.png";
    auto* layer = parent_document.find_layer(layer_id);
    CHECK(layer != nullptr);
    layer->metadata()[patchy::kLayerMetadataSmartObjectLock] = "external";
  }
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // Edit Contents opens the real file from disk as a linked child tab.
  parent_document.set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  auto& child_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(child_document.width() == 32 && child_document.height() == 24);

  // Edit and save: the png on disk changes AND the parent preview re-renders.
  auto& child_layer = child_document.layers().front();
  child_layer.set_pixels(solid_pixels(child_document.width(), child_document.height(),
                                      patchy::PixelFormat::rgba8(), QColor(220, 20, 200, 255)));
  child_layer.set_bounds(patchy::Rect{0, 0, child_document.width(), child_document.height()});
  patchy::ui::MainWindowTestAccess::canvas(window)->document_changed();
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();

  const QImage saved_on_disk(linked_path);
  CHECK(!saved_on_disk.isNull());
  const auto disk_color = saved_on_disk.pixelColor(saved_on_disk.width() / 2, saved_on_disk.height() / 2);
  CHECK(disk_color.red() > 150 && disk_color.green() < 90 && disk_color.blue() > 150);

  const auto renamed_path = patchy::ui::to_qstring(std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      (patchy::test::unicode_path_piece(patchy::test::kUnicodePathStems[0]) += ".psd")));
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.unattended = true;
  options.args = {QStringLiteral("target=") + renamed_path};
  CHECK(host.run_source(QStringLiteral("if (!app.activeDocument.saveAs(patchy.args.target)) throw Error('save failed');"),
                        std::move(options)));
  CHECK(process_events_until([&] { return !host.run_active(); }, 5000));
  CHECK(!host.last_run_had_error());
  CHECK(QFileInfo::exists(renamed_path));

  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  auto& parent_after = patchy::ui::MainWindowTestAccess::document(window);
  const auto* refreshed = parent_after.find_layer(layer_id);
  CHECK(refreshed != nullptr);
  const auto& pixels = refreshed->pixels();
  const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
  CHECK(px != nullptr);
  CHECK(px[0] > 150 && px[1] < 90 && px[2] > 150);  // magenta re-render
  CHECK(patchy::smart_object_lock_reason(*refreshed) == "external");  // still linked
  const auto* source_after = parent_after.metadata().smart_objects.find(uuid);
  CHECK(source_after != nullptr);
  CHECK(source_after->kind == patchy::SmartObjectSourceKind::ExternalFile);
  CHECK(source_after->dirty);
  CHECK(source_after->filename == QFileInfo(renamed_path).fileName().toStdString());
  CHECK(source_after->filetype == "8BPS");
  CHECK(QDir::fromNativeSeparators(QString::fromStdString(source_after->external_original_path)) ==
        QDir::fromNativeSeparators(renamed_path));
  CHECK(source_after->external_file_size > 0U);
  CHECK(source_after->external_mod_year >= 2026);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 2);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_smart_object_update_content_rereads_linked_file() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  ensure_artifact_dir();
  const auto linked_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-update.png")))
          .absoluteFilePath();
  convert_fixture_source_to_external(window, layer_id, linked_path);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // Someone else edits the linked file on disk...
  QImage replacement(32, 24, QImage::Format_RGBA8888);
  replacement.fill(QColor(220, 20, 200, 255));
  CHECK(replacement.save(linked_path));

  // ...and Update Smart Object Content pulls it in.
  document.set_active_layer(layer_id);
  QApplication::processEvents();
  require_action(window, "layerSmartObjectUpdateAction")->trigger();
  QApplication::processEvents();

  const auto* refreshed = document.find_layer(layer_id);
  CHECK(refreshed != nullptr);
  const auto& pixels = refreshed->pixels();
  const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
  CHECK(px != nullptr);
  CHECK(px[0] > 150 && px[1] < 90 && px[2] > 150);  // magenta from disk
  const auto uuid = patchy::smart_object_source_uuid(*refreshed);
  const auto* source = document.metadata().smart_objects.find(uuid);
  CHECK(source != nullptr && source->dirty);
  CHECK(source->external_file_size == static_cast<std::uint64_t>(QFileInfo(linked_path).size()));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);
}

void ui_smart_object_stale_linked_file_noticed_on_open() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  ensure_artifact_dir();
  const auto linked_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-stale.png")))
          .absoluteFilePath();
  convert_fixture_source_to_external(window, layer_id, linked_path);

  // Save the parent with the freshly stamped link data (exercises the liFE writer),
  // then change the linked file so the stored size no longer matches.
  const auto parent_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-stale-parent.psd")))
          .absoluteFilePath();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      patchy::ui::MainWindowTestAccess::document(window), patchy::ui::to_filesystem_path(parent_path));
  QImage bigger(64, 48, QImage::Format_RGBA8888);
  bigger.fill(QColor(20, 200, 40, 255));
  CHECK(bigger.save(linked_path));

  patchy::ui::MainWindowTestAccess::open_document_path(window, parent_path);
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("changed on disk")));
}

void ui_smart_object_relink_and_embed_linked_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  ensure_artifact_dir();
  const auto original_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-relink-a.png")))
          .absoluteFilePath();
  convert_fixture_source_to_external(window, layer_id, original_path);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  // PS's stem-rename rule applies when the layer name tracks the source name.
  document.find_layer(layer_id)->set_name("so-relink-a");
  const auto uuid = patchy::smart_object_source_uuid(*document.find_layer(layer_id));
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // Relink to a different-size file: paths rewrite, the quad rescales (E5 rule), the
  // element uuid stays, and the layer re-renders from the new target.
  const auto relink_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("so-relink-b.png")))
          .absoluteFilePath();
  QImage replacement(10, 8, QImage::Format_RGBA8888);
  replacement.fill(QColor(30, 60, 220, 255));
  replacement.setDotsPerMeterX(2835);
  replacement.setDotsPerMeterY(2835);
  CHECK(replacement.save(relink_path));
  document.set_active_layer(layer_id);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::relink_smart_object_contents_with_path(window, relink_path);
  QApplication::processEvents();

  const auto* relinked = document.find_layer(layer_id);
  CHECK(relinked != nullptr);
  const auto relinked_uuid = patchy::smart_object_source_uuid(*relinked);
  CHECK(!relinked_uuid.empty() && relinked_uuid != uuid);  // PS assigns a fresh uuid (E14)
  CHECK(document.metadata().smart_objects.find(uuid) == nullptr);  // old element pruned
  CHECK(relinked->name().rfind("so-relink-b", 0) == 0);            // stem renamed
  CHECK(patchy::smart_object_lock_reason(*relinked) == "external");
  const auto placement = patchy::smart_object_placement_from_layer(*relinked);
  CHECK(placement.has_value());
  CHECK(placement->width == 10.0 && placement->height == 8.0);
  const auto* source = document.metadata().smart_objects.find(relinked_uuid);
  CHECK(source != nullptr && source->dirty);
  CHECK(source->filename == "so-relink-b.png");
  CHECK(source->external_rel_path.find("so-relink-b.png") != std::string::npos);
  CHECK(source->external_file_size == static_cast<std::uint64_t>(QFileInfo(relink_path).size()));
  const auto& pixels = relinked->pixels();
  const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
  CHECK(px != nullptr && px[2] > 150 && px[0] < 90);  // blue from the new target
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);
  // E4 acceptance artifact: a Patchy-authored linked (liFE) element PS must resolve.
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts/ui_smart_object_relinked.psd"));

  // Embed Linked pulls the bytes in under ANOTHER fresh uuid (E13): kind flips, the
  // lock clears, and Edit Contents then opens an embedded child tab.
  require_action(window, "layerSmartObjectEmbedAction")->trigger();
  QApplication::processEvents();
  const auto embedded_uuid = patchy::smart_object_source_uuid(*document.find_layer(layer_id));
  CHECK(!embedded_uuid.empty() && embedded_uuid != relinked_uuid);
  const auto* embedded_source = document.metadata().smart_objects.find(embedded_uuid);
  CHECK(embedded_source != nullptr);
  CHECK(embedded_source->kind == patchy::SmartObjectSourceKind::Embedded);
  CHECK(embedded_source->file_bytes != nullptr && !embedded_source->file_bytes->empty());
  const auto* unlocked = document.find_layer(layer_id);
  CHECK(unlocked != nullptr);
  CHECK(patchy::smart_object_lock_reason(*unlocked).empty());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 2);

  // E4 acceptance artifact: the embed-linked output PS must open and resave clean.
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts/ui_smart_object_embedded.psd"));

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();
  document.set_active_layer(layer_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == tab_count_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
}

// --- Place Linked and the smart-object script API -------------------------------

// A fresh folder for one linked-placement test (the links point at files inside it).
QString linked_test_dir(const QString& leaf) {
  ensure_artifact_dir();
  const auto dir =
      QFileInfo(QStringLiteral("test-artifacts/so-linked")).absoluteFilePath() + QLatin1Char('/') + leaf;
  QDir(dir).removeRecursively();
  CHECK(QDir().mkpath(dir));
  return dir;
}

void write_linked_test_file(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  CHECK(file.write(bytes) == bytes.size());
}

// A 64 x 32 SVG: a filled circle on a transparent canvas. `padding` changes the byte
// size, which is how a rewrite within the same second still reads as changed.
QByteArray linked_test_svg(const char* fill, const char* padding = "") {
  return QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"32\" viewBox=\"0 0 64 32\">"
                        "<circle cx=\"32\" cy=\"16\" r=\"12\" fill=\"%1\"/></svg>%2\n")
      .arg(QLatin1String(fill), QLatin1String(padding))
      .toUtf8();
}

QString write_linked_test_png(const QString& path, QColor color, int width = 40, int height = 20) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  image.setDotsPerMeterX(2835);  // ~72 dpi, so physical size == pixel size
  image.setDotsPerMeterY(2835);
  CHECK(image.save(path));
  return path;
}

void add_linked_test_document(patchy::ui::MainWindow& window, int width, int height) {
  patchy::Document built(width, height, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("base", solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(255, 255, 255, 255)));
  built.print_settings().horizontal_ppi = 72.0;
  built.print_settings().vertical_ppi = 72.0;
  window.add_document_session(std::move(built), QStringLiteral("Linked"));
}

// A path as a JavaScript string literal (JSON quoting keeps every character).
QString js_string(const QString& text) {
  return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
}

bool run_smart_object_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("smart-object-test");
  options.unattended = true;
  (void)host.run_source(source, std::move(options));
  CHECK(process_events_until([&] { return !host.run_active(); }, 30000));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  return !host.last_run_had_error();
}

bool smart_object_backlog_contains(patchy::ui::MainWindow& window, const QString& needle) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(needle)) {
      return true;
    }
  }
  return false;
}

std::vector<const patchy::SmartObjectSource*> linked_sources(const patchy::Document& document) {
  std::vector<const patchy::SmartObjectSource*> sources;
  for (const auto& block : document.metadata().smart_objects.blocks) {
    for (const auto& source : block.sources) {
      if (source.kind == patchy::SmartObjectSourceKind::ExternalFile) {
        sources.push_back(&source);
      }
    }
  }
  return sources;
}

std::vector<const patchy::Layer*> linked_layers(const patchy::Document& document) {
  std::vector<const patchy::Layer*> layers;
  for (const auto& layer : document.layers()) {
    if (patchy::layer_is_smart_object(layer) && patchy::smart_object_lock_reason(layer) == "external") {
      layers.push_back(&layer);
    }
  }
  return layers;
}

bool layer_has_block(const patchy::Layer& layer, std::string_view key) {
  const auto& blocks = layer.unknown_psd_blocks();
  return std::any_of(blocks.begin(), blocks.end(),
                     [key](const patchy::UnknownPsdBlock& block) { return block.key == key; });
}

// The opaque pixel in the middle of a layer's buffer, as a QColor.
QColor layer_center_color(const patchy::Layer& layer) {
  const auto& pixels = layer.pixels();
  const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
  CHECK(px != nullptr);
  return QColor(px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : 255);
}

// File > Place Linked: the layer references the file instead of holding a copy, the
// link is stamped the way Photoshop stamps it, a second placement of the same file
// shares the element, and the relative path is computed when the document is saved.
void ui_smart_object_place_linked_links_the_file() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* action = require_action(window, "filePlaceLinkedAction");
  CHECK(action->menuRole() == QAction::NoRole);
  CHECK(window.hotkey_registry().find_command(QStringLiteral("file.place_linked")) != nullptr);

  add_linked_test_document(window, 200, 160);
  QApplication::processEvents();
  CHECK(action->isEnabled());
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto dir = linked_test_dir(QStringLiteral("place"));
  const auto art_path = write_linked_test_png(dir + QStringLiteral("/art.png"), QColor(20, 200, 40, 255));
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  patchy::ui::MainWindowTestAccess::place_linked_file_with_path(window, art_path);
  QApplication::processEvents();
  CHECK(std::as_const(document).layers().size() == 2U);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("linked smart object")));
  {
    const auto& placed = std::as_const(document).layers().back();
    CHECK(placed.name() == "art");
    CHECK(patchy::layer_is_smart_object(placed));
    CHECK(patchy::smart_object_lock_reason(placed) == "external");
    CHECK(layer_has_block(placed, "SoLE"));
    CHECK(!layer_has_block(placed, "SoLd"));
    CHECK(std::as_const(document).active_layer_id() == placed.id());
    const auto placement = patchy::smart_object_placement_from_layer(placed);
    CHECK(placement.has_value());
    CHECK(placement->width == 40.0 && placement->height == 20.0);
    CHECK(placement->placed_type == 2);
    // Physical size, centered on the 200 x 160 canvas.
    CHECK(std::abs((placement->transform[0] + placement->transform[4]) / 2.0 - 100.0) < 0.01);
    CHECK(std::abs((placement->transform[1] + placement->transform[5]) / 2.0 - 80.0) < 0.01);
    CHECK(std::abs(placement->transform[4] - placement->transform[0] - 40.0) < 1.0);
    const auto color = layer_center_color(placed);
    CHECK(color.green() > 150 && color.red() < 90);

    const auto sources = linked_sources(document);
    CHECK(sources.size() == 1U);
    const auto& source = *sources.front();
    CHECK(source.uuid == placement->uuid);
    CHECK(source.file_bytes == nullptr);  // a reference, never a copy
    CHECK(source.filename == "art.png");
    CHECK(source.filetype == "png ");
    CHECK(source.dirty);
    // The document has no folder yet: the bare name stands in until the first save.
    CHECK(source.external_rel_path == "art.png");
    CHECK(QString::fromStdString(source.external_original_path) == QDir::toNativeSeparators(art_path));
    CHECK(QString::fromStdString(source.external_full_path) ==
          QStringLiteral("file://") + (art_path.startsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) +
              art_path);
    // Photoshop's stamp: UTC, whole seconds, plus the byte size.
    const auto modified = QFileInfo(art_path).lastModified().toUTC();
    CHECK(source.external_mod_year == modified.date().year());
    CHECK(source.external_mod_day == modified.date().day());
    CHECK(source.external_mod_hour == modified.time().hour());
    CHECK(source.external_mod_minute == modified.time().minute());
    CHECK(source.external_mod_seconds == static_cast<double>(modified.time().second()));
    CHECK(source.external_file_size == static_cast<std::uint64_t>(QFileInfo(art_path).size()));
    CHECK(!patchy::ui::smart_object_link_changed_on_disk(source, QFileInfo(art_path)));
  }

  // The same file again: a second layer on the SAME element, its own placed instance.
  patchy::ui::MainWindowTestAccess::place_linked_file_with_path(window, art_path);
  QApplication::processEvents();
  {
    const auto layers = linked_layers(document);
    CHECK(layers.size() == 2U);
    CHECK(linked_sources(document).size() == 1U);
    CHECK(patchy::smart_object_source_uuid(*layers[0]) == patchy::smart_object_source_uuid(*layers[1]));
    CHECK(patchy::smart_object_placed_uuid(*layers[0]) != patchy::smart_object_placed_uuid(*layers[1]));
  }
  // Undo takes the second layer away again, redo-free check of the single step.
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(linked_layers(document).size() == 1U);
  patchy::ui::MainWindowTestAccess::place_linked_file_with_path(window, art_path);
  QApplication::processEvents();
  CHECK(linked_layers(document).size() == 2U);

  // Saving one folder down computes the relative path against the document's folder.
  const auto psd_dir = dir + QStringLiteral("/psd");
  CHECK(QDir().mkpath(psd_dir));
  const auto psd_path = psd_dir + QStringLiteral("/board.psd");
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd_path));
  CHECK(linked_sources(document).front()->external_rel_path == "../art.png");

  // Reopened from disk: two linked layers on one element, resolved and unchanged.
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  QApplication::processEvents();
  const auto& reopened = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(QFileInfo(patchy::ui::MainWindowTestAccess::active_session_path(window)) == QFileInfo(psd_path));
  const auto reopened_layers = linked_layers(reopened);
  CHECK(reopened_layers.size() == 2U);
  const auto reopened_sources = linked_sources(reopened);
  CHECK(reopened_sources.size() == 1U);
  CHECK(reopened_sources.front()->external_rel_path == "../art.png");
  CHECK(reopened_sources.front()->filename == "art.png");
  for (const auto* layer : reopened_layers) {
    CHECK(layer_has_block(*layer, "SoLE"));
    CHECK(patchy::smart_object_source_uuid(*layer) == reopened_sources.front()->uuid);
  }
  const auto resolved = patchy::ui::resolve_smart_object_external_path(*reopened_sources.front(), psd_dir);
  CHECK(resolved.has_value() && QFileInfo(*resolved) == QFileInfo(art_path));
  const auto message = window.statusBar()->currentMessage();
  CHECK(!message.contains(QStringLiteral("changed on disk")));
  CHECK(!message.contains(QStringLiteral("not found")));
}

// An SVG is vector contents: Photoshop's 'SVG ' filetype and Type 1 placement for
// both linked and embedded placements, and a render at the placement's own scale
// instead of a resampled natural-size raster.
// Editing a linked SVG smart object's file (Edit Contents on the linked
// layer): the child is a shape-only document, so Save writes the linked file
// straight back as vectors, with no flatten warning and no Save As redirect,
// and the parent layer re-renders from the rewritten file.
void ui_smart_object_linked_svg_child_saves_vectors_without_warning() {
  patchy::ui::MainWindow window;
  show_window(window);
  add_linked_test_document(window, 300, 200);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto parent_tab_index = tabs->currentIndex();
  const auto dir = linked_test_dir(QStringLiteral("svg-child"));
  const auto svg_path = dir + QStringLiteral("/mark.svg");
  write_linked_test_file(svg_path, linked_test_svg("#ff0000"));
  patchy::ui::MainWindowTestAccess::place_linked_file_with_path(window, svg_path);
  QApplication::processEvents();
  auto& parent_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(std::as_const(parent_document).layers().size() == 2U);
  const auto layer_id = std::as_const(parent_document).layers().back().id();
  parent_document.set_active_layer(layer_id);

  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  CHECK(QFileInfo(patchy::ui::MainWindowTestAccess::active_session_path(window)) == QFileInfo(svg_path));
  auto& child_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(child_document.layers().size() == 1U);
  CHECK(patchy::svg::DocumentIo::baked_content(std::as_const(child_document)).empty());

  // Recolor the circle in the child, then plain Save.
  {
    auto& circle = child_document.layers().front();
    CHECK(patchy::layer_is_vector_shape(circle));
    auto content = *circle.vector_shape();
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{0, 0, 255};
    circle.set_vector_shape(std::move(content));
  }
  patchy::ui::MainWindowTestAccess::canvas(window)->document_changed();
  bool flatten_prompt = false;
  bool save_as_dialog = false;
  QTimer::singleShot(0, [&flatten_prompt, &save_as_dialog] {
    if (auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("flattenLayersMessageBox")))) {
      flatten_prompt = true;
      box->button(QMessageBox::Cancel)->click();
    }
    if (auto* dialog = find_top_level_dialog(QStringLiteral("saveAsFileDialog"))) {
      save_as_dialog = true;
      dialog->reject();
    }
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();
  CHECK(!flatten_prompt);
  CHECK(!save_as_dialog);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  {
    QFile file(svg_path);
    CHECK(file.open(QIODevice::ReadOnly));
    const auto text = QString::fromUtf8(file.readAll());
    CHECK(text.contains(QStringLiteral("#0000ff")));
    CHECK(!text.contains(QStringLiteral("<image")));
  }

  // The parent re-rendered its placement from the rewritten file.
  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  auto& parent_after = patchy::ui::MainWindowTestAccess::document(window);
  const auto* refreshed = std::as_const(parent_after).find_layer(layer_id);
  CHECK(refreshed != nullptr);
  const auto& pixels = std::as_const(*refreshed).pixels();
  const auto* px = pixels.pixel(pixels.width() / 2, pixels.height() / 2);
  CHECK(px != nullptr);
  CHECK(px[0] < 60 && px[1] < 60 && px[2] > 200);
  CHECK(patchy::smart_object_lock_reason(*refreshed) == "external");
}

void ui_smart_object_placed_svg_is_vector_contents() {
  patchy::ui::MainWindow window;
  show_window(window);
  add_linked_test_document(window, 300, 200);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto dir = linked_test_dir(QStringLiteral("svg"));
  const auto svg_path = dir + QStringLiteral("/mark.svg");
  write_linked_test_file(svg_path, linked_test_svg("#ff0000"));

  patchy::ui::MainWindowTestAccess::place_linked_file_with_path(window, svg_path);
  QApplication::processEvents();
  CHECK(std::as_const(document).layers().size() == 2U);
  {
    const auto& placed = std::as_const(document).layers().back();
    CHECK(patchy::smart_object_lock_reason(placed) == "external");
    const auto placement = patchy::smart_object_placement_from_layer(placed);
    CHECK(placement.has_value());
    CHECK(placement->placed_type == 1);
    CHECK(placement->width == 64.0 && placement->height == 32.0 && placement->resolution == 72.0);
    const std::array<double, 8> centered{118.0, 84.0, 182.0, 84.0, 182.0, 116.0, 118.0, 116.0};
    CHECK(placement->transform == centered);
    const auto sources = linked_sources(document);
    CHECK(sources.size() == 1U && sources.front()->filetype == "SVG ");
    // The authored block: Type 1, no compInfo, warp bounds = the unscaled placement
    // rectangle in document space (Photoshop's vector shape).
    std::vector<std::uint8_t> payload;
    for (const auto& block : placed.unknown_psd_blocks()) {
      if (block.key == "SoLE") {
        payload = block.payload;
      }
    }
    CHECK(!payload.empty());
    patchy::psd::BigEndianReader reader(payload);
    (void)patchy::psd::read_signature(reader);
    (void)reader.read_u32();
    (void)reader.read_u32();
    const auto descriptor = patchy::psd::read_descriptor(reader);
    CHECK(patchy::psd::descriptor_number(descriptor, "Type") == 1.0);
    CHECK(patchy::psd::descriptor_value(descriptor, "compInfo") == nullptr);
    const auto* warp = patchy::psd::descriptor_object(descriptor, "warp");
    CHECK(warp != nullptr);
    const auto* bounds = patchy::psd::descriptor_object(*warp, "bounds");
    CHECK(bounds != nullptr);
    CHECK(patchy::psd::descriptor_number(*bounds, "Left") == 118.0 &&
          patchy::psd::descriptor_number(*bounds, "Top ") == 84.0 &&
          patchy::psd::descriptor_number(*bounds, "Rght") == 182.0 &&
          patchy::psd::descriptor_number(*bounds, "Btom") == 116.0);
    CHECK(layer_center_color(placed).red() > 200);
  }

  // Embedded: the same vector shape, with the bytes inside the document.
  patchy::ui::MainWindowTestAccess::place_embedded_file_with_path(window, svg_path);
  QApplication::processEvents();
  {
    const auto& embedded = std::as_const(document).layers().back();
    CHECK(patchy::smart_object_lock_reason(embedded).empty());
    CHECK(layer_has_block(embedded, "SoLd"));
    const auto placement = patchy::smart_object_placement_from_layer(embedded);
    CHECK(placement.has_value() && placement->placed_type == 1);
    const auto* source = std::as_const(document).metadata().smart_objects.find(placement->uuid);
    CHECK(source != nullptr);
    CHECK(source->kind == patchy::SmartObjectSourceKind::Embedded);
    CHECK(source->filetype == "SVG ");
    CHECK(source->file_bytes != nullptr && !source->file_bytes->empty());
  }

  // Four times the natural size on whole pixels: the layer is the vector render at
  // 256 x 128, not the 64 x 32 raster stretched.
  CHECK(run_smart_object_script(window, QStringLiteral("var l = app.activeDocument.addSmartObject(%1, "
                                                       "{linked: true, x: 10, y: 20, width: 256, name: 'big'});")
                                            .arg(js_string(svg_path))));
  const auto* big = [&]() -> const patchy::Layer* {
    for (const auto& layer : std::as_const(document).layers()) {
      if (layer.name() == "big") {
        return &layer;
      }
    }
    return nullptr;
  }();
  CHECK(big != nullptr);
  const auto big_placement = patchy::smart_object_placement_from_layer(*big);
  CHECK(big_placement.has_value());
  const std::array<double, 8> big_quad{10.0, 20.0, 266.0, 20.0, 266.0, 148.0, 10.0, 148.0};
  CHECK(big_placement->transform == big_quad);
  CHECK(big_placement->width == 64.0 && big_placement->height == 32.0);
  const auto probe = patchy::ui::load_smart_object_file_probe(svg_path);
  CHECK(probe.has_value());
  CHECK(patchy::ui::smart_object_contents_are_vector(*probe));
  const auto vector_image = patchy::ui::render_smart_object_vector_contents(*probe, *big_placement);
  CHECK(vector_image.has_value());
  CHECK(vector_image->width() == 256 && vector_image->height() == 128);
  const auto natural = patchy::ui::decode_smart_object_source_image(*probe);
  CHECK(natural.has_value() && natural->width() == 64 && natural->height() == 32);
  const auto stretched = natural->scaled(256, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const auto bounds = big->bounds();
  const auto& pixels = big->pixels();
  CHECK(pixels.format().channels == 4);
  std::int64_t vector_error = 0;
  std::int64_t stretched_error = 0;
  for (int y = 0; y < bounds.height; ++y) {
    for (int x = 0; x < bounds.width; ++x) {
      const auto* px = pixels.pixel(x, y);
      const int source_x = bounds.x + x - 10;
      const int source_y = bounds.y + y - 20;
      CHECK(source_x >= 0 && source_x < 256 && source_y >= 0 && source_y < 128);
      vector_error += std::abs(static_cast<int>(px[3]) - vector_image->pixelColor(source_x, source_y).alpha());
      stretched_error += std::abs(static_cast<int>(px[3]) - stretched.pixelColor(source_x, source_y).alpha());
    }
  }
  // Pixel-aligned placement: the vector render lands as it is; the stretched raster
  // has a soft edge all the way round the circle.
  CHECK(vector_error <= static_cast<std::int64_t>(bounds.width) * bounds.height / 50);
  CHECK(stretched_error > vector_error * 10 + 1000);

  // A PNG is raster contents: no vector pass.
  const auto png_path = write_linked_test_png(dir + QStringLiteral("/flat.png"), QColor(1, 2, 3, 255));
  const auto png_probe = patchy::ui::load_smart_object_file_probe(png_path);
  CHECK(png_probe.has_value());
  CHECK(!patchy::ui::smart_object_contents_are_vector(*png_probe));
  CHECK(!patchy::ui::render_smart_object_vector_contents(*png_probe, *big_placement).has_value());
}


// Exercise the wrapper target with an inactive layer and an inactive document,
// two shared instances, masks, physical density and the normal script Undo group.
void ui_script_smart_object_replace_contents_shared_unicode_and_ppi() {
  using patchy::ui::MainWindowTestAccess;
  const auto dir = patchy::test::unicode_artifact_dir(u8"script-replace");
  const auto original_path = patchy::ui::to_qstring(dir / "original.png");
  auto replacement_file = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem);
  replacement_file += ".png";
  const auto replacement_path = patchy::ui::to_qstring(replacement_file);
  const auto same_rect = [](const patchy::Rect& a, const patchy::Rect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  };
  QImage image(8, 6, QImage::Format_RGBA8888);
  image.fill(QColor(220, 20, 30, 255));
  image.setDotsPerMeterX(23622);  // PNG's integer pHYs encoding of 600 PPI
  image.setDotsPerMeterY(23622);
  CHECK(image.save(original_path));
  for (const int replacement_dpm : {23622, 3780}) {  // 600 and 96 PPI
    patchy::ui::MainWindow window;
    show_window_empty(window);
    CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
      var d = app.newDocument(96, 96);
      var a = d.addSmartObject(%1, {x: 20, y: 20, width: 8, name: 'FOTO'});
      a.duplicate();
      d.addLayer('untouched').fill('#123456');
    )JS").arg(js_string(original_path))));
    auto& document = MainWindowTestAccess::document(window);
    // newDocument supplies a background layer, so locate the two placed layers.
    std::vector<patchy::LayerId> ids;
    for (const auto& layer : std::as_const(document).layers()) {
      if (patchy::layer_is_smart_object(layer)) {
        ids.push_back(layer.id());
      }
    }
    CHECK(ids.size() == 2U);
    const auto old_uuid = patchy::smart_object_source_uuid(*std::as_const(document).find_layer(ids[0]));
    const auto old_placement = patchy::smart_object_placement_from_layer(*std::as_const(document).find_layer(ids[0]));
    CHECK(old_placement.has_value());
    CHECK(std::abs(old_placement->resolution - 600.0) < 0.02);
    patchy::PixelBuffer mask_pixels(3, 2, patchy::PixelFormat::gray8());
    mask_pixels.clear(173);
    document.find_layer(ids[0])->set_mask(
        patchy::LayerMask{patchy::Rect{21, 22, 3, 2}, std::move(mask_pixels), 31, false, 201, 0.5});
    patchy::LayerVectorMask vector_mask;
    vector_mask.disabled = true;
    vector_mask.unlinked = true;
    vector_mask.density = 199;
    document.find_layer(ids[0])->set_vector_mask(vector_mask);
    patchy::SmartObjectWarp warp;
    warp.style = "warpCustom";
    warp.bounds_right = 8;
    warp.bounds_bottom = 6;
    warp.u_order = 2;
    warp.v_order = 2;
    warp.mesh_xs = {0, 8, 0, 8};
    warp.mesh_ys = {0, 0, 6, 6};
    document.find_layer(ids[0])->metadata()[patchy::kLayerMetadataSmartObjectWarp] =
        patchy::serialize_smart_object_warp(warp);
    patchy::SmartFilterStack filters;
    filters.support = patchy::SmartFilterStackSupport::Supported;
    // Match the UI-authored filter mask; linked masks are not renderable.
    filters.mask.linked = false;
    patchy::SmartFilterEntry blur;
    blur.kind = patchy::SmartFilterKind::GaussianBlur;
    blur.native_name = "Gaussian Blur";
    blur.native_class_id = "GsnB";
    blur.native_filter_id = 0x47736E42U;
    blur.parameters = patchy::GaussianBlurSmartFilter{0.5};
    filters.entries.push_back(blur);
    document.find_layer(ids[1])->set_smart_filter_stack(filters);
    // Diagnose the same phases as refresh_smart_object_layer_preview separately.
    const auto& fixture_layer = *std::as_const(document).find_layer(ids[1]);
    const auto* fixture_source = std::as_const(document).metadata().smart_objects.find(
        patchy::smart_object_source_uuid(fixture_layer));
    CHECK(fixture_source != nullptr);
    CHECK(patchy::ui::decode_smart_object_source_image(*fixture_source).has_value());
    CHECK(patchy::smart_object_lock_reason(fixture_layer).empty());
    const auto fixture_unfiltered = patchy::ui::render_smart_object_unfiltered_layer_preview(
        std::as_const(document), fixture_layer,
        patchy::ui::CanvasWidget::TransformInterpolation::Bicubic);
    CHECK(fixture_unfiltered.has_value());
    patchy::ui::SmartObjectLayerPreview fixture_preview;
    fixture_preview.unfiltered = *fixture_unfiltered;
    CHECK(!filters.mask.linked);
    try {
      fixture_preview.rendered = patchy::render_smart_filter_stack(
          fixture_unfiltered->pixels, fixture_unfiltered->bounds,
          patchy::Rect::from_size(document.width(), document.height()), filters);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string("Smart Filter fixture application: ") + error.what());
    }
    const auto fixture_record = patchy::psd::author_filter_effects_record(
        patchy::smart_object_placed_uuid(fixture_layer),
        patchy::Rect::from_size(document.width(), document.height()),
        fixture_unfiltered->pixels, fixture_unfiltered->bounds, filters.mask);
    CHECK(fixture_record.has_value());
    auto fixture_cache = std::as_const(document).metadata().smart_filter_effects;
    CHECK(fixture_cache.upsert_authored(*fixture_record));
    CHECK(patchy::ui::install_smart_object_layer_preview(
        document, *document.find_layer(ids[1]), std::move(fixture_preview)));
    const auto old_placed = patchy::smart_object_placed_uuid(*std::as_const(document).find_layer(ids[1]));
    CHECK(std::as_const(document).metadata().smart_filter_effects.find_unique(old_placed) != nullptr);
    const auto before = document;
    const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
    image.fill(QColor(20, 30, 220, 255));
    image.setDotsPerMeterX(replacement_dpm);
    image.setDotsPerMeterY(replacement_dpm);
    CHECK(image.save(replacement_path));
    const auto probe = patchy::ui::load_smart_object_file_probe(replacement_path);
    CHECK(probe.has_value());
    const auto dpi = patchy::ui::smart_object_source_dpi(*probe);
    CHECK(std::abs(dpi - (replacement_dpm == 23622 ? 600.0 : 96.0)) < 0.02);
    // Keep another document active, with colliding layer ids.
    CHECK(run_smart_object_script(window, "app.newDocument(12,12).addLayer('other').fill('#abcdef');"));
    const auto other_before = patchy::psd::DocumentIo::write_layered_rgb8(
        std::as_const(MainWindowTestAccess::document(window)));
    const auto other_undo = MainWindowTestAccess::active_session_undo_depth(window);
    const auto session_id = MainWindowTestAccess::session_id(window, 0);
    CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
      var d = app.getDocument('%1');
      var selected = app.activeDocument.id, activeLayer = d.activeLayer.id;
      var n = d.getLayer('%2').replaceSmartObjectContents(%3);
      if (n !== 2) throw new Error('shared count ' + n);
      if (app.activeDocument.id !== selected || d.activeLayer.id !== activeLayer)
        throw new Error('selection changed');
    )JS").arg(QString::number(session_id), QString::number(ids[0]), js_string(replacement_path))));
    CHECK(patchy::psd::DocumentIo::write_layered_rgb8(
              std::as_const(MainWindowTestAccess::document(window))) == other_before);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == other_undo);
    CHECK(std::as_const(document).metadata().smart_objects.find(old_uuid) == nullptr);
    const auto* filtered = std::as_const(document).find_layer(ids[1]);
    CHECK(filtered->smart_filter_stack() == before.find_layer(ids[1])->smart_filter_stack());
    CHECK(std::as_const(document).metadata().smart_filter_effects.find_unique(old_placed) == nullptr);
    CHECK(std::as_const(document).metadata().smart_filter_effects.find_unique(
              patchy::smart_object_placed_uuid(*filtered)) != nullptr);
    std::string new_uuid;
    for (const auto id : ids) {
      const auto* layer = std::as_const(document).find_layer(id);
      CHECK(layer != nullptr);
      const auto uuid = patchy::smart_object_source_uuid(*layer);
      CHECK(uuid != old_uuid);
      if (new_uuid.empty()) { new_uuid = uuid; }
      CHECK(uuid == new_uuid);
      const auto placement = patchy::smart_object_placement_from_layer(*layer);
      CHECK(placement.has_value());
      CHECK(placement->width == 8.0 && placement->height == 6.0);
      CHECK(std::abs(placement->resolution - dpi) < 1e-9);
      const auto old = patchy::smart_object_placement_from_layer(*before.find_layer(id));
      CHECK(old.has_value());
      const auto expected = patchy::rescaled_smart_object_placement(*old, 8, 6, dpi);
      for (std::size_t i = 0; i < 8; ++i) {
        CHECK(std::abs(placement->transform[i] - expected.transform[i]) < 1e-8);
      }
      // Pin the physical rule independently as well as checking all quad corners.
      const auto factor = old->resolution / dpi;
      CHECK(std::abs(factor - (replacement_dpm == 23622 ? 1.0 : 6.25)) < 0.001);
      CHECK(std::abs((placement->transform[2] - placement->transform[0]) -
                     (old->transform[2] - old->transform[0]) * factor) < 1e-8);
      CHECK(layer_center_color(*layer).blue() > 200);
    }
    const auto* updated = std::as_const(document).find_layer(ids[0]);
    CHECK(updated->name() == "FOTO");
    const auto* source = std::as_const(document).metadata().smart_objects.find(new_uuid);
    CHECK(source != nullptr && source->kind == patchy::SmartObjectSourceKind::Embedded);
    CHECK(QString::fromStdString(source->filename) == QFileInfo(replacement_path).fileName());
    CHECK(source->file_bytes != nullptr && source->dirty);
    CHECK(source->creator == "    ");
    CHECK(updated->mask().has_value());
    const auto& mask = *updated->mask();
    const auto& old_mask = *before.find_layer(ids[0])->mask();
    CHECK(same_rect(mask.bounds, old_mask.bounds) && mask.default_color == old_mask.default_color);
    CHECK(mask.disabled == old_mask.disabled && mask.density == old_mask.density);
    CHECK(mask.feather == old_mask.feather && same_rect(mask.feather_canvas, old_mask.feather_canvas));
    CHECK(mask.pixels.width() == old_mask.pixels.width() && mask.pixels.height() == old_mask.pixels.height());
    for (int y = 0; y < mask.pixels.height(); ++y) {
      CHECK(std::equal(mask.pixels.row(y).begin(), mask.pixels.row(y).end(), old_mask.pixels.row(y).begin()));
    }
    CHECK(updated->vector_mask() == before.find_layer(ids[0])->vector_mask());
    CHECK(updated->metadata().at(patchy::kLayerMetadataSmartObjectWarp) ==
          before.find_layer(ids[0])->metadata().at(patchy::kLayerMetadataSmartObjectWarp));
    CHECK(document.layers().size() == before.layers().size());
    for (const auto& layer : before.layers()) {
      if (patchy::layer_is_smart_object(layer)) { continue; }
      const auto* unchanged = std::as_const(document).find_layer(layer.id());
      CHECK(unchanged != nullptr);
      CHECK(unchanged->name() == layer.name() && same_rect(unchanged->bounds(), layer.bounds()));
      CHECK(unchanged->metadata() == layer.metadata());
      CHECK(unchanged->content_revision() == layer.content_revision());
    }
    MainWindowTestAccess::activate_session(window, 0);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
    MainWindowTestAccess::undo(window);
    CHECK(patchy::psd::DocumentIo::write_layered_rgb8(std::as_const(document)) ==
          patchy::psd::DocumentIo::write_layered_rgb8(before));
  }
}

void ui_script_smart_object_replace_contents_errors_are_atomic() {
  using patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto dir = linked_test_dir(QStringLiteral("script-replace-errors"));
  const auto source = write_linked_test_png(dir + "/source.png", QColor(230, 10, 20, 255), 8, 6);
  const auto corrupt = dir + "/corrupt.png";
  write_linked_test_file(corrupt, QByteArray("not an image"));
  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(32,32);
    d.addSmartObject(%1, {name:'embedded', width:8});
    d.addSmartObject(%1, {name:'linked', linked:true, width:8});
    d.addLayer('plain').fill('#123456');
  )JS").arg(js_string(source))));
  auto& document = MainWindowTestAccess::document(window);
  const auto snapshot = patchy::psd::DocumentIo::write_layered_rgb8(std::as_const(document));
  const auto undo = MainWindowTestAccess::active_session_undo_depth(window);
  const auto redo = MainWindowTestAccess::active_session_redo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  for (const auto& scenario : std::vector<std::pair<QString, QString>>{
           {"embedded", dir + "/missing.png"}, {"embedded", corrupt},
           {"plain", source}, {"linked", source}}) {
    CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
      var d = app.activeDocument, target;
      for (var i=0;i<d.layers.length;i++) if(d.layers[i].name === %1) target=d.layers[i];
      var caught=false;
      try { target.replaceSmartObjectContents(%2); } catch(e) { caught=true; }
      if (!caught) throw new Error('replacement did not throw');
    )JS").arg(js_string(scenario.first), js_string(scenario.second))));
    CHECK(patchy::psd::DocumentIo::write_layered_rgb8(std::as_const(document)) == snapshot);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo);
    CHECK(MainWindowTestAccess::active_session_redo_depth(window) == redo);
    CHECK(MainWindowTestAccess::active_session_is_modified(window) == modified);
  }


  CHECK(run_smart_object_script(window, R"JS(
    var d=app.activeDocument;
    for(var i=0;i<d.layers.length;i++)
      if(d.layers[i].name==='embedded') { d.layers[i].duplicate(); break; }
  )JS"));
  std::vector<patchy::LayerId> shared;
  for (const auto& layer : std::as_const(document).layers()) {
    const auto* so = std::as_const(document).metadata().smart_objects.find(patchy::smart_object_source_uuid(layer));
    if (so != nullptr && so->kind == patchy::SmartObjectSourceKind::Embedded) {
      shared.push_back(layer.id());
    }
  }
  CHECK(shared.size() == 2U);
  document.find_layer(shared.back())->metadata()[patchy::kLayerMetadataSmartObjectLock] = "filters";
  const auto blocked_snapshot = patchy::psd::DocumentIo::write_layered_rgb8(std::as_const(document));
  const auto blocked_undo = MainWindowTestAccess::active_session_undo_depth(window);
  const auto blocked_redo = MainWindowTestAccess::active_session_redo_depth(window);
  // First targets an editable instance, then the protected instance directly.
  for (const auto id : shared) {
    CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
      var caught=false;
      try { app.activeDocument.getLayer('%1').replaceSmartObjectContents(%2); }
      catch(e) { caught=true; }
      if(!caught) throw new Error('protected instance did not throw');
    )JS").arg(QString::number(id), js_string(source))));
    CHECK(patchy::psd::DocumentIo::write_layered_rgb8(std::as_const(document)) == blocked_snapshot);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == blocked_undo);
    CHECK(MainWindowTestAccess::active_session_redo_depth(window) == blocked_redo);
  }

  // Invalid wrappers must fail at the JS boundary without entering mutation.
  for (const bool closed_session : {false, true}) {
    auto& host = window.script_engine_host();
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.unattended = true;
    CHECK(host.run_source(QStringLiteral(R"JS(
      setTimeout(function() {
        var caught=false;
        try { stale.replaceSmartObjectContents(%1); } catch(e) { caught=true; }
        if (!caught) throw new Error('stale wrapper did not throw');
      }, 0);
    )JS").arg(js_string(source)), options));
    CHECK(host.run_active());
    const auto session_id = MainWindowTestAccess::session_id(window, 0);
    auto* wrapper = new patchy::ui::ScriptLayerObject(
        host, closed_session ? -1 : session_id, closed_session ? std::as_const(document).layers().front().id() : 999999);
    host.engine()->globalObject().setProperty(QStringLiteral("stale"), host.engine()->newQObject(wrapper));
    CHECK(process_events_until([&] { return !host.run_active(); }, 30000));
    CHECK(!host.last_run_had_error());
    CHECK(patchy::psd::DocumentIo::write_layered_rgb8(std::as_const(document)) == blocked_snapshot);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == blocked_undo);
    CHECK(MainWindowTestAccess::active_session_redo_depth(window) == blocked_redo);
  }
}

// The acceptance scenario, scripted: a new document, one SVG placed linked three
// times at different sizes plus a text layer, saved next to the SVG, reopened, and
// read back as three linked layers on one source. Changing the SVG on disk and
// running one update changes all three renders.
void ui_script_smart_object_linked_round_trip_and_update() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto dir = linked_test_dir(QStringLiteral("script-round-trip"));
  const auto svg_path = dir + QStringLiteral("/logo.svg");
  const auto psd_path = dir + QStringLiteral("/board.psd");
  write_linked_test_file(svg_path, linked_test_svg("#ff0000"));

  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var svg = %1, psd = %2;
    var doc = app.newDocument(600, 400);
    var natural = 64 * doc.resolution / 72;
    var a = doc.addSmartObject(svg, {linked: true, x: 10, y: 10, width: 128});
    var b = doc.addSmartObject(svg, {linked: true, x: 200, y: 40, scale: 0.5});
    var c = doc.addSmartObject(svg, {linked: true, x: 450, y: 300, height: 16, name: 'small mark'});
    var t = doc.addTextLayer('Patchy', {size: 24, x: 20, y: 300});
    function info(layer) {
      var so = layer.getSmartObject();
      if (so === null) throw new Error(layer.name + ' is not a smart object');
      return so;
    }
    function near(actual, expected, what) {
      if (Math.abs(actual - expected) > 0.001) throw new Error(what + ': ' + actual + ' != ' + expected);
    }
    if (!a.isSmartObject || !b.isSmartObject || !c.isSmartObject) throw new Error('isSmartObject');
    if (t.isSmartObject || t.getSmartObject() !== null) throw new Error('text layer reads as a smart object');
    if (a.name !== 'logo' || c.name !== 'small mark') throw new Error('names ' + a.name + ', ' + c.name);
    if (doc.activeLayer.id !== t.id) throw new Error('active layer');
    var ia = info(a), ib = info(b), ic = info(c);
    if (!ia.linked || !ib.linked || !ic.linked) throw new Error('not linked');
    if (ia.sourceId === '' || ia.sourceId !== ib.sourceId || ia.sourceId !== ic.sourceId) throw new Error('sources differ');
    if (ia.fileName !== 'logo.svg' || ia.width !== 64 || ia.height !== 32 || ia.resolution !== 72) throw new Error('contents');
    if (ia.missing || ia.changed) throw new Error('fresh link flagged');
    if (ia.path !== svg) throw new Error('path ' + ia.path);
    if (ia.relativePath !== 'logo.svg') throw new Error('unsaved relativePath ' + ia.relativePath);
    near(ia.quad[0], 10, 'a.x'); near(ia.quad[1], 10, 'a.y'); near(ia.quad[2] - ia.quad[0], 128, 'a.width');
    near(ia.quad[7] - ia.quad[1], 64, 'a.height');
    near(ib.quad[0], 200, 'b.x'); near(ib.quad[2] - ib.quad[0], natural * 0.5, 'b.width');
    near(ic.quad[7] - ic.quad[1], 16, 'c.height'); near(ic.quad[2] - ic.quad[0], 32, 'c.width');
    if (!doc.saveAs(psd)) throw new Error('save failed');
    if (info(a).relativePath !== 'logo.svg') throw new Error('saved relativePath ' + info(a).relativePath);
    doc.close();

    var re = app.open(psd);
    var linked = [];
    for (var i = 0; i < re.layers.length; i++) {
      var layer = re.layers[i];
      if (layer.isSmartObject && layer.getSmartObject().linked) linked.push(layer);
    }
    if (linked.length !== 3) throw new Error('linked layers after reopen: ' + linked.length);
    var first = info(linked[0]);
    for (var j = 0; j < linked.length; j++) {
      var so = info(linked[j]);
      if (so.sourceId !== first.sourceId) throw new Error('reopened sources differ');
      if (so.missing || so.changed) throw new Error('reopened link flagged');
      if (so.fileName !== 'logo.svg' || so.relativePath !== 'logo.svg' || so.path !== svg) throw new Error('reopened link ' + so.path);
    }
    var texts = 0;
    for (var k = 0; k < re.layers.length; k++) { if (re.layers[k].isText) texts++; }
    if (texts !== 1) throw new Error('text layers after reopen: ' + texts);
    console.log('round-trip-ok');
  )JS")
                                            .arg(js_string(svg_path), js_string(psd_path))));
  CHECK(smart_object_backlog_contains(window, QStringLiteral("round-trip-ok")));
  CHECK(QFileInfo::exists(psd_path));

  // The reopened document is active. Its three layers are red circles.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(QFileInfo(patchy::ui::MainWindowTestAccess::active_session_path(window)) == QFileInfo(psd_path));
  {
    const auto layers = linked_layers(document);
    CHECK(layers.size() == 3U);
    for (const auto* layer : layers) {
      const auto color = layer_center_color(*layer);
      CHECK(color.red() > 200 && color.blue() < 60 && color.alpha() == 255);
    }
    CHECK(linked_sources(document).size() == 1U);
  }

  // Someone edits the SVG. One update call refreshes all three layers in one undo step.
  write_linked_test_file(svg_path, linked_test_svg("#0000ff", "<!-- edited -->"));
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var linked = [];
    for (var i = 0; i < doc.layers.length; i++) {
      if (doc.layers[i].isSmartObject) linked.push(doc.layers[i]);
    }
    if (linked.length !== 3) throw new Error('linked layers: ' + linked.length);
    if (!linked[0].getSmartObject().changed) throw new Error('the edit was not noticed');
    var refreshed = linked[1].updateSmartObject();
    if (refreshed !== 3) throw new Error('refreshed ' + refreshed);
    for (var j = 0; j < linked.length; j++) {
      if (linked[j].getSmartObject().changed) throw new Error('still flagged after the update');
    }
    console.log('update-ok');
  )JS")));
  CHECK(smart_object_backlog_contains(window, QStringLiteral("update-ok")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  {
    const auto layers = linked_layers(document);
    CHECK(layers.size() == 3U);
    for (const auto* layer : layers) {
      const auto color = layer_center_color(*layer);
      CHECK(color.blue() > 200 && color.red() < 60 && color.alpha() == 255);
    }
  }
  // Undo brings all three red circles back.
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  for (const auto* layer : linked_layers(document)) {
    CHECK(layer_center_color(*layer).red() > 200);
  }
}

// doc.addSmartObject placement options and refusals, and the read-only layer state.
void ui_script_smart_object_options_and_errors() {
  patchy::ui::MainWindow window;
  show_window(window);
  add_linked_test_document(window, 300, 200);
  const auto dir = linked_test_dir(QStringLiteral("script-options"));
  const auto png_path = write_linked_test_png(dir + QStringLiteral("/art.png"), QColor(20, 200, 40, 255));
  const auto missing_path = dir + QStringLiteral("/missing.png");
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var png = %1, missing = %2;
    var doc = app.activeDocument;
    var base = doc.layers[0];
    function quadOf(layer) { return layer.getSmartObject().quad; }
    function near(actual, expected, what) {
      if (Math.abs(actual - expected) > 0.6) throw new Error(what + ': ' + actual + ' != ' + expected);
    }
    // Default: embedded, physical size, centered, on top and active.
    var e = doc.addSmartObject(png);
    var so = e.getSmartObject();
    if (so.linked || so.path !== '' || so.relativePath !== '' || so.missing || so.changed) throw new Error('embedded state');
    if (so.fileName !== 'art.png' || so.width !== 40 || so.height !== 20) throw new Error('embedded contents');
    if (e.name !== 'art' || doc.activeLayer.id !== e.id || doc.layers[doc.layers.length - 1].id !== e.id) throw new Error('embedded layer');
    near(so.quad[0], 130, 'default x'); near(so.quad[1], 90, 'default y');
    near(so.quad[4], 170, 'default right'); near(so.quad[5], 110, 'default bottom');
    // scale multiplies the physical size and stays centered.
    var s = quadOf(doc.addSmartObject(png, {scale: 2}));
    near(s[0], 110, 'scale x'); near(s[2] - s[0], 80, 'scale width'); near(s[7] - s[1], 40, 'scale height');
    // An explicit size wins over scale; x and y are the top-left corner.
    var w = quadOf(doc.addSmartObject(png, {width: 100, height: 10, x: -5, y: 7, scale: 9}));
    near(w[0], -5, 'size x'); near(w[1], 7, 'size y'); near(w[4], 95, 'size right'); near(w[5], 17, 'size bottom');
    // Each embedded placement holds its own copy.
    if (doc.layers[doc.layers.length - 1].getSmartObject().sourceId === so.sourceId) throw new Error('embedded sources shared');

    var count = doc.layers.length;
    function refuses(what, call) {
      var threw = false;
      try { call(); } catch (error) { threw = true; }
      if (!threw) throw new Error(what + ' was accepted');
      if (doc.layers.length !== count) throw new Error(what + ' changed the document');
    }
    refuses('a missing file', function () { doc.addSmartObject(missing, {linked: true}); });
    refuses('an empty path', function () { doc.addSmartObject(''); });
    refuses('an unknown option', function () { doc.addSmartObject(png, {size: 10}); });
    refuses('a text width', function () { doc.addSmartObject(png, {width: 'wide'}); });
    refuses('a NaN scale', function () { doc.addSmartObject(png, {scale: NaN}); });
    refuses('a zero scale', function () { doc.addSmartObject(png, {scale: 0}); });
    refuses('a negative height', function () { doc.addSmartObject(png, {height: -4}); });
    refuses('a huge width', function () { doc.addSmartObject(png, {width: 40000}); });
    refuses('non-object options', function () { doc.addSmartObject(png, 5); });
    refuses('updating an embedded smart object', function () { e.updateSmartObject(); });
    refuses('updating a pixel layer', function () { base.updateSmartObject(); });
    if (base.isSmartObject || base.getSmartObject() !== null) throw new Error('pixel layer state');
    console.log('options-ok');
  )JS")
                                            .arg(js_string(png_path), js_string(missing_path))));
  CHECK(smart_object_backlog_contains(window, QStringLiteral("options-ok")));
  // Every placement of the run rides one undo step.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.layers().size() == 4U);
  CHECK(linked_sources(document).empty());
}

// A linked file that is gone: the open reports it, the layer keeps its stored
// preview, update refuses with the reason, saving keeps the link, and Relink to
// File (which accepts SVG) repairs it.
void ui_smart_object_missing_linked_file_is_reported_and_relinks() {
  patchy::ui::MainWindow window;
  show_window(window);
  add_linked_test_document(window, 200, 160);
  const auto dir = linked_test_dir(QStringLiteral("missing"));
  const auto art_path = write_linked_test_png(dir + QStringLiteral("/art.png"), QColor(20, 200, 40, 255));
  const auto psd_path = dir + QStringLiteral("/parent.psd");
  patchy::ui::MainWindowTestAccess::place_linked_file_with_path(window, art_path);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd_path));
  CHECK(QFile::remove(art_path));

  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("was not found")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::LayerId layer_id = 0;
  {
    const auto layers = linked_layers(document);
    CHECK(layers.size() == 1U);
    layer_id = layers.front()->id();
    CHECK(layer_center_color(*layers.front()).green() > 150);  // the stored preview
    const auto sources = linked_sources(document);
    CHECK(sources.size() == 1U);
    CHECK(!patchy::ui::resolve_smart_object_external_path(*sources.front(), dir).has_value());
  }

  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = null;
    for (var i = 0; i < doc.layers.length; i++) { if (doc.layers[i].isSmartObject) layer = doc.layers[i]; }
    var so = layer.getSmartObject();
    if (!so.linked || !so.missing || so.changed) throw new Error('missing state');
    if (so.path !== %1) throw new Error('stored path ' + so.path);
    if (so.relativePath !== 'art.png') throw new Error('relativePath ' + so.relativePath);
    var message = '';
    try { layer.updateSmartObject(); } catch (error) { message = String(error); }
    if (message.indexOf('not found') < 0) throw new Error('update message: ' + message);
    console.log('missing-ok');
  )JS")
                                            .arg(js_string(art_path))));
  CHECK(smart_object_backlog_contains(window, QStringLiteral("missing-ok")));

  // The menu command refuses the same way and leaves the document alone.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  document.set_active_layer(layer_id);
  QApplication::processEvents();
  require_action(window, "layerSmartObjectUpdateAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("was not found")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);

  // Saving with the file still missing keeps the link exactly as stored.
  const auto resaved_path = dir + QStringLiteral("/resaved.psd");
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, resaved_path));
  {
    const auto resaved = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(resaved_path));
    const auto sources = linked_sources(resaved);
    CHECK(sources.size() == 1U);
    CHECK(sources.front()->filename == "art.png");
    CHECK(sources.front()->external_rel_path == "art.png");
    CHECK(QString::fromStdString(sources.front()->external_original_path) == QDir::toNativeSeparators(art_path));
    CHECK(linked_layers(resaved).size() == 1U);
  }

  // Relink to File takes an SVG: the link becomes vector contents and renders again.
  const auto svg_path = dir + QStringLiteral("/mark.svg");
  write_linked_test_file(svg_path, linked_test_svg("#0000ff"));
  document.set_active_layer(layer_id);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::relink_smart_object_contents_with_path(window, svg_path);
  QApplication::processEvents();
  const auto* relinked = std::as_const(document).find_layer(layer_id);
  CHECK(relinked != nullptr);
  CHECK(patchy::smart_object_lock_reason(*relinked) == "external");
  const auto placement = patchy::smart_object_placement_from_layer(*relinked);
  CHECK(placement.has_value());
  CHECK(placement->placed_type == 1);
  CHECK(placement->width == 64.0 && placement->height == 32.0);
  const auto sources = linked_sources(document);
  CHECK(sources.size() == 1U);
  CHECK(sources.front()->filename == "mark.svg" && sources.front()->filetype == "SVG ");
  CHECK(sources.front()->external_rel_path == "mark.svg");
  CHECK(layer_center_color(*relinked).blue() > 200);
}

// Photoshop 2026's own linked placement (scripts\dev\smart-objects\ps-capture-linked.ps1):
// its stamp is UTC, so the untouched file must read as unchanged in any time zone,
// and the link resolves beside the document and one folder up.
// --- Geometry operations re-render linked placements from their files -----------

namespace {

// Compares a layer's pixel bytes with a fresh render of it from its source through
// its current placement: what "re-rendered, not resampled" means for a linked layer.
bool layer_pixels_match_fresh_render(const patchy::Document& document, const patchy::Layer& layer,
                                     const QString& document_dir,
                                     patchy::ui::CanvasWidget::TransformInterpolation interpolation) {
  const auto expected =
      patchy::ui::render_smart_object_layer_preview(document, layer, interpolation, nullptr, document_dir);
  if (!expected.has_value()) {
    return false;
  }
  const auto& pixels = layer.pixels();
  const auto& fresh = expected->rendered.pixels;
  if (layer.bounds().x != expected->rendered.bounds.x || layer.bounds().y != expected->rendered.bounds.y ||
      pixels.width() != fresh.width() || pixels.height() != fresh.height() || pixels.format() != fresh.format()) {
    return false;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    if (std::memcmp(pixels.row(y).data(), fresh.row(y).data(), static_cast<std::size_t>(pixels.width()) * 4U) != 0) {
      return false;
    }
  }
  return true;
}

// A 256 x 64 png of 4 px vertical stripes alternating red and blue at 72 dpi: finer
// than a 32 px wide placement's preview can hold, so a layer rendered from the old
// preview and one rendered from the file differ at every other stripe.
QString write_striped_test_png(const QString& path) {
  QImage image(256, 64, QImage::Format_RGBA8888);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, ((x / 4) % 2 == 0) ? QColor(255, 0, 0, 255) : QColor(0, 0, 255, 255));
    }
  }
  image.setDotsPerMeterX(2835);
  image.setDotsPerMeterY(2835);
  CHECK(image.save(path));
  return path;
}

}  // namespace

// doc.resizeImage re-renders a linked SVG and an embedded one from their sources, so
// a placement scaled by Image Size carries the same bytes a fresh placement at the
// new size does (crisp edges from the vector file, not the resampled old preview).
// The untouched file must not read as changed afterwards.
void ui_script_smart_object_image_size_rerenders_linked_and_embedded() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto dir = linked_test_dir(QStringLiteral("image-size"));
  const auto svg_path = dir + QStringLiteral("/logo.svg");
  const auto psd_path = dir + QStringLiteral("/board.psd");
  write_linked_test_file(svg_path, linked_test_svg("#ff0000"));

  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var svg = %1, psd = %2;
    var doc = app.newDocument(300, 200);
    var linked = doc.addSmartObject(svg, {linked: true, x: 10, y: 10, width: 64, name: 'linked'});
    var embedded = doc.addSmartObject(svg, {x: 100, y: 10, width: 64, name: 'embedded'});
    if (!doc.saveAs(psd)) throw new Error('save failed');
    function block(layer) {
      var p = layer.getPixels();
      return {x: p.x, y: p.y, width: p.width, height: p.height, data: new Uint8Array(p.data)};
    }
    function softRatio(b) {
      var soft = 0, covered = 0;
      for (var i = 3; i < b.data.length; i += 4) {
        if (b.data[i] > 0) { covered++; if (b.data[i] < 255) soft++; }
      }
      return soft / covered;
    }
    function sameBytes(a, b, what) {
      if (a.x !== b.x || a.y !== b.y || a.width !== b.width || a.height !== b.height) {
        throw new Error(what + ': geometry ' + [a.x, a.y, a.width, a.height] + ' vs ' + [b.x, b.y, b.width, b.height]);
      }
      for (var i = 0; i < a.data.length; i++) {
        if (a.data[i] !== b.data[i]) throw new Error(what + ': byte ' + i + ' differs');
      }
    }
    var softBefore = softRatio(block(linked));

    doc.resizeImage(1200, 800);

    var so = linked.getSmartObject();
    if (so.missing) throw new Error('link lost');
    if (so.changed) throw new Error('resize flagged the untouched file as changed');
    if (Math.abs(so.quad[0] - 40) > 0.001 || Math.abs(so.quad[2] - so.quad[0] - 256) > 0.001) throw new Error('quad ' + so.quad);
    if (so.width !== 64 || so.height !== 32) throw new Error('contents size changed ' + so.width + 'x' + so.height);
    // Fresh placements at the resized geometry are the reference: same file, same quad.
    var freshLinked = doc.addSmartObject(svg, {linked: true, x: 40, y: 40, width: 256, name: 'fresh linked'});
    var freshEmbedded = doc.addSmartObject(svg, {x: 400, y: 40, width: 256, name: 'fresh embedded'});
    sameBytes(block(linked), block(freshLinked), 'linked after resize');
    sameBytes(block(embedded), block(freshEmbedded), 'embedded after resize');
    var softAfter = softRatio(block(linked));
    if (!(softAfter < softBefore)) throw new Error('edges did not sharpen: ' + softBefore + ' -> ' + softAfter);
    console.log('image-size-ok');
  )JS")
                                            .arg(js_string(svg_path), js_string(psd_path))));
  CHECK(smart_object_backlog_contains(window, QStringLiteral("image-size-ok")));

  // Both re-rendered layers are Patchy rasters with dirty placement blocks, as
  // Image Size leaves an embedded one.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  for (const auto* name : {"linked", "embedded"}) {
    const patchy::Layer* layer = nullptr;
    for (const auto& candidate : std::as_const(document).layers()) {
      if (candidate.name() == name) {
        layer = &candidate;
      }
    }
    CHECK(layer != nullptr);
    CHECK(patchy::layer_smart_object_block_dirty(*layer));
    const auto& metadata = layer->metadata();
    const auto status = metadata.find(patchy::kLayerMetadataSmartObjectRasterStatus);
    CHECK(status != metadata.end() && status->second == patchy::kSmartObjectRasterStatusPatchy);
  }
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("not found")));
}

// A linked raster file re-renders from its full-resolution pixels: a 256 px wide png
// placed 32 px wide and then resized four times shows stripes the 32 px preview
// could never hold.
void ui_script_smart_object_linked_raster_rerenders_from_full_resolution() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto dir = linked_test_dir(QStringLiteral("image-size-raster"));
  const auto png_path = write_striped_test_png(dir + QStringLiteral("/stripes.png"));

  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var png = %1;
    var doc = app.newDocument(200, 100);
    var layer = doc.addSmartObject(png, {linked: true, x: 8, y: 8, width: 32});
    var so = layer.getSmartObject();
    if (!so.linked || so.width !== 256 || so.height !== 64) throw new Error('contents ' + so.width + 'x' + so.height);
    doc.resizeImage(800, 400);
    so = layer.getSmartObject();
    if (so.changed || so.missing) throw new Error('resize flagged the link');
    var p = layer.getPixels();
    if (p.width !== 128 || p.height !== 32) throw new Error('layer size ' + p.width + 'x' + p.height);
    var d = new Uint8Array(p.data);
    function rgb(x, y) { var i = (y * p.width + x) * 4; return [d[i], d[i + 1], d[i + 2], d[i + 3]]; }
    // 4 px source stripes are 2 px wide at this scale: x 0..1 red, x 2..3 blue, ...
    var red = rgb(0, 16), blue = rgb(2, 16), red2 = rgb(4, 16);
    if (!(red[0] > 200 && red[2] < 60 && red[3] === 255)) throw new Error('x0 ' + red);
    if (!(blue[2] > 200 && blue[0] < 60 && blue[3] === 255)) throw new Error('x2 ' + blue);
    if (!(red2[0] > 200 && red2[2] < 60)) throw new Error('x4 ' + red2);
    console.log('raster-ok');
  )JS")
                                            .arg(js_string(png_path))));
  CHECK(smart_object_backlog_contains(window, QStringLiteral("raster-ok")));
}

// A linked file that is gone keeps the resampled preview the resize produced: the
// placement still scales, nothing throws, the link still reads as missing, and the
// status bar names the file.
void ui_script_smart_object_missing_linked_file_keeps_preview_on_image_size() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto dir = linked_test_dir(QStringLiteral("image-size-missing"));
  const auto svg_path = dir + QStringLiteral("/logo.svg");
  const auto psd_path = dir + QStringLiteral("/board.psd");
  write_linked_test_file(svg_path, linked_test_svg("#ff0000"));
  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var doc = app.newDocument(300, 200);
    doc.addSmartObject(%1, {linked: true, x: 10, y: 10, width: 64});
    if (!doc.saveAs(%2)) throw new Error('save failed');
    doc.close();
  )JS")
                                            .arg(js_string(svg_path), js_string(psd_path))));
  CHECK(QFile::remove(svg_path));

  CHECK(run_smart_object_script(window, QStringLiteral(R"JS(
    var doc = app.open(%1);
    var layer = doc.findLayer('logo');
    if (!layer.getSmartObject().missing) throw new Error('the removed file still resolves');
    var before = layer.bounds;
    var centerBefore = (function () { var p = layer.getPixels(); var d = new Uint8Array(p.data);
      var i = ((p.height >> 1) * p.width + (p.width >> 1)) * 4; return [d[i], d[i + 1], d[i + 2], d[i + 3]]; })();
    doc.resizeImage(600, 400);
    layer = doc.findLayer('logo');
    var so = layer.getSmartObject();
    if (!so.missing) throw new Error('missing flag lost');
    if (Math.abs(so.quad[0] - 20) > 0.001 || Math.abs(so.quad[2] - so.quad[0] - 128) > 0.001) throw new Error('quad ' + so.quad);
    var after = layer.bounds;
    if (Math.abs(after.width - before.width * 2) > 1 || Math.abs(after.height - before.height * 2) > 1) {
      throw new Error('bounds ' + JSON.stringify(before) + ' -> ' + JSON.stringify(after));
    }
    var p = layer.getPixels();
    var d = new Uint8Array(p.data);
    var i = ((p.height >> 1) * p.width + (p.width >> 1)) * 4;
    if (!(d[i] > 200 && d[i + 2] < 60 && d[i + 3] === 255)) throw new Error('resampled preview lost: ' + [d[i], d[i + 1], d[i + 2], d[i + 3]] + ' was ' + centerBefore);
    console.log('missing-ok');
  )JS")
                                            .arg(js_string(psd_path))));
  CHECK(smart_object_backlog_contains(window, QStringLiteral("missing-ok")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("logo.svg")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("not found")));
}

// Free Transform and Warp on a linked SVG re-render from the file like an embedded
// placement does (the vector rasterizes at the new scale); once the file is gone the
// transform keeps its resampled pixels and reports the file, and Warp refuses.
void ui_smart_object_free_transform_and_warp_rerender_linked_svg() {
  patchy::ui::MainWindow window;
  show_window(window);
  add_linked_test_document(window, 300, 200);
  QApplication::processEvents();
  const auto dir = linked_test_dir(QStringLiteral("free-transform"));
  const auto svg_path = dir + QStringLiteral("/logo.svg");
  write_linked_test_file(svg_path, linked_test_svg("#ff0000"));
  patchy::ui::MainWindowTestAccess::place_linked_file_with_path(window, svg_path);
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(std::as_const(document).layers().size() == 2U);
  const auto layer_id = std::as_const(document).layers().back().id();
  CHECK(patchy::smart_object_lock_reason(*std::as_const(document).find_layer(layer_id)) == "external");
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, QStringLiteral("logo"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(item);
  item->setSelected(true);
  QApplication::processEvents();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  // The document has no folder: links resolve through their absolute path.
  const QString document_dir;

  // Scale 3x: the placement triples and the pixels are a fresh vector render.
  const auto before = patchy::smart_object_placement_from_layer(*std::as_const(document).find_layer(layer_id));
  CHECK(before.has_value());
  CHECK(canvas->begin_free_transform());
  const auto controls = canvas->transform_controls_state();
  CHECK(controls.has_value());
  CHECK(canvas->set_transform_controls_state(controls->reference_position, 300.0, 300.0, 0.0));
  QApplication::processEvents();
  canvas->finish_free_transform();
  QApplication::processEvents();
  {
    const auto* transformed = std::as_const(document).find_layer(layer_id);
    CHECK(transformed != nullptr);
    const auto after = patchy::smart_object_placement_from_layer(*transformed);
    CHECK(after.has_value());
    CHECK(std::abs((after->transform[2] - after->transform[0]) - (before->transform[2] - before->transform[0]) * 3.0) <
          0.6);
    CHECK(patchy::smart_object_lock_reason(*transformed) == "external");
    const auto& metadata = transformed->metadata();
    const auto status = metadata.find(patchy::kLayerMetadataSmartObjectRasterStatus);
    CHECK(status != metadata.end() && status->second == patchy::kSmartObjectRasterStatusPatchy);
    CHECK(layer_pixels_match_fresh_render(std::as_const(document), *transformed, document_dir,
                                          canvas->transform_interpolation()));
    CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("not found")));
  }

  // Warp bakes the linked contents through a preset mesh; the layer stays linked.
  CHECK(canvas->begin_warp_transform());
  CHECK(canvas->warp_transform_active());
  canvas->apply_warp_style_preset(QStringLiteral("warpArc"), 50.0);
  canvas->finish_warp_transform();
  QApplication::processEvents();
  CHECK(!canvas->warp_transform_active());
  {
    const auto* warped = std::as_const(document).find_layer(layer_id);
    CHECK(warped != nullptr);
    const auto warp = patchy::smart_object_warp_from_layer(*warped);
    CHECK(warp.has_value() && !warp->mesh_xs.empty());
    CHECK(patchy::smart_object_lock_reason(*warped) == "external");
    CHECK(!warped->pixels().empty());
    CHECK(layer_pixels_match_fresh_render(std::as_const(document), *warped, document_dir,
                                          canvas->transform_interpolation()));
  }
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(!patchy::smart_object_warp_from_layer(*std::as_const(document).find_layer(layer_id)).has_value());

  // The file disappears: a transform keeps the resampled pixels and names the file.
  // (Undo rebuilt the layer list, so select the row again before transforming.)
  CHECK(QFile::remove(svg_path));
  item = require_layer_item(*layer_list, QStringLiteral("logo"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(item);
  item->setSelected(true);
  QApplication::processEvents();
  CHECK(std::as_const(document).active_layer_id() == layer_id);
  const auto bounds_before_missing = std::as_const(document).find_layer(layer_id)->bounds();
  CHECK(canvas->begin_free_transform());
  const auto controls_missing = canvas->transform_controls_state();
  CHECK(controls_missing.has_value());
  CHECK(canvas->set_transform_controls_state(controls_missing->reference_position, 50.0, 50.0, 0.0));
  QApplication::processEvents();
  canvas->finish_free_transform();
  QApplication::processEvents();
  {
    const auto* halved = std::as_const(document).find_layer(layer_id);
    CHECK(halved != nullptr);
    // The resampled fallback alpha-trims to the visible circle: radius 12 at 3x is a
    // 72 px disc, so half of it is 36 px (the vector render above kept the whole
    // 192 x 96 artwork rect).
    CHECK(bounds_before_missing.width == 192);
    CHECK(std::abs(halved->bounds().width - 36) <= 2 && std::abs(halved->bounds().height - 36) <= 2);
    CHECK(!halved->pixels().empty());
    CHECK(patchy::smart_object_lock_reason(*halved) == "external");
    CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("logo.svg")));
    CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("not found")));
  }
  CHECK(!canvas->begin_warp_transform());
  CHECK(!canvas->warp_transform_active());
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("not found")));
}

void ui_smart_object_photoshop_linked_capture_resolves_if_available() {
  const auto dir = QFileInfo(patchy::ui::to_qstring(patchy::test::local_psd_fixture_path("ps2026_linked/linked_svg.psd")))
                        .absolutePath();
  const auto psd_path = dir + QStringLiteral("/linked_svg.psd");
  const auto svg_path = dir + QStringLiteral("/logo.svg");
  if (!QFileInfo::exists(psd_path) || !QFileInfo::exists(svg_path)) {
    std::cout << "[SKIP] ps2026_linked capture missing: " << psd_path.toStdString() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_path));
  const auto sources = linked_sources(document);
  CHECK(sources.size() == 1U);
  const auto resolved = patchy::ui::resolve_smart_object_external_path(*sources.front(), dir);
  CHECK(resolved.has_value() && QFileInfo(*resolved) == QFileInfo(svg_path));
  CHECK(!patchy::ui::smart_object_link_changed_on_disk(*sources.front(), QFileInfo(svg_path)));

  const auto parent = patchy::psd::DocumentIo::read_file(
      patchy::ui::to_filesystem_path(dir + QStringLiteral("/sub/linked_svg_parent.psd")));
  const auto parent_sources = linked_sources(parent);
  CHECK(parent_sources.size() == 1U);
  const auto parent_resolved =
      patchy::ui::resolve_smart_object_external_path(*parent_sources.front(), dir + QStringLiteral("/sub"));
  CHECK(parent_resolved.has_value() && QFileInfo(*parent_resolved) == QFileInfo(svg_path));

  // Photoshop's render against Patchy's vector render of the same link, for the flat
  // logo and for the folded one (edit.svg after the capture's update: gradients in
  // userSpaceOnUse units inside scaled and translated groups).
  const auto compare_render = [&](const QString& psd_name, const QString& svg_name, double minimum_width) {
    const auto capture_path = dir + QLatin1Char('/') + psd_name;
    const auto linked_path = dir + QLatin1Char('/') + svg_name;
    if (!QFileInfo::exists(capture_path) || !QFileInfo::exists(linked_path)) {
      std::cout << "[SKIP] ps2026_linked capture missing: " << capture_path.toStdString() << '\n';
      return;
    }
    const auto capture = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(capture_path));
    const auto probe = patchy::ui::load_smart_object_file_probe(linked_path);
    CHECK(probe.has_value());
    bool compared = false;
    for (const auto* layer : linked_layers(capture)) {
      const auto placement = patchy::smart_object_placement_from_layer(*layer);
      CHECK(placement.has_value() && placement->placed_type == 1);
      if (placement->transform[2] - placement->transform[0] < minimum_width) {
        continue;
      }
      const auto vector_image = patchy::ui::render_smart_object_vector_contents(*probe, *placement);
      CHECK(vector_image.has_value());
      const auto bounds = layer->bounds();
      const auto& pixels = layer->pixels();
      CHECK(pixels.format().channels == 4);
      const int left = static_cast<int>(std::lround(placement->transform[0]));
      const int top = static_cast<int>(std::lround(placement->transform[1]));
      std::int64_t error = 0;
      for (int y = 0; y < bounds.height; ++y) {
        for (int x = 0; x < bounds.width; ++x) {
          const auto* px = pixels.pixel(x, y);
          const auto ours = vector_image->pixelColor(bounds.x + x - left, bounds.y + y - top);
          error += std::abs(static_cast<int>(px[3]) - ours.alpha());
          for (int channel = 0; channel < 3; ++channel) {
            const int theirs = px[channel];
            const int mine = channel == 0 ? ours.red() : channel == 1 ? ours.green() : ours.blue();
            error += std::abs(theirs - mine) * px[3] / 255;
          }
        }
      }
      // Two independent rasterizers: a level or two of edge antialiasing per pixel.
      CHECK(error < static_cast<std::int64_t>(bounds.width) * bounds.height * 3);
      compared = true;
    }
    CHECK(compared);
  };
  compare_render(QStringLiteral("linked_svg.psd"), QStringLiteral("logo.svg"), 100.0);
  compare_render(QStringLiteral("linked_svg_update.psd"), QStringLiteral("edit.svg"), 1000.0);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_object_tests() {
  return {
      {"ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel",
       ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel},
      {"ui_layer_smart_object_badge_button_opens_contents", ui_layer_smart_object_badge_button_opens_contents},
      {"ui_smart_object_to_normal_layer_action_rasterizes", ui_smart_object_to_normal_layer_action_rasterizes},
      {"ui_smart_object_paint_prompt_rasterize_choice", ui_smart_object_paint_prompt_rasterize_choice},
      {"ui_smart_object_paint_prompt_edit_contents_choice", ui_smart_object_paint_prompt_edit_contents_choice},
      {"ui_smart_object_paint_prompt_cancel_choice", ui_smart_object_paint_prompt_cancel_choice},
      {"ui_smart_object_paint_prompt_locked_omits_edit_contents",
       ui_smart_object_paint_prompt_locked_omits_edit_contents},
      {"ui_layer_smart_object_badge_shows_linked_variant", ui_layer_smart_object_badge_shows_linked_variant},
      {"ui_smart_object_edit_contents_commit_rerenders_parent",
       ui_smart_object_edit_contents_commit_rerenders_parent},
      {"ui_smart_object_locked_refusal_and_parent_close_prompt",
       ui_smart_object_locked_refusal_and_parent_close_prompt},
      {"ui_smart_object_replace_contents_repoints_shared_layers",
       ui_smart_object_replace_contents_repoints_shared_layers},
      {"ui_smart_object_nested_contents_edit_commits_up_the_chain",
       ui_smart_object_nested_contents_edit_commits_up_the_chain},
      {"ui_smart_object_convert_composites_identically_and_undoes",
       ui_smart_object_convert_composites_identically_and_undoes},
      {"ui_smart_object_convert_keeps_masks_in_place", ui_smart_object_convert_keeps_masks_in_place},
      {"ui_smart_object_edit_commit_keeps_canvas_transparency",
       ui_smart_object_edit_commit_keeps_canvas_transparency},
      {"ui_smart_object_legacy_black_composite_decodes_transparent",
       ui_smart_object_legacy_black_composite_decodes_transparent},
      {"ui_smart_object_psbtest_repro_decodes_transparent_if_available",
       ui_smart_object_psbtest_repro_decodes_transparent_if_available},
      {"ui_smart_object_place_embedded_centers_and_fits", ui_smart_object_place_embedded_centers_and_fits},
      {"ui_smart_object_convert_to_layers_restores_layers_in_place",
       ui_smart_object_convert_to_layers_restores_layers_in_place},
      {"ui_smart_object_convert_to_layers_follows_scaled_placement",
       ui_smart_object_convert_to_layers_follows_scaled_placement},
      {"ui_smart_object_orphaned_source_is_not_written", ui_smart_object_orphaned_source_is_not_written},
      {"ui_smart_object_via_copy_diverges_and_transform_rerenders",
       ui_smart_object_via_copy_diverges_and_transform_rerenders},
      {"ui_smart_object_image_size_scales_placement_and_rerenders",
       ui_smart_object_image_size_scales_placement_and_rerenders},
      {"ui_smart_object_external_edit_from_disk_saves_and_refreshes_parent",
       ui_smart_object_external_edit_from_disk_saves_and_refreshes_parent},
      {"ui_smart_object_update_content_rereads_linked_file",
       ui_smart_object_update_content_rereads_linked_file},
      {"ui_smart_object_stale_linked_file_noticed_on_open",
       ui_smart_object_stale_linked_file_noticed_on_open},
      {"ui_smart_object_relink_and_embed_linked_work", ui_smart_object_relink_and_embed_linked_work},
      {"ui_smart_object_place_linked_links_the_file", ui_smart_object_place_linked_links_the_file},
      {"ui_smart_object_placed_svg_is_vector_contents", ui_smart_object_placed_svg_is_vector_contents},
      {"ui_smart_object_linked_svg_child_saves_vectors_without_warning",
       ui_smart_object_linked_svg_child_saves_vectors_without_warning},
      {"ui_script_smart_object_linked_round_trip_and_update",
       ui_script_smart_object_linked_round_trip_and_update},
      {"ui_script_smart_object_replace_contents_shared_unicode_and_ppi",
       ui_script_smart_object_replace_contents_shared_unicode_and_ppi},
      {"ui_script_smart_object_replace_contents_errors_are_atomic",
       ui_script_smart_object_replace_contents_errors_are_atomic},
      {"ui_script_smart_object_options_and_errors", ui_script_smart_object_options_and_errors},
      {"ui_smart_object_missing_linked_file_is_reported_and_relinks",
       ui_smart_object_missing_linked_file_is_reported_and_relinks},
      {"ui_script_smart_object_image_size_rerenders_linked_and_embedded",
       ui_script_smart_object_image_size_rerenders_linked_and_embedded},
      {"ui_script_smart_object_linked_raster_rerenders_from_full_resolution",
       ui_script_smart_object_linked_raster_rerenders_from_full_resolution},
      {"ui_script_smart_object_missing_linked_file_keeps_preview_on_image_size",
       ui_script_smart_object_missing_linked_file_keeps_preview_on_image_size},
      {"ui_smart_object_free_transform_and_warp_rerender_linked_svg",
       ui_smart_object_free_transform_and_warp_rerender_linked_svg},
      {"ui_smart_object_photoshop_linked_capture_resolves_if_available",
       ui_smart_object_photoshop_linked_capture_resolves_if_available},
  };
}
