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
#include "smart_filter_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_smart_filter_gallery_native_recipe_applies_atomically() {
  GallerySettingsRestorer gallery_settings;
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() == nullptr);
  const auto original_pixels = original->pixels();
  const auto original_bounds = original->bounds();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  const auto configure_gaussian_recipe = [&](QDialog& dialog,
                                             bool mixed_recipe) {
    auto* looks = dialog.findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog.findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog.findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr);
    auto* gaussian_item = require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur"));
    looks->setCurrentItem(gaussian_item);
    QApplication::processEvents();
    auto* preview = dialog.findChild<QWidget*>(
        QStringLiteral("filterGalleryPreview"));
    CHECK(preview != nullptr);
    CHECK(process_events_until(
        [&] {
          return preview->property("filterGalleryExactPreview").toBool() &&
                 gaussian_item->data(Qt::UserRole + 5).toBool();
        },
        10000));
    auto* radius = dialog.findChild<QSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(3);
    duplicate->click();
    QApplication::processEvents();
    if (mixed_recipe) {
      // Vintage Sepia stays Patchy-only, so the recipe cannot map natively.
      // (Box Blur was used here until it gained a native mapping.)
      looks->setCurrentItem(require_gallery_filter_item(
          *looks, QStringLiteral("patchy.filters.sepia")));
    } else {
      looks->setCurrentItem(require_gallery_filter_item(
          *looks, QStringLiteral("patchy.filters.high_pass")));
      QApplication::processEvents();
      auto* high_pass_radius = dialog.findChild<QDoubleSpinBox*>(
          QStringLiteral("filterRadiusSpin"));
      CHECK(high_pass_radius != nullptr);
      high_pass_radius->setValue(4.2);
      duplicate->click();
      QApplication::processEvents();
      looks->setCurrentItem(require_gallery_filter_item(
          *looks,
          QStringLiteral("patchy.filters.dust_and_scratches")));
      QApplication::processEvents();
      auto* dust_radius = dialog.findChild<QSpinBox*>(
          QStringLiteral("filterRadiusSpin"));
      auto* dust_threshold = dialog.findChild<QSpinBox*>(
          QStringLiteral("filterThresholdSpin"));
      CHECK(dust_radius != nullptr && dust_threshold != nullptr);
      dust_radius->setValue(7);
      dust_threshold->setValue(23);
      duplicate->click();
      QApplication::processEvents();
      looks->setCurrentItem(require_gallery_filter_item(
          *looks, QStringLiteral("patchy.filters.surface_blur")));
      QApplication::processEvents();
      auto* surface_radius = dialog.findChild<QDoubleSpinBox*>(
          QStringLiteral("filterRadiusSpin"));
      auto* surface_threshold = dialog.findChild<QSpinBox*>(
          QStringLiteral("filterThresholdSpin"));
      CHECK(surface_radius != nullptr && surface_threshold != nullptr);
      surface_radius->setValue(9.25);
      surface_threshold->setValue(31);
    }
    QApplication::processEvents();
    CHECK(applied->count() == (mixed_recipe ? 2 : 4));
  };

  bool applied_native = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("filterGalleryDialog")));
    CHECK(dialog != nullptr);
    configure_gaussian_recipe(*dialog, false);
    auto* outcome = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryOutcomeLabel"));
    CHECK(outcome != nullptr);
    CHECK(outcome->text() ==
          QStringLiteral("Applies as editable Smart Filters."));
    CHECK(process_events_until(
        [&] {
          const auto* layer = std::as_const(document).find_layer(layer_id);
          return layer != nullptr && layer->smart_filter_stack() == nullptr &&
                 (!filter_rect_equal(layer->bounds(), original_bounds) ||
                  !patchy::ui::pixel_buffers_equal(layer->pixels(),
                                                   original_pixels));
        },
        7000));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(
        QStringLiteral("filterGalleryButtonBox"));
    CHECK(buttons != nullptr &&
          buttons->button(QDialogButtonBox::Ok) != nullptr);
    applied_native = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(applied_native);

  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && filtered->smart_filter_stack() != nullptr);
  CHECK(filtered->smart_filter_stack()->entries.size() == 4U);
  const auto* native_gaussian = std::get_if<patchy::GaussianBlurSmartFilter>(
      &filtered->smart_filter_stack()->entries[0].parameters);
  const auto* native_high_pass = std::get_if<patchy::HighPassSmartFilter>(
      &filtered->smart_filter_stack()->entries[1].parameters);
  const auto* native_dust =
      std::get_if<patchy::DustAndScratchesSmartFilter>(
          &filtered->smart_filter_stack()->entries[2].parameters);
  const auto* native_surface =
      std::get_if<patchy::SurfaceBlurSmartFilter>(
          &filtered->smart_filter_stack()->entries[3].parameters);
  CHECK(native_gaussian != nullptr && native_high_pass != nullptr &&
        native_dust != nullptr && native_surface != nullptr);
  CHECK(std::abs(native_gaussian->radius_pixels - 3.0) < 0.000001);
  CHECK(std::abs(native_high_pass->radius_pixels - 4.2) < 0.000001);
  CHECK(native_dust->radius_pixels == 7 && native_dust->threshold == 23);
  CHECK(std::abs(native_surface->radius_pixels - 9.25) < 0.000001);
  CHECK(native_surface->threshold == 31);
  CHECK(patchy::smart_object_lock_reason(*filtered).empty());
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(placed_uuid) != nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  const auto reopened = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr &&
        reopened_it->smart_filter_stack()->entries.size() == 4U);
  CHECK(std::get_if<patchy::GaussianBlurSmartFilter>(
            &reopened_it->smart_filter_stack()->entries[0].parameters) !=
        nullptr);
  const auto* reopened_high_pass = std::get_if<patchy::HighPassSmartFilter>(
      &reopened_it->smart_filter_stack()->entries[1].parameters);
  CHECK(reopened_high_pass != nullptr &&
        std::abs(reopened_high_pass->radius_pixels - 4.2) < 0.000001);
  const auto* reopened_dust =
      std::get_if<patchy::DustAndScratchesSmartFilter>(
          &reopened_it->smart_filter_stack()->entries[2].parameters);
  CHECK(reopened_dust != nullptr && reopened_dust->radius_pixels == 7 &&
        reopened_dust->threshold == 23);
  const auto* reopened_surface =
      std::get_if<patchy::SurfaceBlurSmartFilter>(
          &reopened_it->smart_filter_stack()->entries[3].parameters);
  CHECK(reopened_surface != nullptr &&
        std::abs(reopened_surface->radius_pixels - 9.25) < 0.000001 &&
        reopened_surface->threshold == 31);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* undone = std::as_const(document).find_layer(layer_id);
  CHECK(undone != nullptr && undone->smart_filter_stack() == nullptr);
  CHECK(filter_rect_equal(undone->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(undone->pixels(), original_pixels));
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(placed_uuid) == nullptr);

  bool applied_mixed = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("filterGalleryDialog")));
    CHECK(dialog != nullptr);
    configure_gaussian_recipe(*dialog, true);
    // The outcome line already announces what the rasterize prompt below
    // will ask, and the blocking Sepia entry carries the warning mark.
    auto* outcome = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryOutcomeLabel"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    CHECK(outcome != nullptr && applied != nullptr);
    CHECK(outcome->text() ==
          QStringLiteral("Applying will rasterize the Smart Object (some "
                         "effects have no Smart Filter mapping)."));
    CHECK(applied->count() == 2);
    CHECK(applied->item(0)->text() == QStringLiteral("Vintage Sepia"));
    CHECK(!applied->item(0)->icon().isNull());
    CHECK(applied->item(1)->text() == QStringLiteral("Gaussian Blur"));
    CHECK(applied->item(1)->icon().isNull());
    auto* buttons = dialog->findChild<QDialogButtonBox*>(
        QStringLiteral("filterGalleryButtonBox"));
    CHECK(buttons != nullptr &&
          buttons->button(QDialogButtonBox::Ok) != nullptr);
    QTimer::singleShot(0, [&] {
      auto* warning = qobject_cast<QMessageBox*>(find_top_level_dialog(
          QStringLiteral("filterGalleryRasterizeMessageBox")));
      CHECK(warning != nullptr);
      auto* yes = warning->button(QMessageBox::Yes);
      CHECK(yes != nullptr);
      yes->click();
    });
    applied_mixed = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(applied_mixed);
  const auto* rasterized = std::as_const(document).find_layer(layer_id);
  CHECK(rasterized != nullptr && !patchy::layer_is_smart_object(*rasterized));
  CHECK(rasterized->smart_filter_stack() == nullptr);
  CHECK(!filter_rect_equal(rasterized->bounds(), original_bounds) ||
        !patchy::ui::pixel_buffers_equal(rasterized->pixels(),
                                         original_pixels));
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(placed_uuid) == nullptr);
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("Rasterized Smart Object")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && patchy::layer_is_smart_object(*restored));
  CHECK(restored->smart_filter_stack() == nullptr);
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));

  // Once a recipe grows beyond the native stack limit, the gallery falls
  // back to its destructive proxy. That transition must also remove the last
  // accepted exact render from the live layer.
  bool rejected_oversized_native_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("filterGalleryDialog")));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    auto* preview = dialog->findChild<QWidget*>(
        QStringLiteral("filterGalleryPreview"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr &&
          preview != nullptr);
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur")));
    CHECK(process_events_until(
        [&] {
          const auto* layer = std::as_const(document).find_layer(layer_id);
          return preview->property("filterGalleryExactPreview").toBool() &&
                 layer != nullptr &&
                 (!filter_rect_equal(layer->bounds(), original_bounds) ||
                  !patchy::ui::pixel_buffers_equal(layer->pixels(),
                                                   original_pixels));
        },
        7000));
    for (int count = 1; count < 65; ++count) {
      duplicate->click();
    }
    CHECK(applied->count() == 65);
    // The caller's 64-entry native stack cap rejects this recipe, not the
    // per-entry mapper: the outcome line flips to the rasterize text while
    // no individual row carries a warning mark.
    auto* outcome = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryOutcomeLabel"));
    CHECK(outcome != nullptr);
    CHECK(outcome->text() ==
          QStringLiteral("Applying will rasterize the Smart Object (some "
                         "effects have no Smart Filter mapping)."));
    for (int row = 0; row < applied->count(); ++row) {
      CHECK(applied->item(row)->icon().isNull());
    }
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(filter_rect_equal(layer->bounds(), original_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), original_pixels));
    rejected_oversized_native_preview = true;
    dialog->reject();
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(rejected_oversized_native_preview);
}

void ui_smart_filter_move_drag_and_nudge_rerender_cache_and_roundtrip() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  open_smart_filter_instances_fixture(window);
  const auto layer_id = select_named_layer(
      window, QString::fromLatin1(kSmartFilterInstanceBName));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() != nullptr);
  CHECK(patchy::smart_object_lock_reason(*original).empty());
  CHECK(original->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  const auto original_placement =
      patchy::smart_object_placement_from_layer(*original);
  CHECK(original_placement.has_value());
  const auto original_pixels = original->pixels();
  const auto original_bounds = original->bounds();
  const auto original_mask = original->smart_filter_stack()->mask;
  CHECK(filter_rect_equal(
      original_mask.bounds,
      patchy::Rect::from_size(document.width(), document.height())));
  CHECK(std::any_of(original_mask.pixels.data().begin(),
                    original_mask.pixels.data().end(),
                    [](std::uint8_t value) {
                      return value > 0U && value < 255U;
                    }));
  const auto placed_uuid = patchy::smart_object_placed_uuid(*original);
  const auto* original_record =
      std::as_const(document)
          .metadata()
          .smart_filter_effects.find_unique(placed_uuid);
  CHECK(original_record != nullptr && original_record->semantic_supported());
  const auto original_record_storage = original_record->raw_storage;
  const auto original_record_span =
      patchy::psd::raw_filter_effects_record_body(*original_record);
  const std::vector<std::uint8_t> original_record_body(
      original_record_span.begin(), original_record_span.end());
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  const auto verify_moved_state = [&](QPoint delta) {
    const auto* moved = std::as_const(document).find_layer(layer_id);
    CHECK(moved != nullptr && moved->smart_filter_stack() != nullptr);
    const auto placement = patchy::smart_object_placement_from_layer(*moved);
    CHECK(placement.has_value());
    for (std::size_t index = 0; index < placement->transform.size(); ++index) {
      const auto expected = original_placement->transform[index] +
                            (index % 2U == 0U ? delta.x() : delta.y());
      CHECK(std::abs(placement->transform[index] - expected) < 0.000001);
    }
    CHECK(filter_rect_equal(moved->smart_filter_stack()->mask.bounds,
                            original_mask.bounds));
    CHECK(patchy::ui::pixel_buffers_equal(
        moved->smart_filter_stack()->mask.pixels, original_mask.pixels));
    CHECK(moved->smart_filter_stack()->mask.default_color ==
          original_mask.default_color);
    CHECK(moved->smart_filter_stack()->mask.enabled == original_mask.enabled);
    CHECK(moved->smart_filter_stack()->mask.linked == original_mask.linked);
    CHECK(moved->smart_filter_stack()->mask.extend_with_white ==
          original_mask.extend_with_white);

    const auto expected_preview = patchy::ui::render_smart_object_layer_preview(
        std::as_const(document), *moved,
        canvas->transform_interpolation());
    CHECK(expected_preview.has_value());
    CHECK(filter_rect_equal(moved->bounds(),
                            expected_preview->rendered.bounds));
    CHECK(patchy::ui::pixel_buffers_equal(
        moved->pixels(), expected_preview->rendered.pixels));

    const auto* actual_record =
        std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(placed_uuid);
    CHECK(actual_record != nullptr && actual_record->semantic_supported());
    const auto expected_record = patchy::psd::author_filter_effects_record(
        placed_uuid, patchy::Rect::from_size(document.width(),
                                             document.height()),
        expected_preview->unfiltered.pixels,
        expected_preview->unfiltered.bounds,
        moved->smart_filter_stack()->mask);
    CHECK(expected_record.has_value());
    const auto actual_body =
        patchy::psd::raw_filter_effects_record_body(*actual_record);
    const auto expected_body =
        patchy::psd::raw_filter_effects_record_body(*expected_record);
    CHECK(actual_body.size() == expected_body.size());
    CHECK(std::equal(actual_body.begin(), actual_body.end(),
                     expected_body.begin()));
  };

  QPoint opaque_document_point;
  bool found_opaque = false;
  for (int y = 0; y < original_pixels.height() && !found_opaque; ++y) {
    for (int x = 0; x < original_pixels.width(); ++x) {
      const auto* pixel = original_pixels.pixel(x, y);
      if (pixel != nullptr && original_pixels.format().channels >= 4U &&
          pixel[3] > 128U) {
        opaque_document_point =
            QPoint(original_bounds.x + x, original_bounds.y + y);
        found_opaque = true;
        break;
      }
    }
  }
  CHECK(found_opaque);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  canvas->setFocus(Qt::OtherFocusReason);

  const QPoint drag_delta(7, 5);
  drag(*canvas,
       canvas->widget_position_for_document_point(opaque_document_point),
       canvas->widget_position_for_document_point(opaque_document_point +
                                                   drag_delta));
  QApplication::processEvents();
  verify_moved_state(drag_delta);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  const auto* dragged_record =
      std::as_const(document)
          .metadata()
          .smart_filter_effects.find_unique(placed_uuid);
  CHECK(dragged_record != nullptr);
  CHECK(dragged_record->raw_storage != original_record_storage);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* drag_undone = std::as_const(document).find_layer(layer_id);
  CHECK(drag_undone != nullptr);
  CHECK(smart_object_placements_equal(
      patchy::smart_object_placement_from_layer(*drag_undone),
      original_placement));
  CHECK(filter_rect_equal(drag_undone->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(drag_undone->pixels(),
                                        original_pixels));
  const auto* drag_undone_record =
      std::as_const(document)
          .metadata()
          .smart_filter_effects.find_unique(placed_uuid);
  CHECK(drag_undone_record != nullptr);
  CHECK(drag_undone_record->raw_storage == original_record_storage);
  const auto drag_undone_body =
      patchy::psd::raw_filter_effects_record_body(*drag_undone_record);
  CHECK(drag_undone_body.size() == original_record_body.size());
  CHECK(std::equal(drag_undone_body.begin(), drag_undone_body.end(),
                   original_record_body.begin()));

  canvas->setFocus(Qt::OtherFocusReason);
  send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  QApplication::processEvents();
  const QPoint nudge_delta(10, 0);
  verify_moved_state(nudge_delta);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  const auto* nudged = std::as_const(document).find_layer(layer_id);
  CHECK(nudged != nullptr);
  const auto nudged_pixels = nudged->pixels();
  const auto nudged_bounds = nudged->bounds();
  const auto nudged_placement =
      patchy::smart_object_placement_from_layer(*nudged);
  CHECK(nudged_placement.has_value());

  ensure_artifact_dir();
  const auto saved_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_moved_masked.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, saved_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(saved_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) {
        return layer.name() == kSmartFilterInstanceBName;
      });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(patchy::smart_object_lock_reason(*reopened_it).empty());
  CHECK(smart_object_placements_equal(
      patchy::smart_object_placement_from_layer(*reopened_it),
      nudged_placement));
  CHECK(filter_rect_equal(reopened_it->bounds(), nudged_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(reopened_it->pixels(),
                                        nudged_pixels));
  CHECK(filter_rect_equal(reopened_it->smart_filter_stack()->mask.bounds,
                          original_mask.bounds));
  CHECK(patchy::ui::pixel_buffers_equal(
      reopened_it->smart_filter_stack()->mask.pixels,
      original_mask.pixels));
  const auto* reopened_record =
      reopened.metadata().smart_filter_effects.find_unique(placed_uuid);
  CHECK(reopened_record != nullptr && reopened_record->semantic_supported());

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* nudge_undone = std::as_const(document).find_layer(layer_id);
  CHECK(nudge_undone != nullptr);
  CHECK(smart_object_placements_equal(
      patchy::smart_object_placement_from_layer(*nudge_undone),
      original_placement));
  CHECK(filter_rect_equal(nudge_undone->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(nudge_undone->pixels(),
                                        original_pixels));
}

void ui_convert_to_smart_object_rejects_tree_containing_smart_filter() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  open_smart_filter_instances_fixture(window);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto& roots = document.layers();
  const auto filtered_it = std::find_if(
      roots.begin(), roots.end(), [](const patchy::Layer& layer) {
        return layer.name() == kSmartFilterInstanceAName;
      });
  CHECK(filtered_it != roots.end());
  patchy::Layer filtered_child = std::move(*filtered_it);
  roots.erase(filtered_it);
  const auto filtered_child_id = filtered_child.id();
  CHECK(filtered_child.smart_filter_stack() != nullptr);

  patchy::Layer folder(document.allocate_layer_id(), "Filtered Tree",
                       patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  folder.metadata()[patchy::kLayerMetadataGroupExpanded] = "true";
  folder.add_child(std::move(filtered_child));
  document.add_layer(std::move(folder));
  document.set_active_layer(folder_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(select_named_layer(window, QStringLiteral("Filtered Tree")) ==
        folder_id);

  const auto& before_document = std::as_const(document);
  const auto* before_folder = before_document.find_layer(folder_id);
  const auto* before_child = before_document.find_layer(filtered_child_id);
  CHECK(before_folder != nullptr &&
        before_folder->kind() == patchy::LayerKind::Group);
  CHECK(before_folder->children().size() == 1U);
  CHECK(before_child != nullptr && before_child->smart_filter_stack() != nullptr);
  CHECK(before_child->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  const auto source_uuid = patchy::smart_object_source_uuid(*before_child);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*before_child);
  const auto placement = patchy::smart_object_placement_from_layer(*before_child);
  const auto child_pixels = before_child->pixels();
  const auto child_bounds = before_child->bounds();
  const auto child_mask = before_child->smart_filter_stack()->mask;
  const auto* before_record =
      before_document.metadata().smart_filter_effects.find_unique(placed_uuid);
  CHECK(before_record != nullptr && before_record->semantic_supported());
  const auto record_storage = before_record->raw_storage;
  const auto record_span =
      patchy::psd::raw_filter_effects_record_body(*before_record);
  const std::vector<std::uint8_t> record_body(record_span.begin(),
                                               record_span.end());
  const auto root_count = before_document.layers().size();
  const auto effect_record_count = smart_filter_effect_record_count(before_document);
  const auto composite = patchy::ui::qimage_from_document(before_document, true);
  const auto undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto was_modified =
      patchy::ui::MainWindowTestAccess::active_session_is_modified(window);

  auto* convert = require_action(window, "layerConvertSmartObjectAction");
  CHECK(convert->isEnabled());
  convert->trigger();
  QApplication::processEvents();

  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("Smart Objects with Smart Filters cannot be wrapped in another Smart Object yet")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_depth);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window) ==
        was_modified);

  const auto& after_document = std::as_const(document);
  CHECK(after_document.layers().size() == root_count);
  CHECK(after_document.active_layer_id() == folder_id);
  const auto* after_folder = after_document.find_layer(folder_id);
  const auto* after_child = after_document.find_layer(filtered_child_id);
  CHECK(after_folder != nullptr &&
        after_folder->kind() == patchy::LayerKind::Group);
  CHECK(!patchy::layer_is_smart_object(*after_folder));
  CHECK(after_folder->children().size() == 1U);
  CHECK(after_child != nullptr && after_child->smart_filter_stack() != nullptr);
  CHECK(after_child->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(patchy::smart_object_source_uuid(*after_child) == source_uuid);
  CHECK(patchy::smart_object_placed_uuid(*after_child) == placed_uuid);
  CHECK(smart_object_placements_equal(
      patchy::smart_object_placement_from_layer(*after_child), placement));
  CHECK(filter_rect_equal(after_child->bounds(), child_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(after_child->pixels(), child_pixels));
  CHECK(filter_rect_equal(after_child->smart_filter_stack()->mask.bounds,
                          child_mask.bounds));
  CHECK(patchy::ui::pixel_buffers_equal(
      after_child->smart_filter_stack()->mask.pixels, child_mask.pixels));
  CHECK(smart_filter_effect_record_count(after_document) ==
        effect_record_count);
  const auto* after_record =
      after_document.metadata().smart_filter_effects.find_unique(placed_uuid);
  CHECK(after_record != nullptr && after_record->semantic_supported());
  CHECK(after_record->raw_storage == record_storage);
  const auto after_record_body =
      patchy::psd::raw_filter_effects_record_body(*after_record);
  CHECK(after_record_body.size() == record_body.size());
  CHECK(std::equal(after_record_body.begin(), after_record_body.end(),
                   record_body.begin()));
  CHECK(patchy::ui::qimage_from_document(after_document, true) == composite);
}

void ui_smart_filter_native_integrity_guards_reject_destructive_actions() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-model.psd")
          .wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto layer_id = select_named_layer(
      window, QStringLiteral("Layer mask plus Smart Filter mask"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->mask().has_value());
  CHECK(filtered->smart_filter_stack() != nullptr);
  CHECK(filtered->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_selection_feather_radius(0);
  canvas->set_selection_antialias(false);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(5, 5)),
       canvas->widget_position_for_document_point(QPoint(27, 27)));
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().isEmpty());

  auto expected_bytes = patchy::psd::DocumentIo::write_layered_rgb8(
      std::as_const(document));
  auto expected_undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto expected_modified =
      patchy::ui::MainWindowTestAccess::active_session_is_modified(window);
  auto verify_unchanged = [&] {
    CHECK(patchy::psd::DocumentIo::write_layered_rgb8(
              std::as_const(document)) == expected_bytes);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
          expected_undo_depth);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window) ==
          expected_modified);
  };
  const auto trigger_without_dialog = [&](QAction* action) {
    bool saw_dialog = false;
    QTimer::singleShot(0, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog != nullptr && dialog->isVisible()) {
          saw_dialog = true;
          dialog->reject();
        }
      }
    });
    action->trigger();
    QApplication::processEvents();
    CHECK(!saw_dialog);
    verify_unchanged();
  };

  accept_image_size_dialog(document.width() + 8, document.height() + 6);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  verify_unchanged();
  accept_canvas_size_dialog(document.width() + 10, document.height() + 8);
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  verify_unchanged();

  trigger_without_dialog(require_action(window, "imageCropToSelectionAction"));
  trigger_without_dialog(require_action(window, "imageRotateClockwiseAction"));
  trigger_without_dialog(require_action(window, "imageRotateCounterclockwiseAction"));
  trigger_without_dialog(
      require_hotkey_action(window, QStringLiteral("layer.flip_horizontal")));
  trigger_without_dialog(
      require_hotkey_action(window, QStringLiteral("layer.flip_vertical")));
  trigger_without_dialog(require_action(window, "layerFillForegroundAction"));
  trigger_without_dialog(require_action(window, "layerFillBackgroundAction"));
  bool gallery_opened = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("filterGalleryDialog")));
    CHECK(dialog != nullptr);
    gallery_opened = true;
    dialog->reject();
  });
  require_action(window, "filterGalleryAction")->trigger();
  QApplication::processEvents();
  CHECK(gallery_opened);
  verify_unchanged();
  trigger_without_dialog(require_action(window, "imageAdjustLevelsAction"));
  trigger_without_dialog(require_action(window, "imageAdjustInvertAction"));
  trigger_without_dialog(require_action(window, "editCutAction"));
  trigger_without_dialog(require_action(window, "layerViaCutAction"));
  trigger_without_dialog(require_action(window, "layerApplyMaskAction"));
  trigger_without_dialog(require_action(window, "editStrokeSelectionAction"));
  trigger_without_dialog(require_action(window, "imageModeIndexedAction"));

  // A render/cache failure during Free Transform must roll back the complete
  // native state and must not leave a history entry behind.
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  expected_bytes = patchy::psd::DocumentIo::write_layered_rgb8(
      std::as_const(document));
  expected_undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  canvas->set_smart_object_transform_render_callback(
      [](patchy::LayerId) { return false; });
  CHECK(canvas->begin_free_transform());
  const auto controls = canvas->transform_controls_state();
  CHECK(controls.has_value());
  CHECK(canvas->set_transform_controls_state(
      controls->reference_position + QPointF(5.0, 3.0), 125.0, 125.0,
      0.0));
  canvas->finish_free_transform();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("Could not rebuild the Smart Filter preview and cache")));
  verify_unchanged();

  // Palette snapping has a separate execution path from conversion. Enable the
  // mode directly as test setup, then prove neither layer nor whole-image snap
  // can rewrite a Smart Object's derived pixels.
  patchy::DocumentPaletteEditing palette_editing;
  palette_editing.palette.colors = {
      patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255}};
  palette_editing.alpha_threshold = 128;
  palette_editing.palette_revision = 0x7A110001ULL;
  document.palette_editing() = std::move(palette_editing);
  patchy::sync_document_indexed_palette(document);
  canvas->document_changed();
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  patchy::ui::MainWindowTestAccess::update_document_action_state(window);
  QApplication::processEvents();
  expected_bytes = patchy::psd::DocumentIo::write_layered_rgb8(
      std::as_const(document));
  expected_undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  trigger_without_dialog(
      require_action(window, "imageSnapLayerToPaletteAction"));
  trigger_without_dialog(
      require_action(window, "imageSnapImageToPaletteAction"));
}

void ui_unsupported_smart_filter_guards_preserve_photoshop_preview() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  auto unsupported_document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-model.psd"));
  auto unsupported_layer = std::find_if(
      unsupported_document.layers().begin(),
      unsupported_document.layers().end(), [](const patchy::Layer& layer) {
        return layer.name() == "Applied Median then Gaussian";
      });
  CHECK(unsupported_layer != unsupported_document.layers().end());
  constexpr std::array<std::uint8_t, 4> median_id{'M', 'd', 'n', ' '};
  constexpr std::array<std::uint8_t, 4> unknown_id{'Z', 'Z', 'Z', 'Z'};
  std::size_t replacements = 0U;
  for (auto& block : unsupported_layer->unknown_psd_blocks()) {
    if (block.key != "SoLd" && block.key != "SoLE") {
      continue;
    }
    auto begin = block.payload.begin();
    while (begin != block.payload.end()) {
      const auto found = std::search(begin, block.payload.end(),
                                     median_id.begin(), median_id.end());
      if (found == block.payload.end()) {
        break;
      }
      std::copy(unknown_id.begin(), unknown_id.end(), found);
      ++replacements;
      begin = found + static_cast<std::ptrdiff_t>(unknown_id.size());
    }
  }
  CHECK(replacements >= 1U);
  ensure_artifact_dir();
  const auto unsupported_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_unsupported_smart_filter_descriptor.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(unsupported_document,
                                                    unsupported_path);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(unsupported_path.wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto layer_id = select_named_layer(
      window, QStringLiteral("Applied Median then Gaussian"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() != nullptr);
  CHECK(filtered->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Unsupported);
  CHECK(patchy::smart_object_lock_reason(*filtered) == "filters");

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(6, 6)),
       canvas->widget_position_for_document_point(QPoint(24, 24)));
  QApplication::processEvents();
  const auto expected_bytes = patchy::psd::DocumentIo::write_layered_rgb8(
      std::as_const(document));
  const auto expected_undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto expected_modified =
      patchy::ui::MainWindowTestAccess::active_session_is_modified(window);
  const std::array<QAction*, 9> guarded_actions{{
      require_action(window, "imageRotateClockwiseAction"),
      require_hotkey_action(window, QStringLiteral("layer.flip_horizontal")),
      require_action(window, "layerFillForegroundAction"),
      require_action(window, "filterGalleryAction"),
      require_action(window, "imageAdjustInvertAction"),
      require_action(window, "editCutAction"),
      require_action(window, "layerViaCutAction"),
      require_action(window, "editStrokeSelectionAction"),
      require_action(window, "imageModeIndexedAction"),
  }};
  for (auto* action : guarded_actions) {
    bool saw_dialog = false;
    QTimer::singleShot(0, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog != nullptr && dialog->isVisible()) {
          saw_dialog = true;
          dialog->reject();
        }
      }
    });
    action->trigger();
    QApplication::processEvents();
    CHECK(!saw_dialog);
    CHECK(patchy::psd::DocumentIo::write_layered_rgb8(
              std::as_const(document)) == expected_bytes);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
          expected_undo_depth);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window) ==
          expected_modified);
  }
}

void ui_smart_filter_linked_external_add_edit_toggle_lock_and_delete() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  QTemporaryDir temporary;
  CHECK(temporary.isValid());

  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  const auto linked_path = temporary.filePath(QStringLiteral("linked.png"));
  convert_fixture_source_to_external(window, layer_id, linked_path);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto* linked = std::as_const(document).find_layer(layer_id);
  CHECK(linked != nullptr && patchy::layer_is_smart_object(*linked));
  CHECK(patchy::smart_object_lock_reason(*linked) == "external");
  const auto* source = std::as_const(document).metadata().smart_objects.find(
      patchy::smart_object_source_uuid(*linked));
  CHECK(source != nullptr &&
        source->kind == patchy::SmartObjectSourceKind::ExternalFile);
  CHECK(QFileInfo::exists(linked_path));
  const auto parent_dir = QFileInfo(
      patchy::ui::MainWindowTestAccess::active_session_path(window))
                              .absolutePath();
  const auto unfiltered = patchy::ui::render_smart_object_unfiltered_layer_preview(
      std::as_const(document), *linked, canvas->transform_interpolation(),
      parent_dir);
  CHECK(unfiltered.has_value());
  const auto unfiltered_pixels = unfiltered->pixels;
  const auto unfiltered_bounds = unfiltered->bounds;
  const auto original_record_count = smart_filter_effect_record_count(document);
  auto* gaussian = require_action(
      window, "filterAction_patchy_filters_gaussian_blur");
  CHECK(gaussian->isEnabled());

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(1.5);
    applied = true;
    dialog->accept();
  });
  gaussian->trigger();
  QApplication::processEvents();
  CHECK(applied);

  linked = std::as_const(document).find_layer(layer_id);
  CHECK(linked != nullptr && linked->smart_filter_stack() != nullptr);
  CHECK(patchy::smart_object_lock_reason(*linked) == "external");
  const auto* stack = linked->smart_filter_stack();
  CHECK(stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U && stack->entries.front().enabled);
  const auto* radius = std::get_if<patchy::GaussianBlurSmartFilter>(
      &stack->entries.front().parameters);
  CHECK(radius != nullptr &&
        std::abs(radius->radius_pixels - 1.5) < 0.000001);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*linked);
  const auto* cache = std::as_const(document)
                          .metadata()
                          .smart_filter_effects.find_unique(placed_uuid);
  CHECK(cache != nullptr && cache->semantic_supported());
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* item = require_layer_item(*list, QStringLiteral("small"));
    auto* row = list->itemWidget(item);
    CHECK(row != nullptr);
    return row;
  };
  auto* row = active_row();
  auto* entry_visibility = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterVisibilityButton"));
  auto* stack_visibility = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFiltersVisibilityButton"));
  auto* edit = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  auto* remove = row->findChild<QAction*>(
      QStringLiteral("layerSmartFilterDeleteAction"));
  CHECK(entry_visibility != nullptr && entry_visibility->isEnabled());
  CHECK(stack_visibility != nullptr && stack_visibility->isEnabled());
  CHECK(edit != nullptr && edit->isEnabled());
  CHECK(remove != nullptr && remove->isEnabled());

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* edit_radius =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(edit_radius != nullptr);
    CHECK(std::abs(edit_radius->value() - 1.5) < 0.000001);
    edit_radius->setValue(2.7);
    edited = true;
    dialog->accept();
  });
  edit->click();
  CHECK(process_events_until([&] { return edited; }, 5000));
  process_events_for(50);
  linked = std::as_const(document).find_layer(layer_id);
  CHECK(linked != nullptr && linked->smart_filter_stack() != nullptr);
  radius = std::get_if<patchy::GaussianBlurSmartFilter>(
      &linked->smart_filter_stack()->entries.front().parameters);
  CHECK(radius != nullptr &&
        std::abs(radius->radius_pixels - 2.7) < 0.000001);
  auto disabled_stack = *linked->smart_filter_stack();
  disabled_stack.entries.front().enabled = false;
  const auto disabled_preview = patchy::ui::render_smart_object_layer_preview(
      std::as_const(document), *linked, canvas->transform_interpolation(),
      &disabled_stack, parent_dir);
  CHECK(disabled_preview.has_value());

  entry_visibility = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterVisibilityButton"));
  CHECK(entry_visibility != nullptr && entry_visibility->isEnabled());
  entry_visibility->click();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() != nullptr &&
           !current->smart_filter_stack()->entries.front().enabled;
  }));
  linked = std::as_const(document).find_layer(layer_id);
  CHECK(linked != nullptr);
  CHECK(filter_rect_equal(linked->bounds(), disabled_preview->rendered.bounds));
  CHECK(patchy::ui::pixel_buffers_equal(
      linked->pixels(), disabled_preview->rendered.pixels));
  entry_visibility = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterVisibilityButton"));
  entry_visibility->click();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() != nullptr &&
           current->smart_filter_stack()->entries.front().enabled;
  }));

  auto* mutable_linked = document.find_layer(layer_id);
  CHECK(mutable_linked != nullptr);
  patchy::set_layer_lock_flags(*mutable_linked,
                               patchy::kLayerLockImagePixels);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  row = active_row();
  entry_visibility = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterVisibilityButton"));
  stack_visibility = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFiltersVisibilityButton"));
  edit = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  remove = row->findChild<QAction*>(
      QStringLiteral("layerSmartFilterDeleteAction"));
  CHECK(entry_visibility != nullptr && !entry_visibility->isEnabled());
  CHECK(stack_visibility != nullptr && !stack_visibility->isEnabled());
  CHECK(edit != nullptr && !edit->isEnabled());
  CHECK(remove != nullptr && !remove->isEnabled());
  const auto locked_pixels =
      std::as_const(document).find_layer(layer_id)->pixels();
  const auto locked_bounds =
      std::as_const(document).find_layer(layer_id)->bounds();
  const auto locked_undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool locked_action_opened_dialog = false;
  QTimer::singleShot(0, [&] {
    if (auto* dialog = qobject_cast<QDialog*>(
            find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
        dialog != nullptr) {
      locked_action_opened_dialog = true;
      dialog->reject();
    }
  });
  gaussian->trigger();
  QApplication::processEvents();
  CHECK(!locked_action_opened_dialog);
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("pixels are locked"), Qt::CaseInsensitive));
  entry_visibility->click();
  edit->click();
  remove->trigger();
  QApplication::processEvents();
  linked = std::as_const(document).find_layer(layer_id);
  CHECK(linked != nullptr && linked->smart_filter_stack() != nullptr);
  CHECK(linked->smart_filter_stack()->entries.front().enabled);
  radius = std::get_if<patchy::GaussianBlurSmartFilter>(
      &linked->smart_filter_stack()->entries.front().parameters);
  CHECK(radius != nullptr &&
        std::abs(radius->radius_pixels - 2.7) < 0.000001);
  CHECK(filter_rect_equal(linked->bounds(), locked_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(linked->pixels(), locked_pixels));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        locked_undo_depth);

  mutable_linked = document.find_layer(layer_id);
  patchy::set_layer_lock_flags(*mutable_linked, patchy::kLayerLockNone);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  remove = active_row()->findChild<QAction*>(
      QStringLiteral("layerSmartFilterDeleteAction"));
  CHECK(remove != nullptr && remove->isEnabled());
  remove->trigger();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() == nullptr;
  }));
  linked = std::as_const(document).find_layer(layer_id);
  CHECK(linked != nullptr);
  CHECK(filter_rect_equal(linked->bounds(), unfiltered_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(linked->pixels(), unfiltered_pixels));
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(placed_uuid) == nullptr);
  CHECK(patchy::smart_object_lock_reason(*linked) == "external");
}

void ui_pixel_mosaic_direct_action_appends_editable_smart_filter() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original_layer = std::as_const(document).find_layer(layer_id);
  CHECK(original_layer != nullptr &&
        original_layer->smart_filter_stack() == nullptr);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  accept_filter_dialog({{QStringLiteral("filterCellSizeSpin"), 12}});
  require_action(window, "filterAction_patchy_filters_pixelate")->trigger();
  QApplication::processEvents();

  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && patchy::layer_is_smart_object(*filtered));
  const auto* stack = filtered->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::Mosaic);
  CHECK(entry.native_name == "Mosaic...");
  CHECK(entry.native_class_id == "Msc ");
  const auto* mosaic =
      std::get_if<patchy::MosaicSmartFilter>(&entry.parameters);
  CHECK(mosaic != nullptr && mosaic->cell_size_pixels == 12);
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("Mosaic")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && restored->smart_filter_stack() == nullptr);
}

void ui_emboss_direct_action_appends_editable_smart_filter() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original_layer = std::as_const(document).find_layer(layer_id);
  CHECK(original_layer != nullptr &&
        original_layer->smart_filter_stack() == nullptr);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  accept_filter_dialog({{QStringLiteral("filterAngleSpin"), -22},
                        {QStringLiteral("filterHeightSpin"), 5},
                        {QStringLiteral("filterDepthSpin"), 250}});
  require_action(window, "filterAction_patchy_filters_emboss")->trigger();
  QApplication::processEvents();

  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && patchy::layer_is_smart_object(*filtered));
  const auto* stack = filtered->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::Emboss);
  CHECK(entry.native_name == "Emboss...");
  CHECK(entry.native_class_id == "Embs");
  const auto* emboss =
      std::get_if<patchy::EmbossSmartFilter>(&entry.parameters);
  CHECK(emboss != nullptr && emboss->angle_degrees == -22 &&
        emboss->height_pixels == 5 && emboss->amount_percent == 250);
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("Emboss")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && restored->smart_filter_stack() == nullptr);
}

void ui_box_blur_direct_action_appends_editable_smart_filter() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original_layer = std::as_const(document).find_layer(layer_id);
  CHECK(original_layer != nullptr &&
        original_layer->smart_filter_stack() == nullptr);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    CHECK(std::abs(radius->value() - 1.0) < 0.000001);
    CHECK(radius->maximum() == 2000.0);
    radius->setValue(5.0);
    drove_dialog = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_box_blur")->trigger();
  QApplication::processEvents();
  CHECK(drove_dialog);

  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && patchy::layer_is_smart_object(*filtered));
  const auto* stack = filtered->smart_filter_stack();
  CHECK(stack != nullptr &&
        stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::BoxBlur);
  CHECK(entry.native_name == "Box Blur...");
  CHECK(entry.native_class_id == "boxblur");
  const auto* box =
      std::get_if<patchy::BoxBlurSmartFilter>(&entry.parameters);
  CHECK(box != nullptr && std::abs(box->radius_pixels - 5.0) < 0.000001);
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("Box Blur")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && restored->smart_filter_stack() == nullptr);
}

void ui_smart_object_direct_unsupported_filter_offers_rasterize() {
  patchy::ui::MainWindow window;
  show_window(window);

  patchy::Document built(48, 36, patchy::PixelFormat::rgba8());
  patchy::Layer artwork(built.allocate_layer_id(), "Artwork",
                        solid_pixels(20, 16, patchy::PixelFormat::rgba8(),
                                     QColor(220, 40, 30, 255)));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{9, 8, 20, 16};
  artwork.set_bounds(original_bounds);
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);
  window.add_document_session(std::move(built),
                              QStringLiteral("Rasterize Prompt"));
  QApplication::processEvents();

  require_action(window, "filterConvertForSmartFiltersAction")->trigger();
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* converted = std::as_const(document).find_layer(layer_id);
  CHECK(converted != nullptr && patchy::layer_is_smart_object(*converted));
  const auto original_pixels = converted->pixels();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  const auto sepia_action_name = patchy::ui::filter_action_object_name(
      QStringLiteral("patchy.filters.sepia"));

  // Cancel keeps the Smart Object untouched (no undo entry, no pixel change).
  bool cancelled_prompt = false;
  QTimer::singleShot(0, [&] {
    auto* warning = qobject_cast<QMessageBox*>(find_top_level_dialog(
        QStringLiteral("filterRasterizeMessageBox")));
    CHECK(warning != nullptr);
    auto* cancel = warning->button(QMessageBox::Cancel);
    CHECK(cancel != nullptr);
    cancelled_prompt = true;
    cancel->click();
  });
  require_action(window, sepia_action_name.toUtf8().constData())->trigger();
  QApplication::processEvents();
  CHECK(cancelled_prompt);
  const auto* still_smart = std::as_const(document).find_layer(layer_id);
  CHECK(still_smart != nullptr && patchy::layer_is_smart_object(*still_smart));
  CHECK(patchy::ui::pixel_buffers_equal(still_smart->pixels(),
                                        original_pixels));
  CHECK(window.statusBar()->currentMessage().startsWith(
      QStringLiteral("Cancelled")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before);

  // Yes rasterizes and applies the filter destructively as one undo step.
  bool accepted_prompt = false;
  QTimer::singleShot(0, [&] {
    auto* warning = qobject_cast<QMessageBox*>(find_top_level_dialog(
        QStringLiteral("filterRasterizeMessageBox")));
    CHECK(warning != nullptr);
    auto* yes = warning->button(QMessageBox::Yes);
    CHECK(yes != nullptr);
    accepted_prompt = true;
    accept_filter_dialog();
    yes->click();
  });
  require_action(window, sepia_action_name.toUtf8().constData())->trigger();
  QApplication::processEvents();
  CHECK(accepted_prompt);
  const auto* rasterized = std::as_const(document).find_layer(layer_id);
  CHECK(rasterized != nullptr &&
        !patchy::layer_is_smart_object(*rasterized));
  CHECK(rasterized->smart_filter_stack() == nullptr);
  CHECK(!patchy::ui::pixel_buffers_equal(rasterized->pixels(),
                                         original_pixels));
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("Rasterized Smart Object")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_part2() {
  return {
      {"ui_smart_filter_gallery_native_recipe_applies_atomically",
       ui_smart_filter_gallery_native_recipe_applies_atomically},
      {"ui_smart_filter_move_drag_and_nudge_rerender_cache_and_roundtrip",
       ui_smart_filter_move_drag_and_nudge_rerender_cache_and_roundtrip},
      {"ui_convert_to_smart_object_rejects_tree_containing_smart_filter",
       ui_convert_to_smart_object_rejects_tree_containing_smart_filter},
      {"ui_smart_filter_native_integrity_guards_reject_destructive_actions",
       ui_smart_filter_native_integrity_guards_reject_destructive_actions},
      {"ui_unsupported_smart_filter_guards_preserve_photoshop_preview",
       ui_unsupported_smart_filter_guards_preserve_photoshop_preview},
      {"ui_pixel_mosaic_direct_action_appends_editable_smart_filter",
       ui_pixel_mosaic_direct_action_appends_editable_smart_filter},
      {"ui_emboss_direct_action_appends_editable_smart_filter",
       ui_emboss_direct_action_appends_editable_smart_filter},
      {"ui_box_blur_direct_action_appends_editable_smart_filter",
       ui_box_blur_direct_action_appends_editable_smart_filter},
      {"ui_smart_object_direct_unsupported_filter_offers_rasterize",
       ui_smart_object_direct_unsupported_filter_offers_rasterize},
      {"ui_smart_filter_linked_external_add_edit_toggle_lock_and_delete",
       ui_smart_filter_linked_external_add_edit_toggle_lock_and_delete},
  };
}
