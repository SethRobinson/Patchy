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

constexpr auto kSmartFilterInstanceAName = "Instance A Gaussian 1.5";

constexpr auto kSmartFilterInstanceBName = "Instance B Gaussian 4.5 transformed masked";

std::size_t smart_filter_effect_record_count(const patchy::Document& document) {
  std::size_t count = 0;
  for (const auto& block : document.metadata().smart_filter_effects.blocks) {
    count += block.records.size();
  }
  return count;
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

patchy::LayerId select_named_layer(patchy::ui::MainWindow& window, const QString& name) {
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, name);
  layer_list->clearSelection();
  layer_list->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
  item->setSelected(true);
  QApplication::processEvents();

  const auto id = static_cast<patchy::LayerId>(
      item->data(patchy::ui::kLayerIdRole).toULongLong());
  CHECK(id != 0);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).active_layer_id() == id);
  return id;
}

void open_smart_filter_instances_fixture(patchy::ui::MainWindow& window) {
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-instances-base.psd")
          .wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto& document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(std::any_of(document.layers().begin(), document.layers().end(),
                    [](const patchy::Layer& layer) {
                      return layer.name() == kSmartFilterInstanceAName;
                    }));
  CHECK(std::any_of(document.layers().begin(), document.layers().end(),
                    [](const patchy::Layer& layer) {
                      return layer.name() == kSmartFilterInstanceBName;
                    }));
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

void ui_smart_filter_blending_more_menu_cancel_apply_is_atomic() {
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
  const auto blending_action = [&]() -> QAction* {
    auto* row = active_row();
    auto* more = row->findChild<QToolButton*>(
        QStringLiteral("layerSmartFilterMoreButton"));
    auto* action = row->findChild<QAction*>(
        QStringLiteral("layerSmartFilterBlendingAction"));
    CHECK(more != nullptr && more->menu() != nullptr);
    CHECK(action != nullptr && action->isEnabled());
    CHECK(more->menu()->actions().contains(action));
    CHECK(action->property("smartFilterExecutionIndex").toULongLong() == 0U);
    return action;
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

  // The action is owned by the entry's More menu. Exercise its real signal path
  // and let a changed live preview render before cancelling.
  bool cancel_dialog_opened = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("smartFilterBlendingDialog")));
    CHECK(dialog != nullptr);
    auto* mode = dialog->findChild<QComboBox*>(
        QStringLiteral("smartFilterBlendModeCombo"));
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("smartFilterOpacitySpin"));
    auto* preview = dialog->findChild<QCheckBox*>(
        QStringLiteral("smartFilterBlendingPreviewCheck"));
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
  blending_action()->trigger();
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
        find_top_level_dialog(QStringLiteral("smartFilterBlendingDialog")));
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
  blending_action()->trigger();
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
      looks->setCurrentItem(require_gallery_filter_item(
          *looks, QStringLiteral("patchy.filters.box_blur")));
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

void ui_gaussian_blur_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::Layer artwork(
      built.allocate_layer_id(), "Ordinary Pixels",
      solid_pixels(18, 14, patchy::PixelFormat::rgba8(),
                   QColor(220, 40, 30, 255)));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Gaussian"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(3);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_gaussian_blur")
      ->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(!filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_high_pass_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(18, 14, patchy::PixelFormat::rgba8(),
                             QColor(220, 40, 30, 255));
  pixels.pixel(9, 7)[0] = 15;
  pixels.pixel(9, 7)[1] = 240;
  pixels.pixel(9, 7)[2] = 90;
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive High Pass"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->minimum() - 0.1) < 0.000001);
    CHECK(std::abs(radius->maximum() - 1000.0) < 0.000001);
    CHECK(slider->maximum() == 119);
    CHECK(std::abs(radius->value() - 10.0) < 0.000001);
    radius->setValue(4.25);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_high_pass")->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  for (std::int32_t y = 0; y < original_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < original_pixels.width(); ++x) {
      CHECK(filtered->pixels().pixel(x, y)[3] ==
            original_pixels.pixel(x, y)[3]);
    }
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_median_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(18, 14, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 43 + y * 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 71) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 89 + y * 7) % 256);
      pixel[3] = static_cast<std::uint8_t>((x * 29 + y * 31 + 64) % 256);
    }
  }
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Median"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 500.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(slider->maximum() == 2400);
    CHECK(std::abs(radius->value() - 1.0) < 0.000001);
    radius->setValue(2.75);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_median")->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_dust_and_scratches_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(18, 14, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 43 + y * 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 71) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 89 + y * 7) % 256);
      pixel[3] = static_cast<std::uint8_t>((x * 29 + y * 31 + 64) % 256);
    }
  }
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Dust and Scratches"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool callback_ran = false;
  bool dialog_found = false;
  bool controls_found = false;
  int radius_minimum = -1;
  int radius_maximum = -1;
  int radius_value = -1;
  int radius_slider_minimum = -1;
  int radius_slider_maximum = -1;
  int radius_value_after_typed_maximum = -1;
  int slider_value_after_typed_maximum = -1;
  int threshold_minimum = -1;
  int threshold_maximum = -1;
  int threshold_value = -1;
  QTimer::singleShot(0, [&] {
    callback_ran = true;
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    dialog_found = dialog != nullptr;
    if (dialog == nullptr) {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* visible_dialog = qobject_cast<QDialog*>(widget);
        if (visible_dialog != nullptr && visible_dialog->isVisible()) {
          visible_dialog->reject();
          break;
        }
      }
      return;
    }
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    auto* radius_slider =
        dialog->findChild<QSlider*>(QStringLiteral("filterRadiusSlider"));
    auto* threshold =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterThresholdSpin"));
    controls_found =
        radius != nullptr && radius_slider != nullptr && threshold != nullptr;
    if (controls_found) {
      radius_minimum = radius->minimum();
      radius_maximum = radius->maximum();
      radius_value = radius->value();
      radius_slider_minimum = radius_slider->minimum();
      radius_slider_maximum = radius_slider->maximum();
      threshold_minimum = threshold->minimum();
      threshold_maximum = threshold->maximum();
      threshold_value = threshold->value();
      radius->setValue(100);
      radius_value_after_typed_maximum = radius->value();
      slider_value_after_typed_maximum = radius_slider->value();
      radius->setValue(2);
      threshold->setValue(17);
    }
    dialog->accept();
  });
  require_action(window,
                 "filterAction_patchy_filters_dust_and_scratches")
      ->trigger();
  QApplication::processEvents();
  CHECK(callback_ran);
  CHECK(dialog_found);
  CHECK(controls_found);
  CHECK(radius_minimum == 1 && radius_maximum == 100);
  CHECK(radius_slider_minimum == 1 && radius_slider_maximum == 25);
  CHECK(radius_value == 1);
  CHECK(radius_value_after_typed_maximum == 100);
  CHECK(slider_value_after_typed_maximum == 25);
  CHECK(threshold_minimum == 0 && threshold_maximum == 255);
  CHECK(threshold_value == 0);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  for (std::int32_t y = 0; y < original_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < original_pixels.width(); ++x) {
      CHECK(filtered->pixels().pixel(x, y)[3] ==
            original_pixels.pixel(x, y)[3]);
    }
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_surface_blur_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(18, 14, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 43 + y * 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 71) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 89 + y * 7) % 256);
      pixel[3] = 255U;
    }
  }
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Surface Blur"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* radius_slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    auto* threshold = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterThresholdSpin"));
    CHECK(radius != nullptr && radius_slider != nullptr &&
          threshold != nullptr);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 100.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(std::abs(radius->value() - 5.0) < 0.000001);
    CHECK(radius_slider->maximum() == 2400);
    CHECK(threshold->minimum() == 2 && threshold->maximum() == 255);
    CHECK(threshold->value() == 15);
    radius->setValue(100.0);
    CHECK(radius_slider->value() == radius_slider->maximum());
    radius->setValue(2.0);
    threshold->setValue(255);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_surface_blur")
      ->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filtered->bounds().x == original_bounds.x - 2);
  CHECK(filtered->bounds().y == original_bounds.y - 2);
  CHECK(filtered->bounds().width == original_bounds.width + 4);
  CHECK(filtered->bounds().height == original_bounds.height + 4);
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_smart_filter_high_pass_add_edit_and_reopen() {
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
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->value() - 10.0) < 0.000001);
    CHECK(std::abs(radius->minimum() - 0.1) < 0.000001);
    CHECK(std::abs(radius->maximum() - 1000.0) < 0.000001);
    CHECK(slider->maximum() == 1190);
    radius->setValue(4.25);
    applied = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_high_pass")->trigger();
  QApplication::processEvents();
  CHECK(applied);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto radius_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    CHECK(stack.entries.front().kind == patchy::SmartFilterKind::HighPass);
    const auto* high_pass = std::get_if<patchy::HighPassSmartFilter>(
        &stack.entries.front().parameters);
    CHECK(high_pass != nullptr);
    return high_pass->radius_pixels;
  };
  CHECK(std::abs(radius_at() - 4.25) < 0.000001);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("High Pass"));
  CHECK(label->toolTip().contains(QStringLiteral("4.25 px")));

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    CHECK(std::abs(radius->value() - 4.25) < 0.000001);
    radius->setValue(9.75);
    edited = true;
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  CHECK(std::abs(radius_at() - 9.75) < 0.000001);
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && filter_rect_equal(filtered->bounds(), original_bounds));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_high_pass.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::HighPass);
  const auto* reopened_high_pass = std::get_if<patchy::HighPassSmartFilter>(
      &reopened_entry.parameters);
  CHECK(reopened_high_pass != nullptr);
  CHECK(std::abs(reopened_high_pass->radius_pixels - 9.75) < 0.000001);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_high_pass_row", *active_row());
}

void ui_smart_filter_median_add_edit_and_reopen() {
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
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->value() - 1.0) < 0.000001);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 500.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(slider->maximum() == 2400);
    radius->setValue(2.75);
    applied = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_median")->trigger();
  QApplication::processEvents();
  CHECK(applied);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto radius_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    CHECK(stack.entries.front().kind == patchy::SmartFilterKind::Median);
    const auto* median = std::get_if<patchy::MedianSmartFilter>(
        &stack.entries.front().parameters);
    CHECK(median != nullptr);
    return median->radius_pixels;
  };
  CHECK(std::abs(radius_at() - 2.75) < 0.000001);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Median"));
  CHECK(label->toolTip().contains(QStringLiteral("2.75 px")));

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    CHECK(std::abs(radius->value() - 2.75) < 0.000001);
    radius->setValue(7.5);
    edited = true;
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  CHECK(std::abs(radius_at() - 7.5) < 0.000001);
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr &&
        filter_rect_equal(filtered->bounds(), original_bounds));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_median.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::Median);
  const auto* reopened_median = std::get_if<patchy::MedianSmartFilter>(
      &reopened_entry.parameters);
  CHECK(reopened_median != nullptr);
  CHECK(std::abs(reopened_median->radius_pixels - 7.5) < 0.000001);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_median_row", *active_row());
}

void ui_smart_filter_dust_and_scratches_add_edit_and_reopen() {
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
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool apply_callback_ran = false;
  bool apply_dialog_found = false;
  bool apply_controls_found = false;
  int apply_radius_minimum = -1;
  int apply_radius_maximum = -1;
  int apply_radius_value = -1;
  int apply_slider_minimum = -1;
  int apply_slider_maximum = -1;
  int apply_threshold_minimum = -1;
  int apply_threshold_maximum = -1;
  int apply_threshold_value = -1;
  QTimer::singleShot(0, [&] {
    apply_callback_ran = true;
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    apply_dialog_found = dialog != nullptr;
    if (dialog == nullptr) {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* visible_dialog = qobject_cast<QDialog*>(widget);
        if (visible_dialog != nullptr && visible_dialog->isVisible()) {
          visible_dialog->reject();
          break;
        }
      }
      return;
    }
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    auto* radius_slider =
        dialog->findChild<QSlider*>(QStringLiteral("filterRadiusSlider"));
    auto* threshold =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterThresholdSpin"));
    apply_controls_found =
        radius != nullptr && radius_slider != nullptr && threshold != nullptr;
    if (apply_controls_found) {
      apply_radius_minimum = radius->minimum();
      apply_radius_maximum = radius->maximum();
      apply_radius_value = radius->value();
      apply_slider_minimum = radius_slider->minimum();
      apply_slider_maximum = radius_slider->maximum();
      apply_threshold_minimum = threshold->minimum();
      apply_threshold_maximum = threshold->maximum();
      apply_threshold_value = threshold->value();
      radius->setValue(7);
      threshold->setValue(23);
    }
    dialog->accept();
  });
  require_action(window,
                 "filterAction_patchy_filters_dust_and_scratches")
      ->trigger();
  QApplication::processEvents();
  CHECK(apply_callback_ran);
  CHECK(apply_dialog_found);
  CHECK(apply_controls_found);
  CHECK(apply_radius_value == 1);
  CHECK(apply_radius_minimum == 1 && apply_radius_maximum == 100);
  CHECK(apply_slider_minimum == 1 && apply_slider_maximum == 25);
  CHECK(apply_threshold_value == 0);
  CHECK(apply_threshold_minimum == 0 && apply_threshold_maximum == 255);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto parameters_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    CHECK(stack.entries.front().kind ==
          patchy::SmartFilterKind::DustAndScratches);
    CHECK(stack.entries.front().native_name == "Dust && Scratches...");
    CHECK(stack.entries.front().native_class_id == "DstS");
    CHECK(stack.entries.front().native_filter_id == 0x44737453U);
    const auto* dust = std::get_if<patchy::DustAndScratchesSmartFilter>(
        &stack.entries.front().parameters);
    CHECK(dust != nullptr);
    return std::pair{dust->radius_pixels, dust->threshold};
  };
  CHECK((parameters_at() ==
         std::pair{std::int32_t{7}, std::int32_t{23}}));
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  // The native Smart Object fixture is a constant green rectangle, so Dust &
  // Scratches is correctly an identity while still creating an editable stack.
  CHECK(patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr &&
        label->text() == QStringLiteral("Dust & Scratches"));
  CHECK(label->toolTip().contains(QStringLiteral("7 px")));
  CHECK(label->toolTip().contains(QStringLiteral("23")));

  bool edit_callback_ran = false;
  bool edit_dialog_found = false;
  bool edit_controls_found = false;
  int edit_radius_value = -1;
  int edit_threshold_value = -1;
  QTimer::singleShot(20, [&] {
    edit_callback_ran = true;
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    edit_dialog_found = dialog != nullptr;
    if (dialog == nullptr) {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* visible_dialog = qobject_cast<QDialog*>(widget);
        if (visible_dialog != nullptr && visible_dialog->isVisible()) {
          visible_dialog->reject();
          break;
        }
      }
      return;
    }
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    auto* threshold =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterThresholdSpin"));
    edit_controls_found = radius != nullptr && threshold != nullptr;
    if (edit_controls_found) {
      edit_radius_value = radius->value();
      edit_threshold_value = threshold->value();
      radius->setValue(9);
      threshold->setValue(31);
    }
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edit_callback_ran; }));
  CHECK(edit_dialog_found);
  CHECK(edit_controls_found);
  CHECK(edit_radius_value == 7);
  CHECK(edit_threshold_value == 23);
  CHECK((parameters_at() ==
         std::pair{std::int32_t{9}, std::int32_t{31}}));
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr &&
        filter_rect_equal(filtered->bounds(), original_bounds));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_dust_and_scratches.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind ==
        patchy::SmartFilterKind::DustAndScratches);
  CHECK(reopened_entry.native_name == "Dust && Scratches...");
  const auto* reopened_dust =
      std::get_if<patchy::DustAndScratchesSmartFilter>(
          &reopened_entry.parameters);
  CHECK(reopened_dust != nullptr);
  CHECK(reopened_dust->radius_pixels == 9);
  CHECK(reopened_dust->threshold == 31);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_dust_and_scratches_row",
                       *active_row());
}

void ui_smart_filter_surface_blur_add_edit_and_reopen() {
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
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* radius_slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    auto* threshold = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterThresholdSpin"));
    CHECK(radius != nullptr && radius_slider != nullptr &&
          threshold != nullptr);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 100.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(std::abs(radius->value() - 5.0) < 0.000001);
    CHECK(radius_slider->maximum() == 2400);
    CHECK(threshold->minimum() == 2 && threshold->maximum() == 255);
    CHECK(threshold->value() == 15);
    radius->setValue(2.0);
    threshold->setValue(255);
    applied = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_surface_blur")
      ->trigger();
  QApplication::processEvents();
  CHECK(applied);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto parameters_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    const auto& entry = stack.entries.front();
    CHECK(entry.kind == patchy::SmartFilterKind::SurfaceBlur);
    CHECK(entry.native_name == "Surface Blur...");
    CHECK(entry.native_class_id == "surfaceBlur");
    CHECK(entry.native_filter_id == 854U);
    const auto* surface =
        std::get_if<patchy::SurfaceBlurSmartFilter>(&entry.parameters);
    CHECK(surface != nullptr);
    return std::pair{surface->radius_pixels, surface->threshold};
  };
  {
    const auto [radius, threshold] = parameters_at();
    CHECK(std::abs(radius - 2.0) < 0.000001);
    CHECK(threshold == 255);
  }
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filtered->bounds().x == original_bounds.x - 2);
  CHECK(filtered->bounds().y == original_bounds.y - 2);
  CHECK(filtered->bounds().width == original_bounds.width + 4);
  CHECK(filtered->bounds().height == original_bounds.height + 4);
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Surface Blur"));
  CHECK(label->toolTip().contains(QStringLiteral("Radius 2 px")));
  CHECK(label->toolTip().contains(QStringLiteral("Threshold 255")));

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* threshold = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterThresholdSpin"));
    CHECK(radius != nullptr && threshold != nullptr);
    CHECK(std::abs(radius->value() - 2.0) < 0.000001);
    CHECK(threshold->value() == 255);
    radius->setValue(9.25);
    threshold->setValue(31);
    edited = true;
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  {
    const auto [radius, threshold] = parameters_at();
    CHECK(std::abs(radius - 9.25) < 0.000001);
    CHECK(threshold == 31);
  }
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_surface_blur.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::SurfaceBlur);
  CHECK(reopened_entry.native_name == "Surface Blur...");
  CHECK(reopened_entry.native_class_id == "surfaceBlur");
  CHECK(reopened_entry.native_filter_id == 854U);
  const auto* reopened_surface =
      std::get_if<patchy::SurfaceBlurSmartFilter>(
          &reopened_entry.parameters);
  CHECK(reopened_surface != nullptr);
  CHECK(std::abs(reopened_surface->radius_pixels - 9.25) < 0.000001);
  CHECK(reopened_surface->threshold == 31);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_surface_blur_row", *active_row());
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

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests() {
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
      {"ui_smart_filter_blending_more_menu_cancel_apply_is_atomic",
       ui_smart_filter_blending_more_menu_cancel_apply_is_atomic},
      {"ui_smart_filter_authored_psd_reopens_with_native_stack_and_cache",
       ui_smart_filter_authored_psd_reopens_with_native_stack_and_cache},
      {"ui_smart_filter_multiple_gaussian_duplicate_reorder_delete_roundtrip",
       ui_smart_filter_multiple_gaussian_duplicate_reorder_delete_roundtrip},
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
      {"ui_smart_filter_linked_external_add_edit_toggle_lock_and_delete",
       ui_smart_filter_linked_external_add_edit_toggle_lock_and_delete},
      {"ui_gaussian_blur_normal_pixel_layer_stays_destructive",
       ui_gaussian_blur_normal_pixel_layer_stays_destructive},
      {"ui_high_pass_normal_pixel_layer_stays_destructive",
       ui_high_pass_normal_pixel_layer_stays_destructive},
      {"ui_median_normal_pixel_layer_stays_destructive",
       ui_median_normal_pixel_layer_stays_destructive},
      {"ui_dust_and_scratches_normal_pixel_layer_stays_destructive",
       ui_dust_and_scratches_normal_pixel_layer_stays_destructive},
      {"ui_surface_blur_normal_pixel_layer_stays_destructive",
       ui_surface_blur_normal_pixel_layer_stays_destructive},
      {"ui_smart_filter_high_pass_add_edit_and_reopen",
       ui_smart_filter_high_pass_add_edit_and_reopen},
      {"ui_smart_filter_median_add_edit_and_reopen",
       ui_smart_filter_median_add_edit_and_reopen},
      {"ui_smart_filter_dust_and_scratches_add_edit_and_reopen",
       ui_smart_filter_dust_and_scratches_add_edit_and_reopen},
      {"ui_smart_filter_surface_blur_add_edit_and_reopen",
       ui_smart_filter_surface_blur_add_edit_and_reopen},
  };
}
