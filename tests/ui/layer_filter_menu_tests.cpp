// The Layers panel filter menu (GitHub issue 671, docs/layer-panel-filter.md):
// criteria by layer type, properties, row color and selection that combine with each other
// and with the name text, the rows they leave out, and the button's active state.
#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
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
#include <QToolButton>
#include <algorithm>
#include <cmath>
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
    // The clear button sits on the same line as the name field.
    CHECK(std::abs(clear->mapTo(&window, clear->rect().center()).y() -
                   fixture.name_edit->mapTo(&window, fixture.name_edit->rect().center()).y()) <= 1);

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

}  // namespace

std::vector<patchy::test::TestCase> layer_filter_menu_tests() {
  return {
      {"ui_layer_filter_menu_layer_types_match_any_ticked", ui_layer_filter_menu_layer_types_match_any_ticked},
      {"ui_layer_filter_menu_groups_and_name_text_combine", ui_layer_filter_menu_groups_and_name_text_combine},
      {"ui_layer_filter_menu_blocks_drag_reorder", ui_layer_filter_menu_blocks_drag_reorder},
      {"ui_layer_filter_menu_canvas_reveal_clears_criteria", ui_layer_filter_menu_canvas_reveal_clears_criteria},
      {"ui_layer_filter_menu_selected_only_follows_selection", ui_layer_filter_menu_selected_only_follows_selection},
      {"ui_layer_filter_menu_is_per_document", ui_layer_filter_menu_is_per_document},
      {"ui_layer_filter_button_active_state_in_both_schemes", ui_layer_filter_button_active_state_in_both_schemes},
  };
}
