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

void ui_smart_object_import_badges_protects_and_rasterizes() {
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd").wstring());
  CHECK(QFileInfo::exists(path));

  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);

  // Import notes land in the status bar (no popup by default).
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("smart object")));

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const patchy::Layer* smart_layer = nullptr;
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.name() == "small") {
      smart_layer = &layer;
    }
  }
  CHECK(smart_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*smart_layer));
  CHECK(patchy::smart_object_lock_reason(*smart_layer).empty());
  const auto smart_layer_id = smart_layer->id();
  document.set_active_layer(smart_layer_id);
  QApplication::processEvents();

  // The layer row shows the smart-object badge button (embedded variant).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  bool badge_found = false;
  for (int i = 0; i < layer_list->count(); ++i) {
    auto* badge = layer_list->itemWidget(layer_list->item(i))
                      ->findChild<QToolButton*>(QStringLiteral("layerSmartObjectBadgeButton"));
    if (badge != nullptr) {
      CHECK(!badge->property("smartObjectLinked").toBool());
      badge_found = true;
    }
  }
  CHECK(badge_found);

  // A brush stroke is refused with an explanation; the composite stays untouched.
  // Use the ACTIVE document's canvas, not findChild's first hit (each tab owns one).
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 20, 20));
  const QPoint stroke_document_point(48, 48);  // inside the placed content
  const auto before_stroke = canvas_pixel(*canvas, stroke_document_point);
  const auto stroke_widget_point = canvas->widget_position_for_document_point(stroke_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, stroke_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, stroke_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Rasterize")));
  CHECK(color_close(canvas_pixel(*canvas, stroke_document_point), before_stroke, 0));

  // Rasterize keeps the preview pixels but demotes the layer to a plain pixel layer.
  const auto pixels_before = smart_layer->pixels();
  require_action(window, "layerRasterizeAction")->trigger();
  QApplication::processEvents();
  const auto* rasterized = document.find_layer(smart_layer_id);
  CHECK(rasterized != nullptr);
  CHECK(!patchy::layer_is_smart_object(*rasterized));
  CHECK(std::none_of(rasterized->unknown_psd_blocks().begin(), rasterized->unknown_psd_blocks().end(),
                     [](const patchy::UnknownPsdBlock& block) { return block.key == "SoLd" || block.key == "PlLd"; }));
  CHECK(rasterized->pixels().width() == pixels_before.width());
  CHECK(rasterized->pixels().height() == pixels_before.height());
  // Paintable now: the same stroke lands.
  send_mouse(*canvas, QEvent::MouseButtonPress, stroke_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, stroke_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, stroke_document_point), QColor(230, 20, 20), 40));

  // Undo the confirmation stroke, then the rasterize: the smart object comes back
  // (whole-document snapshots carry the metadata).
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = document.find_layer(smart_layer_id);
  CHECK(restored != nullptr);
  CHECK(patchy::layer_is_smart_object(*restored));
}

std::vector<std::uint8_t> smart_filter_cache_prefix(
    const patchy::SmartFilterEffectsRecord& record) {
  const auto body = patchy::psd::raw_filter_effects_record_body(record);
  patchy::psd::BigEndianReader reader(body);
  const auto id_length = reader.read_u8();
  CHECK(id_length <= reader.remaining());
  reader.skip(id_length);
  CHECK(reader.read_u32() == 1U);
  const auto cache_length = reader.read_u64();
  CHECK(cache_length <= reader.remaining());
  reader.skip(static_cast<std::size_t>(cache_length));
  return std::vector<std::uint8_t>(
      body.begin(),
      body.begin() + static_cast<std::ptrdiff_t>(reader.position()));
}

std::vector<std::uint8_t> smart_filter_record_body_copy(
    const patchy::SmartFilterEffectsRecord& record) {
  const auto body = patchy::psd::raw_filter_effects_record_body(record);
  return std::vector<std::uint8_t>(body.begin(), body.end());
}

patchy::PixelBuffer materialize_smart_filter_mask_for_ui_test(
    const patchy::SmartFilterMask& mask, std::int32_t document_width,
    std::int32_t document_height) {
  patchy::PixelBuffer result(document_width, document_height,
                             patchy::PixelFormat::gray8());
  result.clear(mask.default_color);
  if (mask.pixels.empty() || mask.pixels.format() != patchy::PixelFormat::gray8()) {
    return result;
  }
  const auto left = std::max<std::int32_t>(0, mask.bounds.x);
  const auto top = std::max<std::int32_t>(0, mask.bounds.y);
  const auto right = std::min<std::int32_t>(
      document_width, mask.bounds.x + mask.pixels.width());
  const auto bottom = std::min<std::int32_t>(
      document_height, mask.bounds.y + mask.pixels.height());
  for (auto y = top; y < bottom; ++y) {
    for (auto x = left; x < right; ++x) {
      result.pixel(x, y)[0] =
          mask.pixels.pixel(x - mask.bounds.x, y - mask.bounds.y)[0];
    }
  }
  return result;
}

// Duplicate runs through MainWindow's real layer action. The clone keeps the
// shared Smart Object source but receives a fresh per-instance SoLd `placed`
// id and a distinct FEid record backed by the original immutable cache bytes.
void ui_smart_filter_duplicate_rekeys_native_cache() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  open_smart_filter_instances_fixture(window);

  const auto source_id = select_named_layer(
      window, QString::fromLatin1(kSmartFilterInstanceAName));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* source_for_ids = document.find_layer(source_id);
  CHECK(source_for_ids != nullptr);
  patchy::set_photoshop_layer_id(
      *source_for_ids, std::numeric_limits<std::uint32_t>::max());
  const auto other_it = std::find_if(
      std::as_const(document).layers().begin(),
      std::as_const(document).layers().end(), [](const patchy::Layer& layer) {
        return layer.name() == kSmartFilterInstanceBName;
      });
  CHECK(other_it != std::as_const(document).layers().end());
  auto* other_for_ids = document.find_layer(other_it->id());
  CHECK(other_for_ids != nullptr);
  patchy::set_photoshop_layer_id(*other_for_ids, 1U);
  const auto* source = std::as_const(document).find_layer(source_id);
  CHECK(source != nullptr);
  CHECK(patchy::layer_is_smart_object(*source));
  CHECK(source->smart_filter_stack() != nullptr);
  const auto source_uuid = patchy::smart_object_source_uuid(*source);
  const auto source_placed = patchy::smart_object_placed_uuid(*source);
  const auto source_native_layer_id = patchy::photoshop_layer_id(*source);
  CHECK(!source_uuid.empty());
  CHECK(!source_placed.empty());
  CHECK(source_native_layer_id.has_value());
  const auto* source_record =
      std::as_const(document).metadata().smart_filter_effects.find_unique(
          source_placed);
  CHECK(source_record != nullptr);
  const auto original_record_count = smart_filter_effect_record_count(document);
  const auto raw_storage = source_record->raw_storage;
  const auto raw_offset = source_record->raw_body_offset;
  const auto raw_length = source_record->raw_body_length;
  const auto original_placed = source_record->original_placed_uuid;

  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();

  const auto duplicate_id = select_named_layer(
      window,
      QString::fromLatin1(kSmartFilterInstanceAName) + QStringLiteral(" copy"));
  const auto* duplicate = std::as_const(document).find_layer(duplicate_id);
  CHECK(duplicate != nullptr);
  CHECK(patchy::layer_is_smart_object(*duplicate));
  CHECK(duplicate->smart_filter_stack() != nullptr);
  CHECK(patchy::smart_object_source_uuid(*duplicate) == source_uuid);
  const auto duplicate_placed = patchy::smart_object_placed_uuid(*duplicate);
  CHECK(!duplicate_placed.empty());
  CHECK(duplicate_placed != source_placed);
  const auto duplicate_native_layer_id =
      patchy::photoshop_layer_id(*duplicate);
  CHECK(duplicate_native_layer_id.has_value());
  CHECK(duplicate_native_layer_id != source_native_layer_id);
  CHECK(*duplicate_native_layer_id == 2U);
  CHECK(smart_filter_effect_record_count(document) == original_record_count + 1U);

  source_record =
      std::as_const(document).metadata().smart_filter_effects.find_unique(
          source_placed);
  const auto* duplicate_record =
      std::as_const(document).metadata().smart_filter_effects.find_unique(
          duplicate_placed);
  CHECK(source_record != nullptr);
  CHECK(duplicate_record != nullptr);
  CHECK(source_record != duplicate_record);
  CHECK(duplicate_record->placed_uuid == duplicate_placed);
  CHECK(duplicate_record->original_placed_uuid == original_placed);
  CHECK(duplicate_record->raw_storage == raw_storage);
  CHECK(duplicate_record->raw_body_offset == raw_offset);
  CHECK(duplicate_record->raw_body_length == raw_length);
  bool cache_is_adjacent_to_source = false;
  for (const auto& block :
       std::as_const(document).metadata().smart_filter_effects.blocks) {
    for (std::size_t index = 0; index + 1U < block.records.size(); ++index) {
      if (block.records[index].placed_uuid == source_placed &&
          block.records[index + 1U].placed_uuid == duplicate_placed) {
        cache_is_adjacent_to_source = true;
      }
    }
  }
  CHECK(cache_is_adjacent_to_source);
}

// Copy/Paste in the same document takes the clipboard adoption path rather
// than Duplicate Layer's clone-rekey path. It still needs Photoshop's adjacent
// FEid record layout, plus fresh placed and native layer ids.
void ui_smart_filter_same_document_paste_keeps_native_cache_adjacent() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  open_smart_filter_instances_fixture(window);

  const auto source_id = select_named_layer(
      window, QString::fromLatin1(kSmartFilterInstanceAName));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* source = std::as_const(document).find_layer(source_id);
  CHECK(source != nullptr);
  const auto source_placed = patchy::smart_object_placed_uuid(*source);
  const auto source_native_layer_id = patchy::photoshop_layer_id(*source);
  CHECK(!source_placed.empty());
  CHECK(source_native_layer_id.has_value());
  const auto expected_pasted_native_layer_id =
      patchy::next_photoshop_layer_id(document.layers());
  const auto original_record_count = smart_filter_effect_record_count(document);

  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  const auto pasted_id = select_named_layer(
      window,
      QString::fromLatin1(kSmartFilterInstanceAName) + QStringLiteral(" copy"));
  const auto* pasted = std::as_const(document).find_layer(pasted_id);
  CHECK(pasted != nullptr);
  CHECK(pasted->smart_filter_stack() != nullptr);
  const auto pasted_placed = patchy::smart_object_placed_uuid(*pasted);
  CHECK(!pasted_placed.empty());
  CHECK(pasted_placed != source_placed);
  const auto pasted_native_layer_id = patchy::photoshop_layer_id(*pasted);
  CHECK(pasted_native_layer_id.has_value());
  CHECK(*pasted_native_layer_id == expected_pasted_native_layer_id);
  CHECK(smart_filter_effect_record_count(document) == original_record_count + 1U);

  bool cache_is_adjacent_to_source = false;
  for (const auto& block :
       std::as_const(document).metadata().smart_filter_effects.blocks) {
    for (std::size_t index = 0; index + 1U < block.records.size(); ++index) {
      if (block.records[index].placed_uuid == source_placed &&
          block.records[index + 1U].placed_uuid == pasted_placed) {
        cache_is_adjacent_to_source = true;
      }
    }
  }
  CHECK(cache_is_adjacent_to_source);
}

// Cross-document copy/paste must carry the source plus the opaque native cache
// record. The destination creates its own `placed` id and adopts the raw FEid
// body without depending on the source document remaining open.
void ui_smart_filter_cross_document_paste_adopts_native_cache() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  open_smart_filter_instances_fixture(window);

  const auto source_id = select_named_layer(
      window, QString::fromLatin1(kSmartFilterInstanceBName));
  const auto& source_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* source = source_document.find_layer(source_id);
  CHECK(source != nullptr);
  CHECK(source->smart_filter_stack() != nullptr);
  const auto source_uuid = patchy::smart_object_source_uuid(*source);
  const auto source_placed = patchy::smart_object_placed_uuid(*source);
  const auto* source_record =
      source_document.metadata().smart_filter_effects.find_unique(source_placed);
  CHECK(source_record != nullptr);
  const auto raw_storage = source_record->raw_storage;
  const auto raw_offset = source_record->raw_body_offset;
  const auto raw_length = source_record->raw_body_length;
  const auto original_placed = source_record->original_placed_uuid;

  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  patchy::Document destination(96, 96, patchy::PixelFormat::rgba8());
  destination.add_pixel_layer(
      "Destination Background",
      solid_pixels(96, 96, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  window.add_document_session(std::move(destination),
                              QStringLiteral("Smart Filter Paste Target"));
  QApplication::processEvents();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  const auto pasted_id = select_named_layer(
      window,
      QString::fromLatin1(kSmartFilterInstanceBName) + QStringLiteral(" copy"));
  const auto& pasted_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* pasted = pasted_document.find_layer(pasted_id);
  CHECK(pasted != nullptr);
  CHECK(patchy::layer_is_smart_object(*pasted));
  CHECK(pasted->smart_filter_stack() != nullptr);
  CHECK(patchy::smart_object_source_uuid(*pasted) == source_uuid);
  CHECK(pasted_document.metadata().smart_objects.find(source_uuid) != nullptr);
  const auto pasted_placed = patchy::smart_object_placed_uuid(*pasted);
  CHECK(!pasted_placed.empty());
  CHECK(pasted_placed != source_placed);
  CHECK(patchy::photoshop_layer_id(*pasted).has_value());
  CHECK(smart_filter_effect_record_count(pasted_document) == 1U);

  const auto* pasted_record =
      pasted_document.metadata().smart_filter_effects.find_unique(pasted_placed);
  CHECK(pasted_record != nullptr);
  CHECK(pasted_record->placed_uuid == pasted_placed);
  CHECK(pasted_record->original_placed_uuid == original_placed);
  CHECK(pasted_record->raw_storage == raw_storage);
  CHECK(pasted_record->raw_body_offset == raw_offset);
  CHECK(pasted_record->raw_body_length == raw_length);
}

// Rasterize removes only the selected instance's native cache. Whole-document
// history snapshots restore both the Smart Object layer and that FEid record.
void ui_smart_filter_rasterize_and_undo_restore_native_cache() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  open_smart_filter_instances_fixture(window);

  const auto layer_id = select_named_layer(
      window, QString::fromLatin1(kSmartFilterInstanceAName));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr);
  CHECK(original->smart_filter_stack() != nullptr);
  const auto original_placed = patchy::smart_object_placed_uuid(*original);
  CHECK(!original_placed.empty());
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(original_placed) != nullptr);
  const auto original_record_count = smart_filter_effect_record_count(document);
  const auto undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  require_action(window, "layerRasterizeAction")->trigger();
  QApplication::processEvents();

  const auto* rasterized = std::as_const(document).find_layer(layer_id);
  CHECK(rasterized != nullptr);
  CHECK(!patchy::layer_is_smart_object(*rasterized));
  CHECK(rasterized->smart_filter_stack() == nullptr);
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(original_placed) == nullptr);
  CHECK(smart_filter_effect_record_count(document) + 1U == original_record_count);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_depth + 1U);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();

  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(patchy::layer_is_smart_object(*restored));
  CHECK(restored->smart_filter_stack() != nullptr);
  CHECK(patchy::smart_object_placed_uuid(*restored) == original_placed);
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(original_placed) != nullptr);
  CHECK(smart_filter_effect_record_count(document) == original_record_count);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_depth);
}

void ui_convert_for_smart_filters_action_converts_eligible_pixel_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* convert =
      require_action(window, "filterConvertForSmartFiltersAction");
  const auto* command = window.hotkey_registry().find_command(
      QStringLiteral("filter.convert_for_smart_filters"));
  CHECK(command != nullptr && command->action == convert);
  CHECK(command->default_shortcuts.isEmpty());

  patchy::Document built(48, 36, patchy::PixelFormat::rgba8());
  patchy::Layer artwork(
      built.allocate_layer_id(), "Artwork",
      solid_pixels(20, 16, patchy::PixelFormat::rgba8(),
                   QColor(220, 40, 30, 255)));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{9, 8, 20, 16};
  artwork.set_bounds(original_bounds);
  built.add_layer(std::move(artwork));
  built.add_pixel_layer(
      "Other Artwork",
      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  built.set_active_layer(layer_id);
  const auto before = patchy::ui::qimage_from_document(built, true);
  window.add_document_session(std::move(built),
                              QStringLiteral("Convert for Smart Filters"));
  QApplication::processEvents();

  auto* layer_list =
      window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() == 2);
  const auto select_layer = [&](const QString& name) {
    auto* item = require_layer_item(*layer_list, name);
    layer_list->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    item->setSelected(true);
    QApplication::processEvents();
  };
  select_layer(QStringLiteral("Artwork"));
  CHECK(convert->isEnabled());
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  convert->trigger();
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* converted = std::as_const(document).find_layer(layer_id);
  CHECK(converted != nullptr);
  CHECK(patchy::layer_is_smart_object(*converted));
  CHECK(patchy::smart_object_lock_reason(*converted).empty());
  CHECK(converted->smart_filter_stack() == nullptr);
  CHECK(filter_rect_equal(converted->bounds(), original_bounds));
  CHECK(patchy::ui::qimage_from_document(document, true) == before);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  CHECK(!convert->isEnabled());

  select_layer(QStringLiteral("Other Artwork"));
  CHECK(convert->isEnabled());
  select_layer(QStringLiteral("Artwork"));
  CHECK(!convert->isEnabled());

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::qimage_from_document(document, true) == before);
  CHECK(convert->isEnabled());

  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  const auto* redone = std::as_const(document).find_layer(layer_id);
  CHECK(redone != nullptr && patchy::layer_is_smart_object(*redone));
  CHECK(!convert->isEnabled());

  require_action(window, "layerRasterizeAction")->trigger();
  QApplication::processEvents();
  const auto* rasterized = std::as_const(document).find_layer(layer_id);
  CHECK(rasterized != nullptr && !patchy::layer_is_smart_object(*rasterized));
  CHECK(convert->isEnabled());
}

void ui_smart_filter_gaussian_dialog_mask_rows_edit_toggle_delete() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto* original_layer = std::as_const(document).find_layer(layer_id);
  CHECK(original_layer != nullptr);
  CHECK(original_layer->smart_filter_stack() == nullptr);
  const auto original_pixels = original_layer->pixels();
  const auto original_bounds = original_layer->bounds();
  const auto original_record_count = smart_filter_effect_record_count(document);

  patchy::PixelBuffer selection(document.width(), document.height(),
                                patchy::PixelFormat::gray8());
  selection.clear(0);
  const auto left = document.width() / 4;
  const auto right = document.width() * 3 / 4;
  const auto top = document.height() / 4;
  const auto bottom = document.height() * 3 / 4;
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      selection.pixel(x, y)[0] =
          static_cast<std::uint8_t>(x == left ? 128 : 255);
    }
  }
  canvas->replace_selection_from_grayscale(
      selection, QStringLiteral("Smart Filter mask selection"));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_has_partial_alpha());
  const auto expected_mask = canvas->selection_as_grayscale();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified_before =
      patchy::ui::MainWindowTestAccess::active_session_is_modified(window);
  auto* gaussian = require_action(
      window, "filterAction_patchy_filters_gaussian_blur");

  bool saw_fractional_cancel_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterRadiusSpin"));
    auto* radius_slider =
        dialog->findChild<QSlider*>(QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr);
    CHECK(radius_slider != nullptr);
    CHECK(std::abs(radius->value() - 2.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(radius->maximum() == 12.0);
    CHECK(radius_slider->maximum() == 1190);
    radius->setValue(12.0);
    CHECK(std::abs(radius->value() - 12.0) < 0.000001);
    CHECK(radius_slider->value() == radius_slider->maximum());
    radius->setValue(2.5);
    CHECK(process_events_until(
        [&] {
          const auto* preview =
              std::as_const(document).find_layer(layer_id);
          return preview != nullptr &&
                 (!filter_rect_equal(preview->bounds(), original_bounds) ||
                  !patchy::ui::pixel_buffers_equal(preview->pixels(),
                                                   original_pixels));
        },
        5000));
    saw_fractional_cancel_preview = true;
    dialog->reject();
  });
  gaussian->trigger();
  process_events_for(100);
  CHECK(saw_fractional_cancel_preview);
  {
    const auto* cancelled = std::as_const(document).find_layer(layer_id);
    CHECK(cancelled != nullptr);
    CHECK(cancelled->smart_filter_stack() == nullptr);
    CHECK(filter_rect_equal(cancelled->bounds(), original_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(cancelled->pixels(),
                                          original_pixels));
  }
  CHECK(smart_filter_effect_record_count(document) == original_record_count);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window) ==
        modified_before);

  bool applied_fractional_radius = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(2.5);
    applied_fractional_radius = true;
    dialog->accept();
  });
  gaussian->trigger();
  QApplication::processEvents();
  CHECK(applied_fractional_radius);

  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && filtered->smart_filter_stack() != nullptr);
  const auto* stack = filtered->smart_filter_stack();
  CHECK(stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->enabled && stack->entries.size() == 1U);
  CHECK(stack->entries.front().kind == patchy::SmartFilterKind::GaussianBlur);
  const auto* radius = std::get_if<patchy::GaussianBlurSmartFilter>(
      &stack->entries.front().parameters);
  CHECK(radius != nullptr);
  CHECK(std::abs(radius->radius_pixels - 2.5) < 0.000001);
  CHECK(filter_rect_equal(stack->mask.bounds,
                          patchy::Rect::from_size(document.width(),
                                                 document.height())));
  CHECK(stack->mask.enabled);
  CHECK(!stack->mask.linked);
  CHECK(!stack->mask.extend_with_white);
  CHECK(stack->mask.default_color == 0U);
  CHECK(patchy::ui::pixel_buffers_equal(stack->mask.pixels, expected_mask));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels) ||
        !filter_rect_equal(filtered->bounds(), original_bounds));
  const auto unfiltered_preview =
      patchy::ui::render_smart_object_layer_preview(
          std::as_const(document), *filtered,
          canvas->transform_interpolation());
  CHECK(unfiltered_preview.has_value());
  const auto expected_unfiltered_pixels = unfiltered_preview->unfiltered.pixels;
  const auto expected_unfiltered_bounds = unfiltered_preview->unfiltered.bounds;
  auto disabled_stack = *stack;
  disabled_stack.entries.front().enabled = false;
  const auto disabled_preview = patchy::ui::render_smart_object_layer_preview(
      std::as_const(document), *filtered, canvas->transform_interpolation(),
      &disabled_stack);
  CHECK(disabled_preview.has_value());
  const auto expected_disabled = disabled_preview->rendered;
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* cache = std::as_const(document)
                          .metadata()
                          .smart_filter_effects.find_unique(placed_uuid);
  CHECK(cache != nullptr && cache->semantic_supported());
  CHECK(cache->mask_present && cache->mask_decoded && cache->mask.has_value());
  CHECK(cache->mask->samples != nullptr);
  CHECK(cache->mask->samples->size() ==
        static_cast<std::size_t>(document.width()) *
            static_cast<std::size_t>(document.height()));
  const auto cache_storage = cache->raw_storage;
  const auto cache_offset = cache->raw_body_offset;
  const auto cache_length = cache->raw_body_length;
  const auto check_patchy_raster_status = [&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    CHECK(current != nullptr);
    const auto& metadata = current->metadata();
    const auto status = metadata.find(
        patchy::kLayerMetadataSmartObjectRasterStatus);
    CHECK(status != metadata.end());
    CHECK(status->second == patchy::kSmartObjectRasterStatusPatchy);
  };

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* item = require_layer_item(*list, QStringLiteral("small"));
    auto* row = list->itemWidget(item);
    CHECK(row != nullptr);
    return row;
  };
  auto* row = active_row();
  CHECK(row->findChild<QWidget*>(QStringLiteral("layerSmartFiltersRow")) !=
        nullptr);
  CHECK(row->findChild<QLabel*>(
            QStringLiteral("layerSmartFilterMaskThumbnail")) != nullptr);
  auto* label = row->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Gaussian Blur"));
  CHECK(label->toolTip().contains(QStringLiteral("2.5 px")));
  auto* entry_visibility = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterVisibilityButton"));
  auto* stack_visibility = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFiltersVisibilityButton"));
  CHECK(entry_visibility != nullptr && entry_visibility->isChecked());
  CHECK(stack_visibility != nullptr && stack_visibility->isChecked());
  CHECK(row->findChild<QToolButton*>(
            QStringLiteral("layerSmartFilterEditButton")) != nullptr);
  CHECK(row->findChild<QToolButton*>(
            QStringLiteral("layerSmartFilterMoreButton")) != nullptr);
  CHECK(row->findChild<QAction*>(
            QStringLiteral("layerSmartFilterDeleteAction")) != nullptr);
  ensure_artifact_dir();
  save_widget_artifact("ui_smart_filter_gaussian_layer_rows", *row);

  entry_visibility->click();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() != nullptr &&
           !current->smart_filter_stack()->entries.front().enabled;
  }));
  {
    const auto* hidden = std::as_const(document).find_layer(layer_id);
    CHECK(filter_rect_equal(hidden->bounds(), expected_disabled.bounds));
    CHECK(patchy::ui::pixel_buffers_equal(hidden->pixels(),
                                          expected_disabled.pixels));
  }
  check_patchy_raster_status();
  entry_visibility = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterVisibilityButton"));
  CHECK(entry_visibility != nullptr && !entry_visibility->isChecked());
  entry_visibility->click();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() != nullptr &&
           current->smart_filter_stack()->entries.front().enabled;
  }));

  stack_visibility = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFiltersVisibilityButton"));
  CHECK(stack_visibility != nullptr && stack_visibility->isChecked());
  stack_visibility->click();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() != nullptr &&
           !current->smart_filter_stack()->enabled;
  }));
  check_patchy_raster_status();
  stack_visibility = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFiltersVisibilityButton"));
  CHECK(stack_visibility != nullptr && !stack_visibility->isChecked());
  stack_visibility->click();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() != nullptr &&
           current->smart_filter_stack()->enabled;
  }));

  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  bool edited_fractional_radius = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* edit_radius =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(edit_radius != nullptr);
    CHECK(std::abs(edit_radius->value() - 2.5) < 0.000001);
    edit_radius->setValue(3.7);
    edited_fractional_radius = true;
    dialog->accept();
  });
  edit->click();
  CHECK(process_events_until([&] { return edited_fractional_radius; }, 5000));
  process_events_for(50);
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && filtered->smart_filter_stack() != nullptr);
  radius = std::get_if<patchy::GaussianBlurSmartFilter>(
      &filtered->smart_filter_stack()->entries.front().parameters);
  CHECK(radius != nullptr &&
        std::abs(radius->radius_pixels - 3.7) < 0.000001);
  label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Gaussian Blur"));
  CHECK(label->toolTip().contains(QStringLiteral("3.7 px")));
  cache = std::as_const(document)
              .metadata()
              .smart_filter_effects.find_unique(placed_uuid);
  CHECK(cache != nullptr && cache->raw_storage == cache_storage);
  CHECK(cache->raw_body_offset == cache_offset);
  CHECK(cache->raw_body_length == cache_length);

  // Imported native descriptors may carry a radius above Patchy's practical
  // slider span. Opening and accepting one unchanged must retain that value
  // instead of silently clamping it to the 12 px slider maximum.
  auto imported_stack = *filtered->smart_filter_stack();
  auto* imported_radius = std::get_if<patchy::GaussianBlurSmartFilter>(
      &imported_stack.entries.front().parameters);
  CHECK(imported_radius != nullptr);
  imported_radius->radius_pixels = 18.75;
  document.find_layer(layer_id)->set_smart_filter_stack(
      std::move(imported_stack));
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);

  bool retained_imported_radius = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* imported_spin = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* imported_slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(imported_spin != nullptr && imported_slider != nullptr);
    CHECK(std::abs(imported_spin->value() - 18.75) < 0.000001);
    CHECK(std::abs(imported_spin->maximum() - 18.75) < 0.000001);
    CHECK(imported_slider->maximum() == 1190);
    CHECK(imported_slider->value() == imported_slider->maximum());
    retained_imported_radius = true;
    dialog->accept();
  });
  edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return retained_imported_radius; }, 5000));
  process_events_for(50);
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && filtered->smart_filter_stack() != nullptr);
  radius = std::get_if<patchy::GaussianBlurSmartFilter>(
      &filtered->smart_filter_stack()->entries.front().parameters);
  CHECK(radius != nullptr &&
        std::abs(radius->radius_pixels - 18.75) < 0.000001);

  auto* remove = active_row()->findChild<QAction*>(
      QStringLiteral("layerSmartFilterDeleteAction"));
  CHECK(remove != nullptr && remove->isEnabled());
  remove->trigger();
  CHECK(process_events_until([&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr && current->smart_filter_stack() == nullptr;
  }));
  const auto* deleted = std::as_const(document).find_layer(layer_id);
  CHECK(deleted != nullptr);
  CHECK(filter_rect_equal(deleted->bounds(), expected_unfiltered_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(deleted->pixels(),
                                        expected_unfiltered_pixels));
  check_patchy_raster_status();
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(placed_uuid) == nullptr);
  CHECK(active_row()->findChild<QWidget*>(
            QStringLiteral("layerSmartFiltersRow")) == nullptr);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && restored->smart_filter_stack() != nullptr);
  CHECK(std::as_const(document)
            .metadata()
            .smart_filter_effects.find_unique(placed_uuid) != nullptr);
}

void ui_smart_filter_mask_thumbnail_routes_edits_and_resyncs_undo() {
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
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_id = select_named_layer(
      window, QStringLiteral("Layer mask plus Smart Filter mask"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(canvas != nullptr && layers != nullptr);
  layers->scrollToItem(
      require_layer_item(*layers,
                         QStringLiteral("Layer mask plus Smart Filter mask")),
      QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  const auto current_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    CHECK(layer->smart_filter_stack()->support ==
          patchy::SmartFilterStackSupport::Supported);
    return *layer->smart_filter_stack();
  };
  const auto current_record = [&]()
      -> const patchy::SmartFilterEffectsRecord& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr);
    const auto placed_uuid = patchy::smart_object_placed_uuid(*layer);
    CHECK(!placed_uuid.empty());
    const auto* record =
        std::as_const(document).metadata().smart_filter_effects.find_unique(
            placed_uuid);
    CHECK(record != nullptr && record->semantic_supported());
    return *record;
  };
  const auto active_row = [&]() -> QWidget* {
    auto* item = require_layer_item(
        *layers, QStringLiteral("Layer mask plus Smart Filter mask"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    return row;
  };

  const auto* original_layer = std::as_const(document).find_layer(layer_id);
  CHECK(original_layer != nullptr && original_layer->mask().has_value());
  const auto ordinary_layer_mask_before = original_layer->mask()->pixels;
  const auto initial_mask = current_stack().mask;
  CHECK(initial_mask.enabled && !initial_mask.pixels.empty());
  const auto materialized_initial = materialize_smart_filter_mask_for_ui_test(
      initial_mask, document.width(), document.height());
  const auto initial_cache_prefix =
      smart_filter_cache_prefix(current_record());

  // The nested thumbnail must be recognized as its own edit target rather than
  // falling through to the row/content click path.
  auto* smart_mask_thumbnail = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterMaskThumbnail"));
  CHECK(smart_mask_thumbnail != nullptr && smart_mask_thumbnail->isEnabled());
  CHECK(!smart_mask_thumbnail->property("layerTargetActive").toBool());
  click_layer_row_thumbnail(
      *layers, QStringLiteral("Layer mask plus Smart Filter mask"),
      QStringLiteral("layerSmartFilterMaskThumbnail"));
  CHECK(canvas->layer_edit_target() ==
        patchy::ui::CanvasWidget::LayerEditTarget::SmartFilterMask);
  CHECK(canvas->editing_smart_filter_mask());
  CHECK(canvas->smart_filter_mask_owner_id() == layer_id);
  CHECK(patchy::ui::pixel_buffers_equal(
      canvas->smart_filter_mask_pixels(), materialized_initial));
  smart_mask_thumbnail = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterMaskThumbnail"));
  CHECK(smart_mask_thumbnail != nullptr);
  CHECK(smart_mask_thumbnail->property("layerTargetActive").toBool());

  click_layer_row_thumbnail(
      *layers, QStringLiteral("Layer mask plus Smart Filter mask"),
      QStringLiteral("layerContentThumbnail"));
  CHECK(canvas->layer_edit_target() ==
        patchy::ui::CanvasWidget::LayerEditTarget::Content);

  // Ctrl-click loads the native filter mask as selection alpha without
  // switching the pixel edit target.
  canvas->clear_selection();
  click_layer_row_thumbnail(
      *layers, QStringLiteral("Layer mask plus Smart Filter mask"),
      QStringLiteral("layerSmartFilterMaskThumbnail"), Qt::ControlModifier);
  CHECK(canvas->has_selection());
  CHECK(patchy::ui::pixel_buffers_equal(canvas->selection_as_grayscale(),
                                        materialized_initial));
  CHECK(canvas->layer_edit_target() ==
        patchy::ui::CanvasWidget::LayerEditTarget::Content);

  // Shift-click changes the SoLd enable flag but leaves every native FEid byte
  // alone. Re-enable it so the later mask edit begins from the fixture state.
  const auto cache_body_before_toggle =
      smart_filter_record_body_copy(current_record());
  const auto undo_before_toggle =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  click_layer_row_thumbnail(
      *layers, QStringLiteral("Layer mask plus Smart Filter mask"),
      QStringLiteral("layerSmartFilterMaskThumbnail"), Qt::ShiftModifier);
  CHECK(!current_stack().mask.enabled);
  CHECK(smart_filter_record_body_copy(current_record()) ==
        cache_body_before_toggle);
  click_layer_row_thumbnail(
      *layers, QStringLiteral("Layer mask plus Smart Filter mask"),
      QStringLiteral("layerSmartFilterMaskThumbnail"), Qt::ShiftModifier);
  CHECK(current_stack().mask.enabled);
  CHECK(smart_filter_record_body_copy(current_record()) ==
        cache_body_before_toggle);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before_toggle + 2U);

  // Alt-click selects the Smart Filter mask and enters the grayscale
  // inspection mode, independently of the ordinary layer mask on this layer.
  click_layer_row_thumbnail(
      *layers, QStringLiteral("Layer mask plus Smart Filter mask"),
      QStringLiteral("layerSmartFilterMaskThumbnail"), Qt::AltModifier);
  CHECK(canvas->layer_edit_target() ==
        patchy::ui::CanvasWidget::LayerEditTarget::SmartFilterMask);
  CHECK(canvas->mask_display_mode() ==
        patchy::ui::CanvasWidget::MaskDisplayMode::Grayscale);
  CHECK(patchy::ui::pixel_buffers_equal(
      canvas->smart_filter_mask_pixels(), materialized_initial));

  // Use the canvas-owned temporary buffer but complete it through the real
  // MainWindow callback. The document changes only at completion, in one undo
  // step; only the optional FEid mask tail is regenerated.
  canvas->clear_selection();
  const auto cache_body_before_edit =
      smart_filter_record_body_copy(current_record());
  const auto undo_before_edit =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(!canvas
             ->fill_smart_filter_mask(
                 QColor(Qt::black), QStringLiteral("Fill Smart Filter Mask"))
             .isEmpty());
  canvas->finish_smart_filter_mask_edit();
  CHECK(process_events_until([&] {
    const auto& mask = current_stack().mask;
    return mask.bounds.x == 0 && mask.bounds.y == 0 &&
           mask.bounds.width == document.width() &&
           mask.bounds.height == document.height() &&
           !mask.pixels.empty() &&
           std::all_of(mask.pixels.data().begin(), mask.pixels.data().end(),
                       [](std::uint8_t value) { return value == 0U; });
  }));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before_edit + 1U);
  CHECK(canvas->editing_smart_filter_mask());
  CHECK(std::all_of(canvas->smart_filter_mask_pixels().data().begin(),
                    canvas->smart_filter_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 0U; }));
  const auto cache_body_after_edit =
      smart_filter_record_body_copy(current_record());
  CHECK(cache_body_after_edit != cache_body_before_edit);
  CHECK(smart_filter_cache_prefix(current_record()) == initial_cache_prefix);
  CHECK(current_record().mask.has_value() &&
        current_record().mask->samples != nullptr);
  CHECK(std::all_of(current_record().mask->samples->begin(),
                    current_record().mask->samples->end(),
                    [](std::uint8_t value) { return value == 0U; }));
  const auto* layer_after_edit = std::as_const(document).find_layer(layer_id);
  CHECK(layer_after_edit != nullptr && layer_after_edit->mask().has_value());
  CHECK(patchy::ui::pixel_buffers_equal(layer_after_edit->mask()->pixels,
                                        ordinary_layer_mask_before));

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(process_events_until([&] {
    return patchy::ui::pixel_buffers_equal(current_stack().mask.pixels,
                                           initial_mask.pixels);
  }));
  CHECK(canvas->editing_smart_filter_mask());
  CHECK(patchy::ui::pixel_buffers_equal(
      canvas->smart_filter_mask_pixels(), materialized_initial));
  CHECK(smart_filter_record_body_copy(current_record()) ==
        cache_body_before_edit);

  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  CHECK(process_events_until([&] {
    return std::all_of(current_stack().mask.pixels.data().begin(),
                       current_stack().mask.pixels.data().end(),
                       [](std::uint8_t value) { return value == 0U; });
  }));
  CHECK(canvas->editing_smart_filter_mask());
  CHECK(std::all_of(canvas->smart_filter_mask_pixels().data().begin(),
                    canvas->smart_filter_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 0U; }));
  CHECK(smart_filter_cache_prefix(current_record()) == initial_cache_prefix);

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_mask_edited.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_layer = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) {
        return layer.name() == "Layer mask plus Smart Filter mask";
      });
  CHECK(reopened_layer != reopened.layers().end());
  CHECK(reopened_layer->smart_filter_stack() != nullptr);
  const auto& reopened_mask = reopened_layer->smart_filter_stack()->mask;
  CHECK(filter_rect_equal(
      reopened_mask.bounds,
      patchy::Rect::from_size(reopened.width(), reopened.height())));
  CHECK(std::all_of(reopened_mask.pixels.data().begin(),
                    reopened_mask.pixels.data().end(),
                    [](std::uint8_t value) { return value == 0U; }));
  const auto* reopened_record =
      reopened.metadata().smart_filter_effects.find_unique(
          patchy::smart_object_placed_uuid(*reopened_layer));
  CHECK(reopened_record != nullptr && reopened_record->semantic_supported());
  CHECK(smart_filter_cache_prefix(*reopened_record) == initial_cache_prefix);
}

void ui_smart_filter_blending_edit_cancel_apply_is_atomic() {
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
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_id =
      select_named_layer(window, QStringLiteral("Gaussian radius 2.0"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  layers->scrollToItem(
      require_layer_item(*layers, QStringLiteral("Gaussian radius 2.0")),
      QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  const auto current_layer = [&]() -> const patchy::Layer& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    CHECK(layer->smart_filter_stack()->entries.size() == 1U);
    return *layer;
  };
  const auto current_entry = [&]() -> const patchy::SmartFilterEntry& {
    return current_layer().smart_filter_stack()->entries.front();
  };
  const auto current_record = [&]()
      -> const patchy::SmartFilterEffectsRecord& {
    const auto placed_uuid =
        patchy::smart_object_placed_uuid(current_layer());
    CHECK(!placed_uuid.empty());
    const auto* record =
        std::as_const(document).metadata().smart_filter_effects.find_unique(
            placed_uuid);
    CHECK(record != nullptr && record->semantic_supported());
    return *record;
  };
  const auto active_row = [&]() -> QWidget* {
    auto* item =
        require_layer_item(*layers, QStringLiteral("Gaussian radius 2.0"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    return row;
  };
  const auto edit_button = [&]() -> QToolButton* {
    auto* row = active_row();
    auto* button = row->findChild<QToolButton*>(
        QStringLiteral("layerSmartFilterEditButton"));
    CHECK(button != nullptr && button->isEnabled());
    CHECK(button->property("smartFilterExecutionIndex").toULongLong() == 0U);
    // Blending lives in the settings dialog since July 2026; the More menu
    // deliberately carries no blending action anymore.
    CHECK(row->findChild<QAction*>(
              QStringLiteral("layerSmartFilterBlendingAction")) == nullptr);
    return button;
  };

  CHECK(current_entry().blend_mode == patchy::BlendMode::Normal);
  CHECK(std::abs(current_entry().opacity - 1.0) < 0.0000001);
  const auto original_pixels = current_layer().pixels();
  const auto original_bounds = current_layer().bounds();
  const auto original_cache_body =
      smart_filter_record_body_copy(current_record());
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified_before =
      patchy::ui::MainWindowTestAccess::active_session_is_modified(window);

  // The Edit button opens the combined settings dialog with the Blending
  // section (July 2026 merge). Exercise its real signal path and let a
  // changed live preview render before cancelling.
  bool cancel_dialog_opened = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* mode = dialog->findChild<QComboBox*>(
        QStringLiteral("smartFilterBlendModeCombo"));
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("smartFilterOpacitySpin"));
    auto* preview = dialog->findChild<QCheckBox*>(
        QStringLiteral("filterPreviewCheck"));
    CHECK(mode != nullptr && opacity != nullptr && preview != nullptr);
    CHECK(preview->isChecked());
    const auto multiply =
        mode->findData(static_cast<int>(patchy::BlendMode::Multiply));
    CHECK(multiply >= 0);
    mode->setCurrentIndex(multiply);
    opacity->setValue(21.5);
    cancel_dialog_opened = true;
    QTimer::singleShot(75, dialog, [dialog] { dialog->reject(); });
  });
  edit_button()->click();
  process_events_for(100);
  CHECK(cancel_dialog_opened);
  CHECK(current_entry().blend_mode == patchy::BlendMode::Normal);
  CHECK(std::abs(current_entry().opacity - 1.0) < 0.0000001);
  CHECK(filter_rect_equal(current_layer().bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(current_layer().pixels(),
                                        original_pixels));
  CHECK(smart_filter_record_body_copy(current_record()) ==
        original_cache_body);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window) ==
        modified_before);

  bool apply_dialog_opened = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* mode = dialog->findChild<QComboBox*>(
        QStringLiteral("smartFilterBlendModeCombo"));
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("smartFilterOpacitySpin"));
    CHECK(mode != nullptr && opacity != nullptr);
    const auto multiply =
        mode->findData(static_cast<int>(patchy::BlendMode::Multiply));
    CHECK(multiply >= 0);
    mode->setCurrentIndex(multiply);
    opacity->setValue(37.0);
    apply_dialog_opened = true;
    dialog->accept();
  });
  edit_button()->click();
  CHECK(process_events_until([&] {
    return current_entry().blend_mode == patchy::BlendMode::Multiply &&
           std::abs(current_entry().opacity - 0.37) < 0.0000001;
  }));
  CHECK(apply_dialog_opened);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  CHECK(smart_filter_record_body_copy(current_record()) ==
        original_cache_body);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(process_events_until([&] {
    return current_entry().blend_mode == patchy::BlendMode::Normal &&
           std::abs(current_entry().opacity - 1.0) < 0.0000001;
  }));
  CHECK(filter_rect_equal(current_layer().bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(current_layer().pixels(),
                                        original_pixels));
  CHECK(smart_filter_record_body_copy(current_record()) ==
        original_cache_body);

  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  CHECK(process_events_until([&] {
    return current_entry().blend_mode == patchy::BlendMode::Multiply &&
           std::abs(current_entry().opacity - 0.37) < 0.0000001;
  }));
  CHECK(smart_filter_record_body_copy(current_record()) ==
        original_cache_body);

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_blending_edited.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_layer = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) {
        return layer.name() == "Gaussian radius 2.0";
      });
  CHECK(reopened_layer != reopened.layers().end());
  CHECK(reopened_layer->smart_filter_stack() != nullptr);
  CHECK(reopened_layer->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_layer->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(reopened_entry.opacity - 0.37) < 0.0000001);
  const auto* reopened_record =
      reopened.metadata().smart_filter_effects.find_unique(
          patchy::smart_object_placed_uuid(*reopened_layer));
  CHECK(reopened_record != nullptr && reopened_record->semantic_supported());
  CHECK(smart_filter_record_body_copy(*reopened_record) ==
        original_cache_body);
}

// Opening the settings dialog (from the Edit button or the Edit Blending
// Options action) with Preview on and unchanged values must not repaint the
// layer: the current raster may be Photoshop's own preview of the stack, and
// swapping in Patchy's render of identical settings visibly changed the image
// (July 2026 field report).
void ui_smart_filter_dialogs_with_preview_keep_unchanged_pixels() {
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
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_id =
      select_named_layer(window, QStringLiteral("Gaussian radius 2.0"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  layers->scrollToItem(
      require_layer_item(*layers, QStringLiteral("Gaussian radius 2.0")),
      QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  // Stand in for Photoshop's baked preview: doctor one byte of the stored
  // raster so any re-render of the unchanged stack is detectable.
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  auto doctored = std::as_const(*layer).pixels();
  doctored.pixel(0, 0)[0] =
      static_cast<std::uint8_t>(doctored.pixel(0, 0)[0] ^ 0x40U);
  layer->set_pixels(doctored);

  const auto pixels_are_doctored = [&] {
    const auto* current = std::as_const(document).find_layer(layer_id);
    return current != nullptr &&
           patchy::ui::pixel_buffers_equal(current->pixels(), doctored);
  };
  const auto active_row = [&]() -> QWidget* {
    auto* item =
        require_layer_item(*layers, QStringLiteral("Gaussian radius 2.0"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    return row;
  };

  bool blending_checked = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("smartFilterOpacitySpin"));
    auto* preview = dialog->findChild<QCheckBox*>(
        QStringLiteral("filterPreviewCheck"));
    CHECK(opacity != nullptr && preview != nullptr && preview->isChecked());
    // Opening with unchanged settings keeps the stored raster.
    process_events_for(150);
    CHECK(pixels_are_doctored());
    // A real change previews...
    opacity->setValue(40.0);
    CHECK(process_events_until([&] { return !pixels_are_doctored(); }));
    // ...and returning to the stored value restores the exact raster.
    opacity->setValue(100.0);
    CHECK(process_events_until([&] { return pixels_are_doctored(); }));
    blending_checked = true;
    dialog->reject();
  });
  auto* blending_edit_button = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(blending_edit_button != nullptr);
  blending_edit_button->click();
  process_events_for(50);
  CHECK(blending_checked);
  CHECK(pixels_are_doctored());

  // The settings dialog takes the same unchanged-restore path.
  bool edit_checked = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    process_events_for(150);
    CHECK(pixels_are_doctored());
    edit_checked = true;
    dialog->reject();
  });
  auto* edit_button = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit_button != nullptr);
  edit_button->click();
  process_events_for(50);
  CHECK(edit_checked);
  CHECK(pixels_are_doctored());
}

// Double-clicking a Smart Filter entry row opens that filter's settings
// dialog (which carries the Blending section) instead of the layer styles
// dialog; double-clicking the layer's main row keeps opening layer styles.
void ui_smart_filter_entry_row_double_click_opens_settings() {
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
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  select_named_layer(window, QStringLiteral("Gaussian radius 2.0"));
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  layers->scrollToItem(
      require_layer_item(*layers, QStringLiteral("Gaussian radius 2.0")),
      QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  // Refetches the row widget per use: dialogs rebuild the layer rows.
  const auto viewport_point_for = [&](const QString& child_name) {
    auto* row = layers->itemWidget(
        require_layer_item(*layers, QStringLiteral("Gaussian radius 2.0")));
    CHECK(row != nullptr);
    auto* child = row->findChild<QWidget*>(child_name);
    CHECK(child != nullptr && child->isVisible());
    return layers->viewport()->mapFromGlobal(
        child->mapToGlobal(QPoint(child->width() / 2, child->height() / 2)));
  };
  const auto double_click_at = [&](QPoint viewport_pos) {
    QMouseEvent double_click(
        QEvent::MouseButtonDblClick, QPointF(viewport_pos),
        layers->viewport()->mapToGlobal(viewport_pos), Qt::LeftButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(layers->viewport(), &double_click);
  };

  bool saw_settings_dialog = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    CHECK(dialog->findChild<QComboBox*>(
              QStringLiteral("smartFilterBlendModeCombo")) != nullptr);
    CHECK(dialog->findChild<QDoubleSpinBox*>(
              QStringLiteral("smartFilterOpacitySpin")) != nullptr);
    saw_settings_dialog = true;
    dialog->reject();
  });
  double_click_at(
      viewport_point_for(QStringLiteral("layerSmartFilterEntryLabel")));
  process_events_for(120);
  CHECK(saw_settings_dialog);
  CHECK(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")) ==
        nullptr);

  bool saw_style_dialog = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    saw_style_dialog = true;
    dialog->reject();
  });
  double_click_at(viewport_point_for(QStringLiteral("layerMainRow")));
  process_events_for(120);
  CHECK(saw_style_dialog);
}

void ui_smart_filter_authored_psd_reopens_with_native_stack_and_cache() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow authoring_window;
  show_window(authoring_window);
  const auto layer_id = open_smart_object_fixture(authoring_window);
  auto& authored_document =
      patchy::ui::MainWindowTestAccess::document(authoring_window);
  auto* canvas =
      patchy::ui::MainWindowTestAccess::canvas(authoring_window);
  CHECK(canvas != nullptr);

  patchy::PixelBuffer selection(authored_document.width(),
                                authored_document.height(),
                                patchy::PixelFormat::gray8());
  selection.clear(0);
  const auto left = authored_document.width() / 5;
  const auto right = authored_document.width() * 4 / 5;
  const auto top = authored_document.height() / 5;
  const auto bottom = authored_document.height() * 4 / 5;
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      selection.pixel(x, y)[0] =
          static_cast<std::uint8_t>(x == left ? 96 : 255);
    }
  }
  canvas->replace_selection_from_grayscale(
      selection, QStringLiteral("Authored Smart Filter mask"));
  QApplication::processEvents();
  CHECK(canvas->selection_has_partial_alpha());
  const auto expected_mask = canvas->selection_as_grayscale();

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(4.5);
    applied = true;
    dialog->accept();
  });
  require_action(authoring_window,
                 "filterAction_patchy_filters_gaussian_blur")
      ->trigger();
  QApplication::processEvents();
  CHECK(applied);

  const auto* authored_layer =
      std::as_const(authored_document).find_layer(layer_id);
  CHECK(authored_layer != nullptr &&
        authored_layer->smart_filter_stack() != nullptr);
  const auto authored_pixels = authored_layer->pixels();
  const auto authored_bounds = authored_layer->bounds();
  // Photoshop 27.8 hide/show rerender of this exact authored fixture expands
  // radius 4.5 against the 96x96 FEid cache canvas to these visible bounds.
  CHECK(authored_bounds.x == 22 && authored_bounds.y == 26);
  CHECK(authored_bounds.width == 52 && authored_bounds.height == 44);
  const auto authored_placed_uuid =
      patchy::smart_object_placed_uuid(*authored_layer);
  CHECK(!authored_placed_uuid.empty());
  const auto* authored_cache =
      std::as_const(authored_document)
          .metadata()
          .smart_filter_effects.find_unique(authored_placed_uuid);
  CHECK(authored_cache != nullptr && authored_cache->semantic_supported());

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_gaussian_authored.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(authored_document,
                                                    artifact_path);
  CHECK(std::filesystem::exists(artifact_path));
  CHECK(std::filesystem::file_size(artifact_path) > 0U);

  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  const auto& reopened_layer = *reopened_it;
  CHECK(patchy::layer_is_smart_object(reopened_layer));
  CHECK(patchy::smart_object_lock_reason(reopened_layer).empty());
  CHECK(reopened_layer.smart_filter_stack() != nullptr);
  const auto* reopened_stack = reopened_layer.smart_filter_stack();
  CHECK(reopened_stack->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_stack->enabled && reopened_stack->entries.size() == 1U);
  CHECK(reopened_stack->entries.front().kind ==
        patchy::SmartFilterKind::GaussianBlur);
  const auto* reopened_radius =
      std::get_if<patchy::GaussianBlurSmartFilter>(
          &reopened_stack->entries.front().parameters);
  CHECK(reopened_radius != nullptr &&
        std::abs(reopened_radius->radius_pixels - 4.5) < 0.000001);
  CHECK(reopened_stack->mask.enabled);
  CHECK(!reopened_stack->mask.linked);
  CHECK(!reopened_stack->mask.extend_with_white);
  CHECK(reopened_stack->mask.default_color == 0U);
  CHECK(filter_rect_equal(
      reopened_stack->mask.bounds,
      patchy::Rect::from_size(reopened.width(), reopened.height())));
  CHECK(patchy::ui::pixel_buffers_equal(reopened_stack->mask.pixels,
                                        expected_mask));
  CHECK(filter_rect_equal(reopened_layer.bounds(), authored_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(reopened_layer.pixels(),
                                        authored_pixels));
  CHECK(patchy::smart_object_placed_uuid(reopened_layer) ==
        authored_placed_uuid);
  const auto* reopened_cache =
      reopened.metadata().smart_filter_effects.find_unique(
          authored_placed_uuid);
  CHECK(reopened_cache != nullptr && reopened_cache->semantic_supported());
  CHECK(reopened_cache->mask_present && reopened_cache->mask_decoded &&
        reopened_cache->mask.has_value());

  patchy::ui::MainWindow reopened_window;
  show_window(reopened_window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      reopened_window, QString::fromStdWString(artifact_path.wstring()));
  QApplication::processEvents();
  const auto reopened_layer_id =
      select_named_layer(reopened_window, QStringLiteral("small"));
  const auto& ui_document = std::as_const(
      patchy::ui::MainWindowTestAccess::document(reopened_window));
  const auto* ui_layer = ui_document.find_layer(reopened_layer_id);
  CHECK(ui_layer != nullptr && ui_layer->smart_filter_stack() != nullptr);
  CHECK(filter_rect_equal(ui_layer->bounds(), authored_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(ui_layer->pixels(), authored_pixels));
  auto* layer_list =
      reopened_window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* row = layer_list->itemWidget(
      require_layer_item(*layer_list, QStringLiteral("small")));
  CHECK(row != nullptr);
  auto* label = row->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  auto* edit = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Gaussian Blur"));
  CHECK(label->toolTip().contains(QStringLiteral("4.5 px")));
  CHECK(edit != nullptr && edit->isEnabled());
}

void ui_smart_filter_multiple_gaussian_duplicate_reorder_delete_roundtrip() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* gaussian = require_action(
      window, "filterAction_patchy_filters_gaussian_blur");

  const auto add_radius = [&](double wanted) {
    bool accepted = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(
          find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
      CHECK(dialog != nullptr);
      auto* radius = dialog->findChild<QDoubleSpinBox*>(
          QStringLiteral("filterRadiusSpin"));
      CHECK(radius != nullptr);
      radius->setValue(wanted);
      accepted = true;
      dialog->accept();
    });
    gaussian->trigger();
    QApplication::processEvents();
    CHECK(accepted);
  };
  const auto stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto radius_at = [&](std::size_t index) {
    const auto* radius = std::get_if<patchy::GaussianBlurSmartFilter>(
        &stack().entries.at(index).parameters);
    CHECK(radius != nullptr);
    return radius->radius_pixels;
  };
  const auto active_row = [&]() -> QWidget* {
    auto* list =
        window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  const auto button_for = [&](const QString& object_name,
                              std::size_t execution_index) {
    const auto buttons = active_row()->findChildren<QToolButton*>(object_name);
    const auto found = std::find_if(
        buttons.begin(), buttons.end(), [execution_index](QToolButton* button) {
          return button->property("smartFilterExecutionIndex").toULongLong() ==
                 static_cast<qulonglong>(execution_index);
        });
    CHECK(found != buttons.end());
    return *found;
  };
  const auto action_for = [&](const QString& object_name,
                              std::size_t execution_index) {
    const auto actions = active_row()->findChildren<QAction*>(object_name);
    const auto found = std::find_if(
        actions.begin(), actions.end(), [execution_index](QAction* action) {
          return action->property("smartFilterExecutionIndex").toULongLong() ==
                 static_cast<qulonglong>(execution_index);
        });
    CHECK(found != actions.end());
    return *found;
  };

  add_radius(2.0);
  const auto placed_uuid = patchy::smart_object_placed_uuid(
      *std::as_const(document).find_layer(layer_id));
  const auto* first_record = std::as_const(document)
                                 .metadata()
                                 .smart_filter_effects.find_unique(placed_uuid);
  CHECK(first_record != nullptr);
  const auto first_record_bytes =
      patchy::psd::raw_filter_effects_record_body(*first_record);
  const std::vector<std::uint8_t> preserved_cache(
      first_record_bytes.begin(), first_record_bytes.end());

  add_radius(7.0);
  CHECK(stack().entries.size() == 2U);
  CHECK(std::abs(radius_at(0) - 2.0) < 0.000001);
  CHECK(std::abs(radius_at(1) - 7.0) < 0.000001);
  auto labels = active_row()->findChildren<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(labels.size() == 2);
  CHECK(labels[0]->property("smartFilterExecutionIndex").toULongLong() == 1U);
  CHECK(labels[0]->text() == QStringLiteral("Gaussian Blur"));
  CHECK(labels[0]->toolTip().contains(QStringLiteral("7 px")));
  CHECK(labels[1]->property("smartFilterExecutionIndex").toULongLong() == 0U);

  auto* more =
      button_for(QStringLiteral("layerSmartFilterMoreButton"), 0U);
  CHECK(more->menu() != nullptr);
  auto* duplicate =
      action_for(QStringLiteral("layerSmartFilterDuplicateAction"), 0U);
  bool popup_path_reached = false;
  bool duplicate_rect_valid = false;
  QTimer::singleShot(20, [&] {
    popup_path_reached = more->menu()->isVisible();
    const auto duplicate_rect = more->menu()->actionGeometry(duplicate);
    duplicate_rect_valid = duplicate_rect.isValid();
    if (duplicate_rect_valid) {
      QTest::mouseClick(more->menu(), Qt::LeftButton, Qt::NoModifier,
                        duplicate_rect.center());
    } else {
      more->menu()->close();
    }
  });
  QTest::mouseClick(more, Qt::LeftButton);
  CHECK(popup_path_reached);
  CHECK(duplicate_rect_valid);
  CHECK(process_events_until([&] { return stack().entries.size() == 3U; }));
  CHECK(std::abs(radius_at(0) - 2.0) < 0.000001);
  CHECK(std::abs(radius_at(1) - 2.0) < 0.000001);
  CHECK(std::abs(radius_at(2) - 7.0) < 0.000001);

  bool edited_duplicate = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr && std::abs(radius->value() - 2.0) < 0.000001);
    radius->setValue(4.5);
    edited_duplicate = true;
    dialog->accept();
  });
  button_for(QStringLiteral("layerSmartFilterEditButton"), 1U)->click();
  CHECK(process_events_until([&] { return edited_duplicate; }));
  CHECK(std::abs(radius_at(0) - 2.0) < 0.000001);
  CHECK(std::abs(radius_at(1) - 4.5) < 0.000001);
  CHECK(std::abs(radius_at(2) - 7.0) < 0.000001);

  const auto undo_before_move =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  action_for(QStringLiteral("layerSmartFilterMoveUpAction"), 1U)->trigger();
  CHECK(process_events_until(
      [&] { return std::abs(radius_at(2) - 4.5) < 0.000001; }));
  CHECK(std::abs(radius_at(1) - 7.0) < 0.000001);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before_move + 1U);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(radius_at(1) - 4.5) < 0.000001);
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(radius_at(2) - 4.5) < 0.000001);
  action_for(QStringLiteral("layerSmartFilterMoveDownAction"), 2U)->trigger();
  CHECK(process_events_until(
      [&] { return std::abs(radius_at(1) - 4.5) < 0.000001; }));

  action_for(QStringLiteral("layerSmartFilterDeleteAction"), 1U)->trigger();
  CHECK(process_events_until([&] { return stack().entries.size() == 2U; }));
  CHECK(std::abs(radius_at(0) - 2.0) < 0.000001);
  CHECK(std::abs(radius_at(1) - 7.0) < 0.000001);

  const auto effects_before_missing_record_guard =
      document.metadata().smart_filter_effects;
  CHECK(document.metadata().smart_filter_effects.remove(placed_uuid));
  const auto guard_undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  action_for(QStringLiteral("layerSmartFilterDuplicateAction"), 0U)->trigger();
  QApplication::processEvents();
  CHECK(stack().entries.size() == 2U);
  CHECK(std::abs(radius_at(0) - 2.0) < 0.000001);
  CHECK(std::abs(radius_at(1) - 7.0) < 0.000001);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        guard_undo_depth);
  CHECK(window.statusBar()->currentMessage().contains(
      QStringLiteral("cannot be edited safely")));
  document.metadata().smart_filter_effects =
      effects_before_missing_record_guard;

  const auto* final_record = std::as_const(document)
                                 .metadata()
                                 .smart_filter_effects.find_unique(placed_uuid);
  CHECK(final_record != nullptr);
  const auto final_record_bytes =
      patchy::psd::raw_filter_effects_record_body(*final_record);
  CHECK(final_record_bytes.size() == preserved_cache.size());
  CHECK(std::equal(final_record_bytes.begin(), final_record_bytes.end(),
                   preserved_cache.begin()));

  const auto reopened = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 2U);
  const auto reopened_radius = [&](std::size_t index) {
    const auto* value = std::get_if<patchy::GaussianBlurSmartFilter>(
        &reopened_it->smart_filter_stack()->entries[index].parameters);
    CHECK(value != nullptr);
    return value->radius_pixels;
  };
  CHECK(std::abs(reopened_radius(0) - 2.0) < 0.000001);
  CHECK(std::abs(reopened_radius(1) - 7.0) < 0.000001);
  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_multiple_gaussian.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  CHECK(std::filesystem::exists(artifact_path));
  save_widget_artifact("ui_smart_filter_multiple_gaussian_rows",
                       *active_row());
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_part1() {
  return {
      {"ui_smart_object_import_badges_protects_and_rasterizes",
       ui_smart_object_import_badges_protects_and_rasterizes},
      {"ui_smart_filter_duplicate_rekeys_native_cache",
       ui_smart_filter_duplicate_rekeys_native_cache},
      {"ui_smart_filter_same_document_paste_keeps_native_cache_adjacent",
       ui_smart_filter_same_document_paste_keeps_native_cache_adjacent},
      {"ui_smart_filter_cross_document_paste_adopts_native_cache",
       ui_smart_filter_cross_document_paste_adopts_native_cache},
      {"ui_smart_filter_rasterize_and_undo_restore_native_cache",
       ui_smart_filter_rasterize_and_undo_restore_native_cache},
      {"ui_convert_for_smart_filters_action_converts_eligible_pixel_layer",
       ui_convert_for_smart_filters_action_converts_eligible_pixel_layer},
      {"ui_smart_filter_gaussian_dialog_mask_rows_edit_toggle_delete",
       ui_smart_filter_gaussian_dialog_mask_rows_edit_toggle_delete},
      {"ui_smart_filter_mask_thumbnail_routes_edits_and_resyncs_undo",
       ui_smart_filter_mask_thumbnail_routes_edits_and_resyncs_undo},
      {"ui_smart_filter_blending_edit_cancel_apply_is_atomic",
       ui_smart_filter_blending_edit_cancel_apply_is_atomic},
      {"ui_smart_filter_dialogs_with_preview_keep_unchanged_pixels",
       ui_smart_filter_dialogs_with_preview_keep_unchanged_pixels},
      {"ui_smart_filter_entry_row_double_click_opens_settings",
       ui_smart_filter_entry_row_double_click_opens_settings},
      {"ui_smart_filter_authored_psd_reopens_with_native_stack_and_cache",
       ui_smart_filter_authored_psd_reopens_with_native_stack_and_cache},
      {"ui_smart_filter_multiple_gaussian_duplicate_reorder_delete_roundtrip",
       ui_smart_filter_multiple_gaussian_duplicate_reorder_delete_roundtrip},
  };
}
