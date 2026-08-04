// Multi-target Free Transform: Ctrl-T on a selected folder (or a multi-layer
// selection) transforms the flattened target set with one shared delta, linked
// raster masks ride along, and the whole commit is one undo entry. The
// single-layer path stays byte-pinned by the text_transform_commit suites.

#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_buffer.hpp"
#include "core/smart_object.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"

#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

patchy::Layer* find_group_layer(std::vector<patchy::Layer>& layers, const QString& name_contains) {
  for (auto& layer : layers) {
    if (layer.kind() == patchy::LayerKind::Group &&
        QString::fromStdString(layer.name()).contains(name_contains, Qt::CaseInsensitive)) {
      return &layer;
    }
    if (layer.kind() == patchy::LayerKind::Group) {
      if (auto* found = find_group_layer(layer.children(), name_contains); found != nullptr) {
        return found;
      }
    }
  }
  return nullptr;
}

void select_layer_rows_by_id(QListWidget& list, const std::vector<patchy::LayerId>& ids) {
  list.clearSelection();
  bool first = true;
  for (int row = 0; row < list.count(); ++row) {
    auto* item = list.item(row);
    const auto id = static_cast<patchy::LayerId>(item->data(Qt::UserRole).toULongLong());
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
      continue;
    }
    if (first) {
      list.setCurrentItem(item);
      first = false;
    }
    item->setSelected(true);
  }
  QApplication::processEvents();
}

// resample_transformed_gray8: identity is byte-identical for every
// interpolation (the +0.5 pixel-center convention lands exactly on texels),
// and samples outside the source read as default_color, never 0.
void gray8_resample_identity_and_default_fill() {
  patchy::PixelBuffer source(8, 8, patchy::PixelFormat::gray8());
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      source.pixel(x, y)[0] = static_cast<std::uint8_t>(x * 30 + y * 3);
    }
  }

  for (const auto interpolation : {patchy::ui::CanvasWidget::TransformInterpolation::NearestNeighbor,
                                   patchy::ui::CanvasWidget::TransformInterpolation::Bilinear,
                                   patchy::ui::CanvasWidget::TransformInterpolation::Bicubic}) {
    const auto identity = patchy::ui::resample_transformed_gray8(source, 0, QTransform(), interpolation);
    CHECK(identity.bounds.x == 0 && identity.bounds.y == 0);
    CHECK(identity.bounds.width == 8 && identity.bounds.height == 8);
    for (std::int32_t y = 0; y < 8; ++y) {
      for (std::int32_t x = 0; x < 8; ++x) {
        CHECK(identity.pixels.pixel(x, y)[0] == source.pixel(x, y)[0]);
      }
    }
  }

  // 2x nearest duplicates each texel into a 2x2 block.
  const auto doubled = patchy::ui::resample_transformed_gray8(
      source, 0, QTransform::fromScale(2.0, 2.0),
      patchy::ui::CanvasWidget::TransformInterpolation::NearestNeighbor);
  CHECK(doubled.bounds.width == 16 && doubled.bounds.height == 16);
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      CHECK(doubled.pixels.pixel(x, y)[0] == source.pixel(x / 2, y / 2)[0]);
    }
  }

  // A rotation leaves the output AABB's corners outside the source; those
  // pixels must read the mask's default color for both defaults.
  QTransform rotated;
  rotated.translate(4.0, 4.0);
  rotated.rotate(45.0);
  rotated.translate(-4.0, -4.0);
  for (const std::uint8_t default_color : {std::uint8_t{0}, std::uint8_t{255}}) {
    const auto result = patchy::ui::resample_transformed_gray8(
        source, default_color, rotated, patchy::ui::CanvasWidget::TransformInterpolation::Bicubic);
    CHECK(result.pixels.pixel(0, 0)[0] == default_color);
    CHECK(result.pixels.pixel(result.bounds.width - 1, 0)[0] == default_color);
    CHECK(result.pixels.pixel(0, result.bounds.height - 1)[0] == default_color);
    CHECK(result.pixels.pixel(result.bounds.width - 1, result.bounds.height - 1)[0] == default_color);
  }
}

// Ctrl-T on a selected folder starts a multi-target session over the flattened
// leaves (nested folders included); a numeric 200% scale about the union center
// maps every member's bounds through the one shared delta, and a single Undo
// restores the whole set.
void ui_group_free_transform_scales_folder_members_together() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Arcade Folder", patchy::LayerKind::Group);
  auto red = patchy::Layer(document.allocate_layer_id(), "Red Member",
                           solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  const auto red_id = red.id();
  red.set_bounds(patchy::Rect{40, 40, 20, 20});
  folder.add_child(std::move(red));
  patchy::Layer nested(document.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group);
  auto blue = patchy::Layer(document.allocate_layer_id(), "Blue Member",
                            solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  const auto blue_id = blue.id();
  blue.set_bounds(patchy::Rect{100, 40, 20, 20});
  nested.add_child(std::move(blue));
  folder.add_child(std::move(nested));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Group Transform Scale"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto before = patchy::ui::qimage_from_document(doc, true);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Arcade Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  // The box is the union of the members' content rects: (40,40)-(120,60).
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  CHECK(std::abs(state->reference_position.x() - 80.0) < 0.51);
  CHECK(std::abs(state->reference_position.y() - 50.0) < 0.51);

  CHECK(canvas->set_transform_controls_state(state->reference_position, 200.0, 200.0, 0.0));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // Scale 2x about (80,50): red (40,40,20,20) -> (0,30,40,40), blue
  // (100,40,20,20) -> (120,30,40,40).
  const auto* red_layer = doc.find_layer(red_id);
  const auto* blue_layer = doc.find_layer(blue_id);
  CHECK(red_layer != nullptr && blue_layer != nullptr);
  CHECK(red_layer->bounds().x == 0 && red_layer->bounds().y == 30);
  CHECK(red_layer->bounds().width == 40 && red_layer->bounds().height == 40);
  CHECK(blue_layer->bounds().x == 120 && blue_layer->bounds().y == 30);
  CHECK(blue_layer->bounds().width == 40 && blue_layer->bounds().height == 40);

  // Exactly one undo entry restores every member byte-identically.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(doc.find_layer(red_id)->bounds().x == 40 && doc.find_layer(red_id)->bounds().y == 40);
  CHECK(doc.find_layer(blue_id)->bounds().x == 100 && doc.find_layer(blue_id)->bounds().y == 40);
  const auto after_undo = patchy::ui::qimage_from_document(doc, true);
  CHECK(images_equal_rgba(before, after_undo));
}

// Ctrl-T with two sibling layers selected transforms them together with one
// shared delta (translation via the numeric reference-position field) and one
// undo entry.
void ui_multi_select_free_transform_transforms_selection_together() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto red = patchy::Layer(document.allocate_layer_id(), "Red Sibling",
                           solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  const auto red_id = red.id();
  red.set_bounds(patchy::Rect{30, 30, 20, 20});
  document.add_layer(std::move(red));
  auto blue = patchy::Layer(document.allocate_layer_id(), "Blue Sibling",
                            solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  const auto blue_id = blue.id();
  blue.set_bounds(patchy::Rect{90, 80, 20, 20});
  document.add_layer(std::move(blue));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Select Transform"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto before = patchy::ui::qimage_from_document(doc, true);

  select_layer_rows_by_id(*layer_list, {red_id, blue_id});
  CHECK(layer_list->selectedItems().size() == 2);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  CHECK(canvas->set_transform_controls_state(state->reference_position + QPointF(15.0, 10.0), 100.0, 100.0, 0.0));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  CHECK(doc.find_layer(red_id)->bounds().x == 45 && doc.find_layer(red_id)->bounds().y == 40);
  CHECK(doc.find_layer(blue_id)->bounds().x == 105 && doc.find_layer(blue_id)->bounds().y == 90);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(doc.find_layer(red_id)->bounds().x == 30);
  CHECK(doc.find_layer(blue_id)->bounds().x == 90);
  CHECK(images_equal_rgba(before, patchy::ui::qimage_from_document(doc, true)));
}

// Linked raster masks ride along with a folder transform: the folder's own
// mask, a member's linked mask, and an adjustment layer's mask all resample
// through the shared delta; an UNLINKED member mask stays put.
void ui_group_transform_resamples_linked_masks() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Masked Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::PixelBuffer folder_mask(80, 20, patchy::PixelFormat::gray8());
  folder_mask.clear(255);
  folder.set_mask(patchy::LayerMask{patchy::Rect{40, 40, 80, 20}, std::move(folder_mask), 0, false});

  auto linked = patchy::Layer(document.allocate_layer_id(), "Linked Mask Member",
                              solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  const auto linked_id = linked.id();
  linked.set_bounds(patchy::Rect{40, 40, 20, 20});
  patchy::PixelBuffer linked_mask(20, 20, patchy::PixelFormat::gray8());
  linked_mask.clear(255);
  linked.set_mask(patchy::LayerMask{patchy::Rect{40, 40, 20, 20}, std::move(linked_mask), 0, false});
  folder.add_child(std::move(linked));

  auto unlinked = patchy::Layer(document.allocate_layer_id(), "Unlinked Mask Member",
                                solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  const auto unlinked_id = unlinked.id();
  unlinked.set_bounds(patchy::Rect{100, 40, 20, 20});
  patchy::PixelBuffer unlinked_mask(20, 20, patchy::PixelFormat::gray8());
  unlinked_mask.clear(255);
  unlinked.set_mask(patchy::LayerMask{patchy::Rect{100, 40, 20, 20}, std::move(unlinked_mask), 0, false});
  patchy::set_layer_mask_linked(unlinked, false);
  folder.add_child(std::move(unlinked));

  patchy::AdjustmentSettings warm;
  warm.kind = patchy::AdjustmentKind::ColorBalance;
  warm.color_balance = patchy::ColorBalanceAdjustment{35, 0, 0};
  patchy::Layer adjustment(document.allocate_layer_id(), "Warmth", patchy::LayerKind::Adjustment);
  const auto adjustment_id = adjustment.id();
  patchy::configure_adjustment_layer(adjustment, warm);
  patchy::PixelBuffer adjustment_mask(80, 20, patchy::PixelFormat::gray8());
  adjustment_mask.clear(255);
  adjustment.set_mask(patchy::LayerMask{patchy::Rect{40, 40, 80, 20}, std::move(adjustment_mask), 0, false});
  folder.add_child(std::move(adjustment));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Group Transform Masks"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  select_layer_rows_by_id(*layer_list, {folder_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  CHECK(canvas->set_transform_controls_state(state->reference_position, 200.0, 200.0, 0.0));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // Union box (40,40)-(120,60), center (80,50); 2x maps (40,40,80,20) to
  // (0,30,160,40) and (40,40,20,20) to (0,30,40,40).
  const auto* folder_layer = doc.find_layer(folder_id);
  CHECK(folder_layer != nullptr && folder_layer->mask().has_value());
  CHECK(folder_layer->mask()->bounds.x == 0 && folder_layer->mask()->bounds.y == 30);
  CHECK(folder_layer->mask()->bounds.width == 160 && folder_layer->mask()->bounds.height == 40);
  CHECK(folder_layer->mask()->pixels.pixel(80, 20)[0] == 255);

  const auto* linked_layer = doc.find_layer(linked_id);
  CHECK(linked_layer != nullptr && linked_layer->mask().has_value());
  CHECK(linked_layer->mask()->bounds.x == 0 && linked_layer->mask()->bounds.y == 30);
  CHECK(linked_layer->mask()->bounds.width == 40 && linked_layer->mask()->bounds.height == 40);
  CHECK(linked_layer->mask()->pixels.pixel(20, 20)[0] == 255);

  const auto* unlinked_layer = doc.find_layer(unlinked_id);
  CHECK(unlinked_layer != nullptr && unlinked_layer->mask().has_value());
  CHECK(unlinked_layer->mask()->bounds.x == 100 && unlinked_layer->mask()->bounds.y == 40);
  CHECK(unlinked_layer->mask()->bounds.width == 20 && unlinked_layer->mask()->bounds.height == 20);
  // The unlinked member's pixels still transformed.
  CHECK(unlinked_layer->bounds().x == 120 && unlinked_layer->bounds().y == 30);

  const auto* adjustment_layer = doc.find_layer(adjustment_id);
  CHECK(adjustment_layer != nullptr && adjustment_layer->mask().has_value());
  CHECK(adjustment_layer->mask()->bounds.x == 0 && adjustment_layer->mask()->bounds.y == 30);
  CHECK(adjustment_layer->mask()->bounds.width == 160 && adjustment_layer->mask()->bounds.height == 40);
}

// A position-locked member or an unparsed smart object member (no placement
// quad to ride the transform) refuses the whole folder session; scaling the
// rest of a folder around a pinned member would tear the artwork apart. A
// preview-locked-but-PARSED smart object member (warp/non-affine/external)
// does NOT refuse: its quads map per corner like Photoshop.
void ui_group_transform_refuses_locked_and_unparsed_members() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer locked_folder(document.allocate_layer_id(), "Locked Folder", patchy::LayerKind::Group);
  const auto locked_folder_id = locked_folder.id();
  auto locked = patchy::Layer(document.allocate_layer_id(), "Locked Member",
                              solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  locked.set_bounds(patchy::Rect{40, 40, 20, 20});
  patchy::set_layer_lock_flags(locked, patchy::kLayerLockPosition);
  locked_folder.add_child(std::move(locked));
  auto free_member = patchy::Layer(document.allocate_layer_id(), "Free Member",
                                   solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  free_member.set_bounds(patchy::Rect{100, 40, 20, 20});
  locked_folder.add_child(std::move(free_member));
  document.add_layer(std::move(locked_folder));

  patchy::Layer so_folder(document.allocate_layer_id(), "Preview Locked Folder", patchy::LayerKind::Group);
  const auto so_folder_id = so_folder.id();
  auto placed = patchy::Layer(document.allocate_layer_id(), "Preview Locked SO",
                              solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 180, 90)));
  placed.set_bounds(patchy::Rect{40, 100, 20, 20});
  patchy::SmartObjectPlacement placement;
  placement.uuid = "test-preview-locked";
  placement.transform = {40.0, 100.0, 60.0, 100.0, 60.0, 120.0, 40.0, 120.0};
  placement.width = 20.0;
  placement.height = 20.0;
  patchy::set_layer_smart_object_metadata(placed, placement, "test-preview-locked", "SoLd", "warp",
                                          "photoshop");
  so_folder.add_child(std::move(placed));
  document.add_layer(std::move(so_folder));

  patchy::Layer unparsed_folder(document.allocate_layer_id(), "Unparsed Folder", patchy::LayerKind::Group);
  const auto unparsed_folder_id = unparsed_folder.id();
  auto unparsed = patchy::Layer(document.allocate_layer_id(), "Unparsed SO",
                                solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(200, 160, 40)));
  unparsed.set_bounds(patchy::Rect{100, 100, 20, 20});
  patchy::SmartObjectPlacement unparsed_placement;
  unparsed_placement.transform = {100.0, 100.0, 120.0, 100.0, 120.0, 120.0, 100.0, 120.0};
  patchy::set_layer_smart_object_metadata(unparsed, unparsed_placement, "", "SoLd", "unparsed",
                                          "photoshop");
  unparsed_folder.add_child(std::move(unparsed));
  document.add_layer(std::move(unparsed_folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Group Transform Refusals"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto before = patchy::ui::qimage_from_document(doc, true);

  select_layer_rows_by_id(*layer_list, {locked_folder_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  select_layer_rows_by_id(*layer_list, {unparsed_folder_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // The preview-locked-but-parsed member starts a session instead.
  select_layer_rows_by_id(*layer_list, {so_folder_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  CHECK(images_equal_rgba(before, patchy::ui::qimage_from_document(doc, true)));
}

// During a folder session the options-bar warp toggle grays out and
// begin_warp_transform refuses while KEEPING the session alive; Esc cancels
// without touching the document.
void ui_group_transform_session_disables_warp_and_esc_cancels() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer folder(document.allocate_layer_id(), "Warp Refusal Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  auto red = patchy::Layer(document.allocate_layer_id(), "Red Member",
                           solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  red.set_bounds(patchy::Rect{40, 40, 20, 20});
  folder.add_child(std::move(red));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Group Transform Warp Refusal"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto before = patchy::ui::qimage_from_document(doc, true);

  select_layer_rows_by_id(*layer_list, {folder_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  auto* warp_button = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
  CHECK(warp_button != nullptr);
  CHECK(!warp_button->isEnabled());

  CHECK(!canvas->begin_warp_transform());
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(images_equal_rgba(before, patchy::ui::qimage_from_document(doc, true)));
}

// Re-emitting the same folder selection (panel refreshes re-push it) keeps the
// session alive; selecting a different layer commits it.
void ui_group_transform_selection_reemission_keeps_session() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer folder(document.allocate_layer_id(), "Sticky Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  auto red = patchy::Layer(document.allocate_layer_id(), "Red Member",
                           solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  red.set_bounds(patchy::Rect{40, 40, 20, 20});
  folder.add_child(std::move(red));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Group Transform Teardown"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  select_layer_rows_by_id(*layer_list, {folder_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  // The same selection pushed again keeps the session.
  canvas->set_selected_layer_ids({folder_id});
  CHECK(canvas->free_transform_active());

  // Selecting another layer commits (ends) it.
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// Show Transform Controls with the Move tool frames a selected folder or a
// multi-layer selection (the flattened target union, matching the session a
// handle grab starts) without needing Ctrl-T; grabbing a corner starts the
// multi session, Esc cancels untouched, and a folder whose session would
// refuse (position-locked member) shows no box.
void ui_move_passive_transform_controls_frame_folder_and_multi_selection() {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Passive Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  auto red = patchy::Layer(document.allocate_layer_id(), "Red Member",
                           solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  const auto red_id = red.id();
  red.set_bounds(patchy::Rect{40, 40, 20, 20});
  folder.add_child(std::move(red));
  auto blue = patchy::Layer(document.allocate_layer_id(), "Blue Member",
                            solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  const auto blue_id = blue.id();
  blue.set_bounds(patchy::Rect{100, 40, 20, 20});
  folder.add_child(std::move(blue));
  document.add_layer(std::move(folder));

  patchy::Layer locked_folder(document.allocate_layer_id(), "Locked Folder", patchy::LayerKind::Group);
  const auto locked_folder_id = locked_folder.id();
  auto locked = patchy::Layer(document.allocate_layer_id(), "Locked Member",
                              solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 180, 90)));
  locked.set_bounds(patchy::Rect{40, 100, 20, 20});
  patchy::set_layer_lock_flags(locked, patchy::kLayerLockPosition);
  locked_folder.add_child(std::move(locked));
  document.add_layer(std::move(locked_folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Passive Group Controls"));
  auto* canvas = require_canvas(window);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto before = patchy::ui::qimage_from_document(doc, true);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  canvas->set_selected_layer_ids({folder_id});
  QApplication::processEvents();

  // Union of the members' content rects: (40,40)-(120,60), center (80,50).
  const auto passive = canvas->transform_controls_state();
  CHECK(passive.has_value());
  CHECK(!passive->active);
  CHECK(std::abs(passive->reference_position.x() - 80.0) < 0.51);
  CHECK(std::abs(passive->reference_position.y() - 50.0) < 0.51);

  const auto corner = canvas->widget_position_for_document_point(QPoint(120, 60));
  send_mouse(*canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  // Grabbing the corner starts the multi session without Ctrl-T.
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(10, 8), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner + QPoint(10, 8), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(images_equal_rgba(before, patchy::ui::qimage_from_document(doc, true)));

  // A multi-layer selection of the two members frames the same union.
  canvas->set_selected_layer_ids({red_id, blue_id});
  QApplication::processEvents();
  const auto multi = canvas->transform_controls_state();
  CHECK(multi.has_value());
  CHECK(!multi->active);
  CHECK(std::abs(multi->reference_position.x() - 80.0) < 0.51);
  CHECK(std::abs(multi->reference_position.y() - 50.0) < 0.51);

  // A folder whose session would refuse shows no box.
  canvas->set_selected_layer_ids({locked_folder_id});
  QApplication::processEvents();
  CHECK(!canvas->transform_controls_state().has_value());
}

// The reported repro: the pinball PSD's folder must accept Ctrl-T, scale, and
// restore byte-identically on one Undo.
void ui_pinball_folder_free_transform_end_to_end() {
  const auto path = patchy::test::local_psd_fixture_path("pinball_from_photoshop.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local pinball_from_photoshop.psd fixture missing" << std::endl;
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Pinball Group Transform"));
  accept_missing_psd_text_font_warning_if_present();
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  auto* folder = find_group_layer(doc.layers(), QStringLiteral("EFFECT"));
  if (folder == nullptr) {
    folder = find_group_layer(doc.layers(), QStringLiteral("ARCADE"));
  }
  CHECK(folder != nullptr);
  const auto folder_id = folder->id();

  const auto before = patchy::ui::qimage_from_document(doc, true);

  select_layer_rows_by_id(*layer_list, {folder_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  // The reported bug: this used to fail with "Select a pixel layer to transform".
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  CHECK(canvas->set_transform_controls_state(state->reference_position, 110.0, 110.0, 0.0));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto after = patchy::ui::qimage_from_document(doc, true);
  CHECK(!images_equal_rgba(before, after));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(images_equal_rgba(before, patchy::ui::qimage_from_document(doc, true)));
}

patchy::Layer* find_locked_smart_object(std::vector<patchy::Layer>& layers, const std::string& reason) {
  for (auto& layer : layers) {
    if (layer.kind() == patchy::LayerKind::Group) {
      if (auto* found = find_locked_smart_object(layer.children(), reason); found != nullptr) {
        return found;
      }
      continue;
    }
    if (patchy::smart_object_lock_reason(layer) == reason) {
      return &layer;
    }
  }
  return nullptr;
}

// The reported repro: selecting EVERY row in the retronight poster (which
// contains perspective-placed "non_affine" preview-locked smart objects) and
// hitting Ctrl-T must transform everything together like Photoshop. The locked
// smart objects' Trnf AND nonAffineTransform quads map per corner through the
// shared delta, and one Undo restores the whole document byte-identically.
void ui_select_all_free_transform_transforms_retronight_poster() {
  const auto path = patchy::test::local_psd_fixture_path("pinball_retronight_poster_a3.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local pinball_retronight_poster_a3.psd fixture missing" << std::endl;
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Retronight Select All"));
  accept_missing_psd_text_font_warning_if_present();
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  auto* grid = find_locked_smart_object(doc.layers(), "non_affine");
  CHECK(grid != nullptr);
  const auto grid_id = grid->id();
  const auto placement_before = patchy::smart_object_placement_from_layer(*grid);
  CHECK(placement_before.has_value());
  CHECK(placement_before->non_affine_transform.has_value());
  const auto before = patchy::ui::qimage_from_document(doc, true);

  layer_list->selectAll();
  QApplication::processEvents();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  // The reported bug: this used to refuse with "Select a pixel layer to transform".
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());

  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  const auto center = state->reference_position;
  CHECK(canvas->set_transform_controls_state(center, 150.0, 150.0, 0.0));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // Both stored quads scaled 1.5x about the session box center.
  const auto* grid_after = doc.find_layer(grid_id);
  CHECK(grid_after != nullptr);
  const auto placement_after = patchy::smart_object_placement_from_layer(*grid_after);
  CHECK(placement_after.has_value());
  CHECK(placement_after->non_affine_transform.has_value());
  const auto expect_scaled = [&center](double before_value, double after_value, double center_value) {
    CHECK(std::abs(after_value - (center_value + (before_value - center_value) * 1.5)) < 0.01);
  };
  for (std::size_t i = 0; i < 8U; i += 2U) {
    expect_scaled(placement_before->transform[i], placement_after->transform[i], center.x());
    expect_scaled(placement_before->transform[i + 1U], placement_after->transform[i + 1U], center.y());
    expect_scaled((*placement_before->non_affine_transform)[i], (*placement_after->non_affine_transform)[i],
                  center.x());
    expect_scaled((*placement_before->non_affine_transform)[i + 1U],
                  (*placement_after->non_affine_transform)[i + 1U], center.y());
  }

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(images_equal_rgba(before, patchy::ui::qimage_from_document(doc, true)));

  // A single selected preview-locked smart object routes to the multi path
  // and transforms too (the single-layer path used to refuse it). The GRID
  // row sits inside collapsed folders, so push the selection directly.
  canvas->set_selected_layer_ids({grid_id});
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_is_multi_target());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

}  // namespace

std::vector<patchy::test::TestCase> group_transform_tests() {
  return {
      {"gray8_resample_identity_and_default_fill", gray8_resample_identity_and_default_fill},
      {"ui_group_free_transform_scales_folder_members_together",
       ui_group_free_transform_scales_folder_members_together},
      {"ui_multi_select_free_transform_transforms_selection_together",
       ui_multi_select_free_transform_transforms_selection_together},
      {"ui_group_transform_resamples_linked_masks", ui_group_transform_resamples_linked_masks},
      {"ui_group_transform_refuses_locked_and_unparsed_members",
       ui_group_transform_refuses_locked_and_unparsed_members},
      {"ui_group_transform_session_disables_warp_and_esc_cancels",
       ui_group_transform_session_disables_warp_and_esc_cancels},
      {"ui_group_transform_selection_reemission_keeps_session",
       ui_group_transform_selection_reemission_keeps_session},
      {"ui_move_passive_transform_controls_frame_folder_and_multi_selection",
       ui_move_passive_transform_controls_frame_folder_and_multi_selection},
      {"ui_pinball_folder_free_transform_end_to_end", ui_pinball_folder_free_transform_end_to_end},
      {"ui_select_all_free_transform_transforms_retronight_poster",
       ui_select_all_free_transform_transforms_retronight_poster},
  };
}
