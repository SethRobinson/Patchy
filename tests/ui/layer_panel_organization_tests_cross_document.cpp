// Cross-document layer copies (a Layers-panel drag dropped on another document's
// canvas or tab), a part file of the layer_panel_organization group (the
// aggregator in layer_panel_organization_tests.cpp appends this part's vector).

#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/smart_object.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/main_window.hpp"

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

constexpr int kSourceWidth = 64;
constexpr int kSourceHeight = 48;

patchy::PixelBuffer opaque_pixels(int width, int height, QColor color) {
  return solid_pixels(width, height, patchy::PixelFormat::rgba8(), color);
}

patchy::Document plain_document(int width, int height, QColor color) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", opaque_pixels(width, height, color));
  return document;
}

// Bottom to top: Background, "Mark" (20x12 at 10,8), folder "Set" holding "Inner"
// (8x8 at 40,30).
patchy::Document cross_document_source() {
  auto document = plain_document(kSourceWidth, kSourceHeight, QColor(255, 255, 255));
  patchy::Layer mark(document.allocate_layer_id(), "Mark", opaque_pixels(20, 12, QColor(200, 30, 30)));
  mark.set_bounds(patchy::Rect{10, 8, 20, 12});
  document.add_layer(std::move(mark));
  patchy::Layer group(document.allocate_layer_id(), "Set", patchy::LayerKind::Group);
  patchy::Layer inner(document.allocate_layer_id(), "Inner", opaque_pixels(8, 8, QColor(30, 200, 30)));
  inner.set_bounds(patchy::Rect{40, 30, 8, 8});
  group.add_child(std::move(inner));
  document.add_layer(std::move(group));
  // "Mark" is the active layer: filters and the dialog act on a pixel layer.
  document.set_active_layer(document.layers()[1].id());
  return document;
}

patchy::LayerId layer_id_named(const patchy::Document& document, const char* name) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return layer.id();
    }
    for (const auto& child : layer.children()) {
      if (child.name() == name) {
        return child.id();
      }
    }
  }
  CHECK(false);
  return 0;
}

void collect_ids(const std::vector<patchy::Layer>& layers, std::set<patchy::LayerId>& ids) {
  for (const auto& layer : layers) {
    ids.insert(layer.id());
    collect_ids(layer.children(), ids);
  }
}

struct TwoDocuments {
  patchy::ui::CanvasWidget* target_canvas{nullptr};
  patchy::Document* target_document{nullptr};
  patchy::ui::CanvasWidget* source_canvas{nullptr};
  patchy::Document* source_document{nullptr};
  std::int64_t source_session{0};
};

// Opens the target first, then the source, which stays the active document.
TwoDocuments open_two_documents(patchy::ui::MainWindow& window, patchy::Document target) {
  TwoDocuments docs;
  window.add_document_session(std::move(target), QStringLiteral("Target"));
  QApplication::processEvents();
  docs.target_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  docs.target_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, docs.target_canvas);
  window.add_document_session(cross_document_source(), QStringLiteral("Source"));
  QApplication::processEvents();
  docs.source_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  docs.source_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, docs.source_canvas);
  docs.source_session = patchy::ui::MainWindowTestAccess::session_id_for_canvas(window, docs.source_canvas);
  CHECK(docs.target_canvas != nullptr && docs.target_document != nullptr);
  CHECK(docs.source_canvas != nullptr && docs.source_document != nullptr);
  CHECK(docs.source_canvas != docs.target_canvas);
  CHECK(docs.source_session > 0);
  return docs;
}

void ui_layer_drag_to_other_document_tab_copies_above_active() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  auto target = plain_document(kSourceWidth, kSourceHeight, QColor(40, 40, 40));
  target.add_pixel_layer("Top", opaque_pixels(kSourceWidth, kSourceHeight, QColor(0, 0, 0, 0)));
  target.set_active_layer(target.layers().front().id());
  const auto docs = open_two_documents(window, std::move(target));

  const auto mark_id = layer_id_named(*docs.source_document, "Mark");
  const auto set_id = layer_id_named(*docs.source_document, "Set");
  const auto inner_id = layer_id_named(*docs.source_document, "Inner");
  auto* tab_bar = tabs->tabBar();
  CHECK(tab_bar != nullptr);
  const auto target_tab = tabs->indexOf(docs.target_canvas);
  CHECK(target_tab >= 0);

  // The folder's child rides inside its folder (root normalization). A drop on
  // the target's TAB keeps the source coordinates: the documents share
  // dimensions.
  bool entered = false;
  send_layer_drop_to_widget(*tab_bar, tab_bar->tabRect(target_tab).center(), {set_id, inner_id, mark_id},
                            docs.source_session, Qt::NoModifier, &entered);
  CHECK(entered);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == docs.target_canvas);
  {
    const auto& layers = std::as_const(*docs.target_document).layers();
    CHECK(layers.size() == 4);
    CHECK(layers[0].name() == "Background");
    CHECK(layers[1].name() == "Mark");
    CHECK(layers[2].name() == "Set");
    CHECK(layers[3].name() == "Top");
    CHECK(layers[2].children().size() == 1);
    CHECK(layers[2].children()[0].name() == "Inner");
    CHECK(layers[1].bounds().x == 10 && layers[1].bounds().y == 8);
    CHECK(layers[2].children()[0].bounds().x == 40 && layers[2].children()[0].bounds().y == 30);
    CHECK(docs.target_document->active_layer_id() == layers[2].id());
    std::set<patchy::LayerId> target_ids;
    collect_ids(layers, target_ids);
    CHECK(target_ids.size() == 5);
  }
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, docs.target_canvas) == 1);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, docs.source_canvas) == 0);
  CHECK(std::as_const(*docs.source_document).layers().size() == 3);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->selectedItems().size() == 2);

  // A name the target already has earns the copy suffix, and the copy lands
  // above the target's active layer (the Set copy).
  const auto background_id = layer_id_named(*docs.source_document, "Background");
  patchy::ui::MainWindowTestAccess::activate_canvas(window, docs.source_canvas);
  QApplication::processEvents();
  send_layer_drop_to_widget(*tab_bar, tab_bar->tabRect(tabs->indexOf(docs.target_canvas)).center(),
                            {background_id}, docs.source_session, Qt::NoModifier, &entered);
  CHECK(entered);
  CHECK(std::as_const(*docs.target_document).layers().size() == 5);
  CHECK(std::as_const(*docs.target_document).layers()[3].name() == "Background copy");
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, docs.target_canvas) == 2);
}

void ui_layer_drag_same_document_canvas_is_ignored() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(cross_document_source(), QStringLiteral("Only"));
  QApplication::processEvents();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, canvas);
  const auto session = patchy::ui::MainWindowTestAccess::session_id_for_canvas(window, canvas);
  const auto mark_id = layer_id_named(*document, "Mark");

  // Photoshop does nothing when a layer is dragged onto its own document.
  bool entered = true;
  send_layer_drop_to_widget(*canvas, canvas->widget_position_for_document_point(QPoint(30, 20)), {mark_id},
                            session, Qt::NoModifier, &entered);
  CHECK(!entered);
  CHECK(std::as_const(*document).layers().size() == 3);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 0);
}

void ui_layer_drag_unknown_source_session_is_ignored() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto docs = open_two_documents(window, plain_document(kSourceWidth, kSourceHeight, QColor(40, 40, 40)));
  const auto mark_id = layer_id_named(*docs.source_document, "Mark");
  const auto position = docs.target_canvas->widget_position_for_document_point(QPoint(30, 20));

  // An unknown session (closed, or another process) and a missing token both
  // refuse: the ids would resolve against the wrong document.
  bool entered = true;
  send_layer_drop_to_widget(*docs.target_canvas, position, {mark_id}, docs.source_session + 424242,
                            Qt::NoModifier, &entered);
  CHECK(!entered);
  send_layer_drop_to_widget(*docs.target_canvas, position, {mark_id}, std::nullopt, Qt::NoModifier, &entered);
  CHECK(!entered);
  CHECK(std::as_const(*docs.target_document).layers().size() == 1);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, docs.target_canvas) == 0);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == docs.source_canvas);
}

void ui_layer_drag_to_other_document_respects_edit_lock() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto docs = open_two_documents(window, plain_document(kSourceWidth, kSourceHeight, QColor(40, 40, 40)));
  const auto mark_id = layer_id_named(*docs.source_document, "Mark");

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      // Every session canvas is locked while a preview dialog is open; a layer
      // drop on a background document is refused like any other edit.
      bool entered = true;
      send_layer_drop_to_widget(*docs.target_canvas,
                                docs.target_canvas->widget_position_for_document_point(QPoint(30, 20)), {mark_id},
                                docs.source_session, Qt::NoModifier, &entered);
      CHECK(!entered);
      CHECK(std::as_const(*docs.target_document).layers().size() == 1);
      CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == docs.source_canvas);
      saw_dialog = true;
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, docs.target_canvas) == 0);
}

void ui_layer_drag_group_mask_style_and_shape_survive() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(plain_document(128, 96, QColor(40, 40, 40)), QStringLiteral("Target"));
  QApplication::processEvents();
  auto* target_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* target_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, target_canvas);

  // Source: a folder holding a masked, styled pixel layer (20x12 at 10,8) and a
  // shape layer (a 20x10 rectangle at 8,8).
  auto source = plain_document(kSourceWidth, kSourceHeight, QColor(255, 255, 255));
  patchy::Layer group(source.allocate_layer_id(), "Set", patchy::LayerKind::Group);
  patchy::Layer inner(source.allocate_layer_id(), "Inner", opaque_pixels(20, 12, QColor(200, 30, 30)));
  inner.set_bounds(patchy::Rect{10, 8, 20, 12});
  patchy::PixelBuffer mask_pixels(20, 12, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  inner.set_mask(patchy::LayerMask{patchy::Rect{10, 8, 20, 12}, std::move(mask_pixels), 255, false});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  inner.layer_style().drop_shadows.push_back(shadow);
  group.add_child(std::move(inner));
  source.add_layer(std::move(group));
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = 8;
  params.top = 8;
  params.right = 28;
  params.bottom = 18;
  patchy::VectorShapeContent shape;
  shape.path.subpaths = patchy::generate_live_shape_subpaths(params);
  shape.fill.color = patchy::RgbColor{20, 80, 140};
  patchy::Layer shape_layer(source.allocate_layer_id(), "Shape", patchy::LayerKind::Pixel);
  shape_layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  shape_layer.set_vector_shape(std::move(shape));
  patchy::update_vector_shape_raster(shape_layer, patchy::Rect::from_size(kSourceWidth, kSourceHeight),
                                     &std::as_const(source).metadata().patterns);
  source.add_layer(std::move(shape_layer));
  window.add_document_session(std::move(source), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* source_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* source_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, source_canvas);
  const auto source_session = patchy::ui::MainWindowTestAccess::session_id_for_canvas(window, source_canvas);
  const auto set_id = layer_id_named(*source_document, "Set");
  const auto shape_id = layer_id_named(*source_document, "Shape");

  // The copied set's extent, (8,8)-(30,20), centers on the drop point (64,48):
  // a (45, 34) shift for every layer, mask included.
  bool entered = false;
  send_layer_drop_to_widget(*target_canvas, target_canvas->widget_position_for_document_point(QPoint(64, 48)),
                            {shape_id, set_id}, source_session, Qt::NoModifier, &entered);
  CHECK(entered);
  const auto& layers = std::as_const(*target_document).layers();
  CHECK(layers.size() == 3);
  CHECK(layers[1].name() == "Set");
  CHECK(layers[2].name() == "Shape");
  CHECK(layers[1].children().size() == 1);
  const auto& inner_copy = layers[1].children()[0];
  CHECK(std::abs(inner_copy.bounds().x - 55) <= 1);
  CHECK(std::abs(inner_copy.bounds().y - 42) <= 1);
  CHECK(inner_copy.mask().has_value());
  CHECK(inner_copy.mask()->bounds.x == inner_copy.bounds().x);
  CHECK(inner_copy.mask()->bounds.y == inner_copy.bounds().y);
  CHECK(inner_copy.layer_style().drop_shadows.size() == 1);
  CHECK(inner_copy.layer_style().drop_shadows.front().enabled);
  const auto& shape_copy = layers[2];
  CHECK(shape_copy.vector_shape() != nullptr);
  // Re-baked against the larger target canvas at the shifted position.
  CHECK(shape_copy.bounds().contains(63, 47));
  CHECK(!shape_copy.bounds().contains(20, 14));
  CHECK(std::as_const(*source_document).layers()[1].children()[0].bounds().x == 10);
}

void ui_layer_drag_smart_object_adopts_source() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  // Source: Background plus "Mark" (20x12 at 10,8), converted to a Smart Object.
  auto source = plain_document(kSourceWidth, kSourceHeight, QColor(255, 255, 255));
  patchy::Layer mark(source.allocate_layer_id(), "Mark", opaque_pixels(20, 12, QColor(200, 30, 30)));
  mark.set_bounds(patchy::Rect{10, 8, 20, 12});
  source.add_layer(std::move(mark));
  source.set_active_layer(source.layers().back().id());
  window.add_document_session(std::move(source), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* source_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* source_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, source_canvas);
  const auto source_session = patchy::ui::MainWindowTestAccess::session_id_for_canvas(window, source_canvas);
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(*source_document).layers().size() == 2);
  const auto& smart = std::as_const(*source_document).layers().back();
  CHECK(patchy::layer_is_smart_object(smart));
  CHECK(smart.name() == "Mark");
  const auto smart_id = smart.id();
  const auto source_uuid = patchy::smart_object_source_uuid(smart);
  const auto placed_uuid = patchy::smart_object_placed_uuid(smart);
  CHECK(!source_uuid.empty() && !placed_uuid.empty());
  const auto source_undo = patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, source_canvas);

  window.add_document_session(plain_document(kSourceWidth, kSourceHeight, QColor(40, 40, 40)),
                              QStringLiteral("Target"));
  QApplication::processEvents();
  auto* target_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* target_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, target_canvas);
  CHECK(std::as_const(*target_document).metadata().smart_objects.find(source_uuid) == nullptr);

  // The source is a BACKGROUND document here: the drag's session token, not
  // the active document, says where the ids live.
  bool entered = false;
  send_layer_drop_to_widget(*target_canvas, target_canvas->widget_position_for_document_point(QPoint(32, 24)),
                            {smart_id}, source_session, Qt::NoModifier, &entered);
  CHECK(entered);
  const auto& layers = std::as_const(*target_document).layers();
  CHECK(layers.size() == 2);
  const auto& copy = layers.back();
  CHECK(patchy::layer_is_smart_object(copy));
  CHECK(patchy::smart_object_source_uuid(copy) == source_uuid);
  CHECK(patchy::smart_object_placed_uuid(copy) != placed_uuid);
  CHECK(std::as_const(*target_document).metadata().smart_objects.find(source_uuid) != nullptr);
  // The 20x12 placement, centered on (32, 24).
  CHECK(std::abs(copy.bounds().x - 22) <= 1);
  CHECK(std::abs(copy.bounds().y - 18) <= 1);
  CHECK(std::as_const(*source_document).layers().size() == 2);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, source_canvas) == source_undo);
}

void ui_duplicate_layer_to_document_dialog_copies() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(plain_document(kSourceWidth, kSourceHeight, QColor(40, 40, 40)),
                              QStringLiteral("Target"));
  QApplication::processEvents();
  auto* target_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  auto* target_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, target_canvas);
  const auto target_session = patchy::ui::MainWindowTestAccess::session_id_for_canvas(window, target_canvas);
  // The source opens with "Mark" active, so the panel selection is the mark.
  auto source = plain_document(kSourceWidth, kSourceHeight, QColor(255, 255, 255));
  patchy::Layer mark(source.allocate_layer_id(), "Mark", opaque_pixels(20, 12, QColor(200, 30, 30)));
  mark.set_bounds(patchy::Rect{10, 8, 20, 12});
  source.add_layer(std::move(mark));
  source.set_active_layer(source.layers().back().id());
  window.add_document_session(std::move(source), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* source_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(source_canvas != target_canvas);

  // The dialog offers the other open document and New Document; the name
  // field renames a lone copy.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("duplicateLayerToDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* name_edit = dialog->findChild<QLineEdit*>(QStringLiteral("duplicateLayerNameEdit"));
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("duplicateLayerTargetCombo"));
    CHECK(name_edit != nullptr && combo != nullptr);
    CHECK(name_edit->text() == QStringLiteral("Mark"));
    CHECK(combo->count() == 2);
    CHECK(combo->itemText(0) == QStringLiteral("Target"));
    CHECK(combo->itemData(0).toLongLong() == target_session);
    CHECK(combo->itemData(1).toLongLong() == 0);
    name_edit->setText(QStringLiteral("Renamed"));
    combo->setCurrentIndex(0);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "layerDuplicateToDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(patchy::ui::MainWindowTestAccess::canvas(window) == target_canvas);
  CHECK(std::as_const(*target_document).layers().size() == 2);
  CHECK(std::as_const(*target_document).layers().back().name() == "Renamed");
  CHECK(std::as_const(*target_document).layers().back().bounds().x == 10);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target_canvas) == 1);

  // New Document: a fresh session the source's size holding only the copy.
  patchy::ui::MainWindowTestAccess::activate_canvas(window, source_canvas);
  QApplication::processEvents();
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("duplicateLayerToDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("duplicateLayerTargetCombo"));
    CHECK(combo != nullptr);
    combo->setCurrentIndex(combo->count() - 1);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "layerDuplicateToDocumentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
  auto* created_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(created_canvas != source_canvas && created_canvas != target_canvas);
  auto* created_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, created_canvas);
  CHECK(created_document != nullptr);
  CHECK(created_document->width() == kSourceWidth && created_document->height() == kSourceHeight);
  CHECK(std::as_const(*created_document).layers().size() == 1);
  CHECK(std::as_const(*created_document).layers().front().name() == "Mark");
  CHECK(std::as_const(*created_document).layers().front().bounds().x == 10);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_panel_organization_tests_cross_document_part() {
  return {
      {"ui_layer_drag_to_other_document_tab_copies_above_active",
       ui_layer_drag_to_other_document_tab_copies_above_active},
      {"ui_layer_drag_same_document_canvas_is_ignored", ui_layer_drag_same_document_canvas_is_ignored},
      {"ui_layer_drag_unknown_source_session_is_ignored", ui_layer_drag_unknown_source_session_is_ignored},
      {"ui_layer_drag_to_other_document_respects_edit_lock", ui_layer_drag_to_other_document_respects_edit_lock},
      {"ui_layer_drag_group_mask_style_and_shape_survive", ui_layer_drag_group_mask_style_and_shape_survive},
      {"ui_duplicate_layer_to_document_dialog_copies", ui_duplicate_layer_to_document_dialog_copies},
      {"ui_layer_drag_smart_object_adopts_source", ui_layer_drag_smart_object_adopts_source},
  };
}
