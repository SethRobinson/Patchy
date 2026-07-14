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
  CHECK(source_after->external_file_size > 0U);
  CHECK(source_after->external_mod_year >= 2026);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);
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
      patchy::ui::MainWindowTestAccess::document(window), std::filesystem::path(parent_path.toStdString()));
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

}  // namespace

std::vector<patchy::test::TestCase> smart_object_tests() {
  return {
      {"ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel",
       ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel},
      {"ui_layer_smart_object_badge_button_opens_contents", ui_layer_smart_object_badge_button_opens_contents},
      {"ui_smart_object_to_normal_layer_action_rasterizes", ui_smart_object_to_normal_layer_action_rasterizes},
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
      {"ui_smart_object_edit_commit_keeps_canvas_transparency",
       ui_smart_object_edit_commit_keeps_canvas_transparency},
      {"ui_smart_object_legacy_black_composite_decodes_transparent",
       ui_smart_object_legacy_black_composite_decodes_transparent},
      {"ui_smart_object_psbtest_repro_decodes_transparent_if_available",
       ui_smart_object_psbtest_repro_decodes_transparent_if_available},
      {"ui_smart_object_place_embedded_centers_and_fits", ui_smart_object_place_embedded_centers_and_fits},
      {"ui_smart_object_via_copy_diverges_and_transform_rerenders",
       ui_smart_object_via_copy_diverges_and_transform_rerenders},
      {"ui_smart_object_external_edit_from_disk_saves_and_refreshes_parent",
       ui_smart_object_external_edit_from_disk_saves_and_refreshes_parent},
      {"ui_smart_object_update_content_rereads_linked_file",
       ui_smart_object_update_content_rereads_linked_file},
      {"ui_smart_object_stale_linked_file_noticed_on_open",
       ui_smart_object_stale_linked_file_noticed_on_open},
      {"ui_smart_object_relink_and_embed_linked_work", ui_smart_object_relink_and_embed_linked_work},
  };
}
