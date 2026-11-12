// The Layers panel filter menu (GitHub issue 671, docs/layer-panel-filter.md):
// criteria by layer type, properties, row color and selection that combine with each other
// and with the name text, the rows they leave out, and the button's active state.
#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "local_psd_fixtures.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;

struct FilterDocumentIds {
  patchy::LayerId background{0};
  patchy::LayerId paint{0};
  patchy::LayerId title{0};
  patchy::LayerId box{0};
  patchy::LayerId tone{0};
  patchy::LayerId group{0};
  patchy::LayerId masked{0};
  patchy::LayerId smart{0};
};

patchy::Layer pixel_layer(patchy::Document& document, const char* name, patchy::Rect bounds, QColor color) {
  patchy::Layer layer(document.allocate_layer_id(), name,
                      solid_pixels(bounds.width, bounds.height, patchy::PixelFormat::rgba8(), color));
  layer.set_bounds(bounds);
  return layer;
}

// Bottom to top: Background; Paint (pixel, red label); Title (text, blue label); Box (shape with
// a drop shadow); Tone (adjustment, hidden); folder Group holding Masked (pixel with a mask,
// locked, red label); Smart (pixel, made a Smart Object by the tests that need one).
patchy::Document make_filter_document(FilterDocumentIds& ids) {
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  ids.background = document
                       .add_pixel_layer("Background",
                                        solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(Qt::white)))
                       .id();
  auto paint = pixel_layer(document, "Paint", patchy::Rect{8, 8, 24, 24}, QColor(220, 40, 40));
  paint.set_color_label(patchy::kLayerColorRed);
  ids.paint = paint.id();
  document.add_layer(std::move(paint));

  patchy::Layer title(document.allocate_layer_id(), "Title", patchy::LayerKind::Text);
  title.metadata()[patchy::kLayerMetadataText] = "Title";
  title.metadata()[patchy::kLayerMetadataTextSize] = "18";
  title.set_color_label(patchy::kLayerColorBlue);
  ids.title = title.id();
  document.add_layer(std::move(title));

  patchy::Layer box(document.allocate_layer_id(), "Box", patchy::PixelBuffer());
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = 100;
  params.top = 70;
  params.right = 140;
  params.bottom = 110;
  patchy::populate_live_shape_box_corners(params);
  patchy::VectorShapeContent content;
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination.push_back(params);
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{40, 110, 230};
  box.set_vector_shape(std::move(content));
  box.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(box, patchy::Rect::from_size(kWidth, kHeight), nullptr);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 3.0F;
  shadow.size = 2.0F;
  box.layer_style().drop_shadows.push_back(shadow);
  ids.box = box.id();
  document.add_layer(std::move(box));

  patchy::Layer tone(document.allocate_layer_id(), "Tone", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::HueSaturation;
  settings.hue_saturation = patchy::HueSaturationAdjustment{18, 22, 0};
  patchy::configure_adjustment_layer(tone, settings);
  tone.set_visible(false);
  ids.tone = tone.id();
  document.add_layer(std::move(tone));

  patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
  ids.group = group.id();
  auto masked = pixel_layer(document, "Masked", patchy::Rect{50, 8, 20, 20}, QColor(40, 180, 80));
  patchy::PixelBuffer mask_pixels(20, 20, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  masked.set_mask(patchy::LayerMask{patchy::Rect{50, 8, 20, 20}, std::move(mask_pixels), 0, true});
  masked.set_lock_flags(patchy::kLayerLockAll);
  masked.set_color_label(patchy::kLayerColorRed);
  ids.masked = masked.id();
  group.add_child(std::move(masked));
  document.add_layer(std::move(group));

  auto smart = pixel_layer(document, "Smart", patchy::Rect{8, 80, 24, 24}, QColor(230, 160, 40));
  ids.smart = smart.id();
  document.add_layer(std::move(smart));
  document.set_active_layer(ids.paint);
  return document;
}

std::set<QString> listed_names(QListWidget& list) {
  std::set<QString> names;
  for (int row = 0; row < list.count(); ++row) {
    names.insert(list.item(row)->text());
  }
  return names;
}

void check_listed(QListWidget& list, std::set<QString> expected) {
  const auto names = listed_names(list);
  if (names != expected) {
    std::cout << "[INFO] listed:";
    for (const auto& name : names) {
      std::cout << " '" << name.toStdString() << "'";
    }
    std::cout << " expected:";
    for (const auto& name : expected) {
      std::cout << " '" << name.toStdString() << "'";
    }
    std::cout << "\n";
  }
  CHECK(names == expected);
}

// Ticks or unticks a filter entry the way a click does (the action's own toggle).
void set_filter(patchy::ui::MainWindow& window, const char* action_name, bool on) {
  auto* action = require_action(window, action_name);
  if (action != nullptr && action->isChecked() != on) {
    action->trigger();
  }
  QApplication::processEvents();
}

QToolButton* filter_button(patchy::ui::MainWindow& window) {
  auto* button = window.findChild<QToolButton*>(QStringLiteral("layerFilterButton"));
  CHECK(button != nullptr);
  return button;
}

QToolButton* filter_clear_button(patchy::ui::MainWindow& window) {
  auto* button = window.findChild<QToolButton*>(QStringLiteral("layerFilterClearButton"));
  CHECK(button != nullptr);
  return button;
}

struct FilterFixture {
  patchy::ui::MainWindow window;
  FilterDocumentIds ids;
  QListWidget* list{nullptr};
  QLineEdit* name_edit{nullptr};

  FilterFixture() {
    show_window(window);
    window.add_document_session(make_filter_document(ids), QStringLiteral("Layer Filter"));
    QApplication::processEvents();
    list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    name_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
    CHECK(list != nullptr);
    CHECK(name_edit != nullptr);
  }
};

void make_smart_object(FilterFixture& fixture) {
  patchy::ui::MainWindowTestAccess::select_layers(fixture.window, {fixture.ids.smart}, fixture.ids.smart);
  QApplication::processEvents();
  require_action(fixture.window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(fixture.window));
  CHECK(patchy::layer_is_smart_object(doc.layers().back()));
  fixture.ids.smart = doc.layers().back().id();
}

void ui_layer_filter_menu_layer_types_match_any_ticked() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;
  make_smart_object(fixture);
  CHECK(list.count() == 8);

  set_filter(window, "layerFilterTextAction", true);
  check_listed(list, {QStringLiteral("Title")});
  // Within Layer Type any ticked entry matches.
  set_filter(window, "layerFilterShapeAction", true);
  check_listed(list, {QStringLiteral("Title"), QStringLiteral("Box")});
  set_filter(window, "layerFilterTextAction", false);
  set_filter(window, "layerFilterShapeAction", false);
  set_filter(window, "layerFilterPixelAction", true);
  // A folder whose child matches stays listed as the child's parent.
  check_listed(list, {QStringLiteral("Background"), QStringLiteral("Paint"), QStringLiteral("Group"),
                      QStringLiteral("Masked")});
  set_filter(window, "layerFilterPixelAction", false);
  set_filter(window, "layerFilterAdjustmentAction", true);
  check_listed(list, {QStringLiteral("Tone")});
  set_filter(window, "layerFilterAdjustmentAction", false);
  set_filter(window, "layerFilterSmartObjectAction", true);
  check_listed(list, {QStringLiteral("Smart")});
  set_filter(window, "layerFilterSmartObjectAction", false);
  // A matching folder is listed alone: its children do not ride along.
  set_filter(window, "layerFilterFolderAction", true);
  check_listed(list, {QStringLiteral("Group")});
  set_filter(window, "layerFilterFolderAction", false);
  CHECK(list.count() == 8);
  CHECK(!filter_button(window)->property("layerFilterActive").toBool());
}

// Text layers the Type tool makes and the ones a PSD brings are pixel-kind layers carrying
// text metadata (issue 671 follow-up): Text lists them and Pixel leaves them out.
void ui_layer_filter_menu_finds_type_tool_and_psd_text_layers() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  ColorSchemeRestorer restore(patchy::ui::ColorSchemePreference::Dark);
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 60));
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->textCursor().insertText(QStringLiteral("Caption"));  // replaces the selected placeholder
  QApplication::processEvents();
  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  process_events_for(150);

  QString typed_name;
  {
    const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto typed_id = doc.active_layer_id();
    const auto* typed = typed_id.has_value() ? doc.find_layer(*typed_id) : nullptr;
    CHECK(typed != nullptr && patchy::layer_is_text(*typed));
    if (typed == nullptr) {
      return;
    }
    CHECK(typed->id() != fixture.ids.title);
    typed_name = QString::fromStdString(typed->name());
    std::cout << "[INFO] Type tool layer '" << typed->name() << "' kind " << static_cast<int>(typed->kind())
              << "\n";
  }
  CHECK(list.count() == 9);

  set_filter(window, "layerFilterTextAction", true);
  check_listed(list, {QStringLiteral("Title"), typed_name});
  if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel")); panel != nullptr) {
    save_widget_artifact("issue_671_text_filter_finds_type_layer", *panel);
  }
  set_filter(window, "layerFilterTextAction", false);
  set_filter(window, "layerFilterPixelAction", true);
  check_listed(list, {QStringLiteral("Background"), QStringLiteral("Paint"), QStringLiteral("Group"),
                      QStringLiteral("Masked"), QStringLiteral("Smart")});
  set_filter(window, "layerFilterPixelAction", false);

  // A text layer Photoshop saved.
  auto imported = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-box-text-overflow.psd"));
  std::set<QString> text_names;
  std::set<QString> other_names;
  std::function<void(const std::vector<patchy::Layer>&)> collect = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      (patchy::layer_is_text(layer) ? text_names : other_names).insert(QString::fromStdString(layer.name()));
      collect(layer.children());
    }
  };
  collect(std::as_const(imported).layers());
  CHECK(text_names.contains(QStringLiteral("Overflow Box")));
  window.add_document_session(std::move(imported), QStringLiteral("PSD Text"));
  QApplication::processEvents();
  CHECK(list.count() == static_cast<int>(text_names.size() + other_names.size()));
  set_filter(window, "layerFilterTextAction", true);
  check_listed(list, text_names);
  set_filter(window, "layerFilterTextAction", false);
  set_filter(window, "layerFilterPixelAction", true);
  for (const auto& name : listed_names(list)) {
    CHECK(!text_names.contains(name));
  }
  CHECK(!listed_names(list).contains(QStringLiteral("Overflow Box")));
  set_filter(window, "layerFilterPixelAction", false);
}

// Every layer has one Layer Type, the one its row shows: a fill layer is Fill or Adjustment
// and not a shape, a Smart Object is not a pixel layer.
void ui_layer_filter_menu_each_layer_has_one_type() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;
  make_smart_object(fixture);
  {
    auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    patchy::Layer wash(doc.allocate_layer_id(), "Wash", patchy::PixelBuffer());
    patchy::VectorShapeContent content;
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{230, 200, 60};
    wash.set_vector_shape(std::move(content));
    wash.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::update_vector_shape_raster(wash, patchy::Rect::from_size(kWidth, kHeight), nullptr);
    CHECK(patchy::layer_is_fill_layer(wash));
    doc.add_layer(std::move(wash));
    patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
    QApplication::processEvents();
  }
  CHECK(list.count() == 9);
  // The entry reads in the Layer menus' word order; its object name stays.
  CHECK(require_action(window, "layerFilterAdjustmentAction")->text() == QStringLiteral("Fill or Adjustment"));

  const std::vector<std::pair<const char*, std::set<QString>>> types{
      {"layerFilterPixelAction",
       {QStringLiteral("Background"), QStringLiteral("Paint"), QStringLiteral("Group"), QStringLiteral("Masked")}},
      {"layerFilterTextAction", {QStringLiteral("Title")}},
      {"layerFilterShapeAction", {QStringLiteral("Box")}},
      {"layerFilterAdjustmentAction", {QStringLiteral("Tone"), QStringLiteral("Wash")}},
      {"layerFilterSmartObjectAction", {QStringLiteral("Smart")}},
      {"layerFilterFolderAction", {QStringLiteral("Group")}},
  };
  int listed_once = 0;
  for (const auto& [action, expected] : types) {
    set_filter(window, action, true);
    check_listed(list, expected);
    listed_once += list.count();
    set_filter(window, action, false);
  }
  // Nine layers, each under one type; the folder is listed a second time as the parent of Masked.
  CHECK(listed_once == 10);
}

// Properties > Linked lists the layers that have a link partner; Has Smart Filters the layers
// carrying a Smart Filter stack.
void ui_layer_filter_menu_linked_and_smart_filters() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;
  make_smart_object(fixture);
  const auto& ids = fixture.ids;

  set_filter(window, "layerFilterLinkedAction", true);
  CHECK(list.count() == 0);
  set_filter(window, "layerFilterLinkedAction", false);
  patchy::ui::MainWindowTestAccess::select_layers(window, {ids.paint, ids.box}, ids.paint);
  QApplication::processEvents();
  require_action(window, "layerLinkAction")->trigger();
  QApplication::processEvents();
  {
    const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(patchy::effective_link_group(doc.layers(), ids.paint) != 0U);
  }
  set_filter(window, "layerFilterLinkedAction", true);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Box")});
  // Within Properties any ticked entry matches; other groups narrow it.
  set_filter(window, "layerFilterHiddenAction", true);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Box"), QStringLiteral("Tone")});
  set_filter(window, "layerFilterHiddenAction", false);
  set_filter(window, "layerFilterShapeAction", true);
  check_listed(list, {QStringLiteral("Box")});
  set_filter(window, "layerFilterShapeAction", false);
  set_filter(window, "layerFilterLinkedAction", false);

  // A link group left with one member is not linked (its row has no chain badge).
  {
    auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    doc.find_layer(ids.box)->set_link_group(0U);
    CHECK(std::as_const(doc).find_layer(ids.paint)->link_group() != 0U);
    patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  }
  set_filter(window, "layerFilterLinkedAction", true);
  CHECK(list.count() == 0);
  set_filter(window, "layerFilterLinkedAction", false);

  set_filter(window, "layerFilterSmartFiltersAction", true);
  CHECK(list.count() == 0);
  {
    patchy::SmartFilterStack stack;
    stack.support = patchy::SmartFilterStackSupport::Supported;
    stack.mask.linked = false;
    patchy::SmartFilterEntry entry;
    entry.kind = patchy::SmartFilterKind::GaussianBlur;
    entry.native_name = "Gaussian Blur...";
    entry.native_class_id = "GsnB";
    entry.native_filter_id = 0x47736e42U;
    entry.parameters = patchy::GaussianBlurSmartFilter{2.0};
    stack.entries.push_back(std::move(entry));
    auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    doc.find_layer(ids.smart)->set_smart_filter_stack(std::move(stack));
    patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
    QApplication::processEvents();
  }
  check_listed(list, {QStringLiteral("Smart")});
  set_filter(window, "layerFilterSmartObjectAction", true);
  check_listed(list, {QStringLiteral("Smart")});
  set_filter(window, "layerFilterSmartObjectAction", false);
  set_filter(window, "layerFilterPixelAction", true);
  CHECK(list.count() == 0);
  require_action(window, "layerFilterClearAction")->trigger();
  QApplication::processEvents();
  CHECK(list.count() == 8);
  CHECK(!require_action(window, "layerFilterSmartFiltersAction")->isChecked());
}

void ui_layer_filter_menu_groups_and_name_text_combine() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;

  set_filter(window, "layerFilterMaskAction", true);
  check_listed(list, {QStringLiteral("Group"), QStringLiteral("Masked")});
  set_filter(window, "layerFilterMaskAction", false);
  set_filter(window, "layerFilterStyleAction", true);
  check_listed(list, {QStringLiteral("Box")});
  set_filter(window, "layerFilterHiddenAction", true);
  check_listed(list, {QStringLiteral("Box"), QStringLiteral("Tone")});
  set_filter(window, "layerFilterStyleAction", false);
  set_filter(window, "layerFilterHiddenAction", false);
  set_filter(window, "layerFilterLockedAction", true);
  {
    const auto names = listed_names(list);
    CHECK(names.contains(QStringLiteral("Masked")));
    CHECK(names.contains(QStringLiteral("Group")));
    CHECK(!names.contains(QStringLiteral("Paint")));
    CHECK(!names.contains(QStringLiteral("Box")));
  }
  set_filter(window, "layerFilterLockedAction", false);

  // Row color: any ticked color.
  set_filter(window, "layerFilterColorRedAction", true);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Group"), QStringLiteral("Masked")});
  set_filter(window, "layerFilterColorBlueAction", true);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Title"), QStringLiteral("Group"),
                      QStringLiteral("Masked")});
  auto* color_menu = window.findChild<QMenu*>(QStringLiteral("layerFilterColorMenu"));
  CHECK(color_menu != nullptr);
  CHECK(color_menu != nullptr && color_menu->menuAction()->text() == QStringLiteral("Row Color (2)"));
  set_filter(window, "layerFilterColorBlueAction", false);

  // Across groups every ticked group must match: red pixel layers with a mask.
  set_filter(window, "layerFilterPixelAction", true);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Group"), QStringLiteral("Masked")});
  set_filter(window, "layerFilterMaskAction", true);
  check_listed(list, {QStringLiteral("Group"), QStringLiteral("Masked")});
  set_filter(window, "layerFilterMaskAction", false);

  // And the name text with them.
  fixture.name_edit->setText(QStringLiteral("pai"));
  QApplication::processEvents();
  check_listed(list, {QStringLiteral("Paint")});
  fixture.name_edit->setText(QStringLiteral("title"));
  QApplication::processEvents();
  CHECK(list.count() == 0);

  // Clear Filter turns off the criteria and the name text.
  require_action(window, "layerFilterClearAction")->trigger();
  QApplication::processEvents();
  CHECK(fixture.name_edit->text().isEmpty());
  CHECK(list.count() == 8);
  CHECK(!require_action(window, "layerFilterPixelAction")->isChecked());
  CHECK(!require_action(window, "layerFilterColorRedAction")->isChecked());
}

void ui_layer_filter_menu_blocks_drag_reorder() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;

  set_filter(window, "layerFilterColorRedAction", true);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Group"), QStringLiteral("Masked")});
  auto* group_item = require_layer_item(list, QStringLiteral("Group"));
  const auto group_rect = list.visualItemRect(group_item);
  send_layer_drop(list, QPoint(2, group_rect.top() + 2), {fixture.ids.paint});
  process_events_for(1);
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(doc.layers()[1].id() == fixture.ids.paint);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Group"), QStringLiteral("Masked")});

  // A drag attempt says what to clear.
  auto* paint_item = require_layer_item(list, QStringLiteral("Paint"));
  auto* paint_name = list.itemWidget(paint_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(paint_name != nullptr);
  if (paint_name != nullptr) {
    const auto press_point = paint_name->rect().center();
    send_mouse(*paint_name, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*paint_name, QEvent::MouseMove, press_point + QPoint(0, 60), Qt::NoButton, Qt::LeftButton);
    CHECK(window.statusBar()->currentMessage() == QStringLiteral("Clear the layer filter to reorder layers"));
    send_mouse(*paint_name, QEvent::MouseButtonRelease, press_point + QPoint(0, 60), Qt::LeftButton,
               Qt::NoButton);
    QApplication::processEvents();
  }

  // The clear button turns the filter off and reordering works again.
  auto* clear = filter_clear_button(window);
  CHECK(clear != nullptr && clear->isVisible());
  if (clear != nullptr) {
    clear->click();
  }
  QApplication::processEvents();
  CHECK(list.count() == 8);
  CHECK(clear == nullptr || !clear->isVisible());
  const auto top_rect = list.visualItemRect(list.item(0));
  send_layer_drop(list, QPoint(2, top_rect.top() + 2), {fixture.ids.paint});
  process_events_for(1);
  CHECK(doc.layers().back().id() == fixture.ids.paint);
}

void ui_layer_filter_menu_canvas_reveal_clears_criteria() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;

  set_filter(window, "layerFilterTextAction", true);
  check_listed(list, {QStringLiteral("Title")});
  CHECK(filter_button(window)->property("layerFilterActive").toBool());

  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(18, 18));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();

  // Paint is not a text layer: the whole filter goes and its row is selected.
  CHECK(list.count() == 8);
  auto* paint_item = require_layer_item(list, QStringLiteral("Paint"));
  CHECK(paint_item->isSelected());
  CHECK(!require_action(window, "layerFilterTextAction")->isChecked());
  CHECK(!filter_button(window)->property("layerFilterActive").toBool());
  CHECK(!filter_clear_button(window)->isVisible());
}

void ui_layer_filter_menu_selected_only_follows_selection() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;
  const auto& ids = fixture.ids;

  patchy::ui::MainWindowTestAccess::select_layers(window, {ids.paint, ids.title}, ids.paint);
  QApplication::processEvents();
  set_filter(window, "layerFilterSelectedOnlyAction", true);
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Title")});
  // The rebuild keeps the multi-selection.
  CHECK(list.selectedItems().size() == 2);

  // A click in the panel narrows the selection but keeps the rows to pick among.
  list.setCurrentItem(require_layer_item(list, QStringLiteral("Title")), QItemSelectionModel::ClearAndSelect);
  QApplication::processEvents();
  check_listed(list, {QStringLiteral("Paint"), QStringLiteral("Title")});
  CHECK(list.selectedItems().size() == 1);

  // A selection made outside the panel (a command, the canvas) is followed.
  patchy::ui::MainWindowTestAccess::select_layers(window, {ids.box, ids.tone}, ids.box);
  QApplication::processEvents();
  check_listed(list, {QStringLiteral("Box"), QStringLiteral("Tone")});
  CHECK(list.selectedItems().size() == 2);

  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(18, 18));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();
  check_listed(list, {QStringLiteral("Paint")});
  CHECK(require_action(window, "layerFilterSelectedOnlyAction")->isChecked());
  CHECK(require_layer_item(list, QStringLiteral("Paint"))->isSelected());

  set_filter(window, "layerFilterSelectedOnlyAction", false);
  CHECK(list.count() == 8);
}

void ui_layer_filter_menu_is_per_document() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto& list = *fixture.list;
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  auto* filter_canvas = tabs->currentWidget();

  set_filter(window, "layerFilterHiddenAction", true);
  check_listed(list, {QStringLiteral("Tone")});

  patchy::Document other(48, 48, patchy::PixelFormat::rgb8());
  other.add_pixel_layer("Boat", solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(40, 90, 220)));
  window.add_document_session(std::move(other), QStringLiteral("Other"));
  QApplication::processEvents();
  check_listed(list, {QStringLiteral("Boat")});
  CHECK(!filter_button(window)->property("layerFilterActive").toBool());
  CHECK(!require_action(window, "layerFilterHiddenAction")->isChecked());

  tabs->setCurrentWidget(filter_canvas);
  QApplication::processEvents();
  check_listed(list, {QStringLiteral("Tone")});
  CHECK(filter_button(window)->property("layerFilterActive").toBool());
  CHECK(require_action(window, "layerFilterHiddenAction")->isChecked());
}

// The button lights while a criterion is ticked, in Dark and Light; saves the open menu and
// a filtered list for the issue's pictures.
void ui_layer_filter_button_active_state_in_both_schemes() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    const std::string scheme_name = scheme == patchy::ui::ColorSchemePreference::Dark ? "dark" : "light";
    FilterFixture fixture;
    auto& window = fixture.window;
    auto* button = filter_button(window);
    auto* clear = filter_clear_button(window);
    if (button == nullptr || clear == nullptr) {
      return;
    }
    CHECK(button->isVisible());
    CHECK(!clear->isVisible());
    const auto rest = button->grab().toImage();

    set_filter(window, "layerFilterColorRedAction", true);
    set_filter(window, "layerFilterPixelAction", true);
    CHECK(button->property("layerFilterActive").toBool());
    CHECK(clear->isVisible());
    const auto active = button->grab().toImage();
    // The inset background behind the glyph changes color.
    CHECK(rest.size() == active.size());
    if (rest.size() == active.size() && !rest.isNull()) {
      // Inside the border, left of the glyph.
      const QPoint inside(2, rest.height() / 2);
      CHECK(rest.pixelColor(inside) != active.pixelColor(inside));
    }
    // The filter button and then the clear button follow the name field on its line.
    const auto field_rect = QRect(fixture.name_edit->mapTo(&window, QPoint()), fixture.name_edit->size());
    const auto button_rect = QRect(button->mapTo(&window, QPoint()), button->size());
    const auto clear_rect = QRect(clear->mapTo(&window, QPoint()), clear->size());
    CHECK(button_rect.left() > field_rect.right());
    CHECK(clear_rect.left() > button_rect.right());
    CHECK(std::abs(button_rect.center().y() - field_rect.center().y()) <= 1);
    CHECK(std::abs(clear_rect.center().y() - field_rect.center().y()) <= 1);
    CHECK(fixture.name_edit->width() >= 90);

    auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
    CHECK(panel != nullptr);
    if (panel != nullptr) {
      save_widget_artifact("issue_671_filtered_list_" + scheme_name, *panel);
    }
    auto* menu = window.findChild<QMenu*>(QStringLiteral("layerFilterMenu"));
    CHECK(menu != nullptr);
    if (menu != nullptr) {
      button->click();
      QApplication::processEvents();
      CHECK(menu->isVisible());
      save_widget_artifact("issue_671_filter_menu_" + scheme_name, *menu);
      // A ticked entry keeps the menu open for the next tick.
      auto* text_action = require_action(window, "layerFilterTextAction");
      const auto text_rect = menu->actionGeometry(text_action);
      send_mouse(*menu, QEvent::MouseButtonRelease, text_rect.center(), Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      CHECK(text_action->isChecked());
      CHECK(menu->isVisible());
      menu->close();
      QApplication::processEvents();
    }

    clear->click();
    QApplication::processEvents();
    CHECK(!button->property("layerFilterActive").toBool());
    CHECK(!clear->isVisible());
    CHECK(fixture.list->count() == 8);
  }
}

// The filter button and its clear button follow the name field (issue 671 follow-up): one
// line after the Lock buttons at the opening width, and wrapped together onto the next line
// in the compact column, the x included, with the field keeping its typing room.
void ui_layer_filter_button_follows_name_field_in_narrow_column() {
  FilterFixture fixture;
  auto& window = fixture.window;
  auto* button = filter_button(window);
  auto* clear = filter_clear_button(window);
  auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
  auto* lock_all = window.findChild<QWidget*>(QStringLiteral("layerLockAllButton"));
  if (button == nullptr || clear == nullptr || panel == nullptr || lock_all == nullptr) {
    CHECK(false);
    return;
  }
  const auto rect_in_panel = [panel](QWidget* widget) { return QRect(widget->mapTo(panel, QPoint()), widget->size()); };
  const auto settle = [] {
    for (int pass = 0; pass < 3; ++pass) {
      QApplication::processEvents();
    }
  };
  const auto check_order = [&](const char* where) {
    const auto field = rect_in_panel(fixture.name_edit);
    const auto menu_button = rect_in_panel(button);
    std::cout << "[INFO] " << where << ": field " << field.x() << "," << field.y() << " " << field.width()
              << "x" << field.height() << ", button at " << menu_button.x() << "\n";
    CHECK(menu_button.left() > field.right());
    CHECK(std::abs(menu_button.center().y() - field.center().y()) <= 1);
    CHECK(panel->rect().contains(menu_button));
    CHECK(fixture.name_edit->width() >= 90);
    if (clear->isVisible()) {
      const auto x = rect_in_panel(clear);
      CHECK(x.left() > menu_button.right());
      CHECK(std::abs(x.center().y() - field.center().y()) <= 1);
      CHECK(panel->rect().contains(x));
    }
  };

  const auto opening = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
  const auto compact = patchy::ui::MainWindowTestAccess::right_dock_compact_width(window);
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, opening);
  settle();
  check_order("opening");
  CHECK(std::abs(rect_in_panel(fixture.name_edit).center().y() - rect_in_panel(lock_all).center().y()) <= 1);
  // Showing the x takes its width from the field alone: the field keeps its left edge.
  const auto field_left = rect_in_panel(fixture.name_edit).left();
  const auto field_width = fixture.name_edit->width();
  set_filter(window, "layerFilterHiddenAction", true);
  settle();
  CHECK(clear->isVisible());
  check_order("opening, filter on");
  CHECK(rect_in_panel(fixture.name_edit).left() == field_left);
  CHECK(fixture.name_edit->width() < field_width);

  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, compact);
  settle();
  check_order("compact, filter on");
  // Wrapped below the Lock buttons, all three together.
  CHECK(rect_in_panel(fixture.name_edit).top() > rect_in_panel(lock_all).bottom());
  save_widget_artifact("issue_671_filter_row_compact", *panel);
  set_filter(window, "layerFilterHiddenAction", false);
  settle();
  CHECK(!clear->isVisible());
  check_order("compact, filter off");
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, opening);
  settle();
}

}  // namespace

std::vector<patchy::test::TestCase> layer_filter_menu_tests() {
  return {
      {"ui_layer_filter_menu_layer_types_match_any_ticked", ui_layer_filter_menu_layer_types_match_any_ticked},
      {"ui_layer_filter_menu_finds_type_tool_and_psd_text_layers",
       ui_layer_filter_menu_finds_type_tool_and_psd_text_layers},
      {"ui_layer_filter_menu_each_layer_has_one_type", ui_layer_filter_menu_each_layer_has_one_type},
      {"ui_layer_filter_menu_linked_and_smart_filters", ui_layer_filter_menu_linked_and_smart_filters},
      {"ui_layer_filter_menu_groups_and_name_text_combine", ui_layer_filter_menu_groups_and_name_text_combine},
      {"ui_layer_filter_menu_blocks_drag_reorder", ui_layer_filter_menu_blocks_drag_reorder},
      {"ui_layer_filter_menu_canvas_reveal_clears_criteria", ui_layer_filter_menu_canvas_reveal_clears_criteria},
      {"ui_layer_filter_menu_selected_only_follows_selection", ui_layer_filter_menu_selected_only_follows_selection},
      {"ui_layer_filter_menu_is_per_document", ui_layer_filter_menu_is_per_document},
      {"ui_layer_filter_button_active_state_in_both_schemes", ui_layer_filter_button_active_state_in_both_schemes},
      {"ui_layer_filter_button_follows_name_field_in_narrow_column",
       ui_layer_filter_button_follows_name_field_in_narrow_column},
  };
}
