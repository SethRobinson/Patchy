#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "ui/style_browser.hpp"
#include "ui/color_panel.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/main_window.hpp"
#include "psd/psd_document_io.hpp"
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
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
#include "ui_test_support.hpp"
#include "ui/appearance_properties.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "ui/style_library.hpp"
#include "ui/pattern_library.hpp"

using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

namespace {

template<class Widget>
Widget* field(QWidget& parent, const char* name) {
  auto* result = parent.findChild<Widget*>(QLatin1String(name));
  CHECK(result != nullptr);
  return result;
}

QDialog* appearance_dialog(const char* name) {
  for (auto* widget : QApplication::topLevelWidgets())
    if (widget->objectName() == QLatin1String(name)) return qobject_cast<QDialog*>(widget);
  CHECK(false);
  return nullptr;
}

Layer rectangle(LayerId id, double left, double width, std::array<double, 4> radii, RgbColor color) {
  Layer layer(id, "Rectangle " + std::to_string(id), PixelBuffer());
  LiveShapeParams params;
  params.kind = std::any_of(radii.begin(), radii.end(), [](double value) { return value > 0; })
      ? LiveShapeKind::RoundedRectangle : LiveShapeKind::Rectangle;
  params.left = left; params.top = 20; params.right = left + width; params.bottom = 100;
  params.corner_radii = radii; params.index = static_cast<int>(id);
  populate_live_shape_box_corners(params);
  VectorShapeContent content;
  content.path.subpaths = generate_live_shape_subpaths(params);
  content.origination.push_back(params);
  content.fill.kind = VectorFillKind::Solid; content.fill.color = color;
  content.stroke.enabled = true; content.stroke.width = static_cast<double>(id);
  content.stroke.content.color = color;
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  update_vector_shape_raster(layer, Rect::from_size(600, 400), nullptr);
  return layer;
}

AppearanceDialogContext<ShapeAppearanceSettings> shape_context(const std::vector<Layer>& layers) {
  AppearanceDialogContext<ShapeAppearanceSettings> result;
  result.selected_count = layers.size();
  for (const auto& layer : layers) {
    result.originals.push_back(shape_appearance_settings(layer));
    result.names.push_back(QString::fromStdString(layer.name()));
  }
  return result;
}

void ui_batch_shape_fields_radii_and_full_apply() {
  const std::vector<Layer> layers{rectangle(1, 10, 130, {2, 4, 6, 8}, {210, 20, 30}),
                                 rectangle(2, 210, 30, {0, 0, 0, 0}, {10, 180, 40})};
  auto context = shape_context(layers);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    CHECK(field<QLabel>(*dialog, "shapeAppearanceSelectionSummary")->text().contains("2"));
    auto* radius_link = field<QToolButton>(*dialog, "shapeGeometryRadiusLinkButton");
    CHECK(!radius_link->isChecked());
    CHECK(!field<QDoubleSpinBox>(*dialog, "shapeGeometryWidthSpin")->isEnabled());
    CHECK(field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->property("appearanceMixed").toBool());
    field<QDoubleSpinBox>(*dialog, "shapeGeometryRadiusTopLeftSpin")->setValue(90);
    field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->setValue(7);
    dialog->accept();
  });
  const auto edited = request_shape_appearance_settings(nullptr, {}, context.originals.front(), {},
      nullptr, nullptr, nullptr, {}, {}, {}, &context);
  CHECK(edited.has_value());
  for (std::size_t i = 0; i < layers.size(); ++i) {
    const auto next = apply_shape_appearance_edits(context.originals[i], *edited);
    CHECK(next.stroke.width == 7);
    CHECK(next.fill == context.originals[i].fill);
    CHECK(next.stroke.content == context.originals[i].stroke.content);
    CHECK(next.geometry->corner_radii[0] == 90);
    CHECK(next.geometry->corner_radii[1] == context.originals[i].geometry->corner_radii[1]);
    CHECK(next.geometry->left == context.originals[i].geometry->left);
    CHECK(next.geometry->right == context.originals[i].geometry->right);
    const auto content = assemble_shape_appearance(*layers[i].vector_shape(), next);
    CHECK(content.path.bounds()->left == context.originals[i].geometry->left);
    CHECK(content.path.bounds()->right == context.originals[i].geometry->right);
    CHECK(content.origination.front().index == context.originals[i].geometry->index);
  }
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    field<QToolButton>(*dialog, "shapeGeometryRadiusLinkButton")->setChecked(true);
    field<QDoubleSpinBox>(*dialog, "shapeGeometryRadiusBottomLeftSpin")->setValue(15);
    field<QPushButton>(*dialog, "shapeAppearanceApplyAllButton")->click();
    field<QCheckBox>(*dialog, "shapeAppearancePreviewCheck")->setChecked(false);
    dialog->accept();
  });
  const auto all = request_shape_appearance_settings(nullptr, {}, context.originals.front(), {},
      nullptr, nullptr, nullptr, {}, {}, {}, &context);
  CHECK(all && all->preview_enabled);
  const auto second = apply_shape_appearance_edits(context.originals[1], *all);
  CHECK(second.fill == context.originals.front().fill);
  CHECK(second.geometry->left == 210 && second.geometry->right == 240);
  for (auto value : second.geometry->corner_radii) CHECK(value == 15);
}

void ui_batch_shape_same_value_and_nested_picker_cancel() {
  const std::vector<Layer> layers{rectangle(1, 10, 130, {}, {210, 20, 30}),
                                 rectangle(2, 210, 50, {}, {10, 180, 40})};
  auto context = shape_context(layers);
  ShapeAppearanceSettings preview;
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    auto* width = field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin");
    auto* text = width->findChild<QLineEdit*>();
    CHECK(text);
    // An explicit commit of the displayed value applies it to the other shape.
    QMetaObject::invokeMethod(text, "textEdited", Qt::DirectConnection, Q_ARG(QString, text->text()));
    QMetaObject::invokeMethod(width, "editingFinished", Qt::DirectConnection);
    QTimer::singleShot(0, [&] {
      auto* picker_dialog = appearance_dialog("patchyColorDialog");
      auto* picker = field<PatchyColorPicker>(*picker_dialog, "patchyAdvancedColorPicker");
      picker->setCurrentColor(QColor(30, 40, 230));
      CHECK(apply_shape_appearance_edits(context.originals[1], preview).fill.color.blue == 230);
      picker_dialog->reject();
    });
    field<QPushButton>(*dialog, "shapeFillColorButton")->click();
    CHECK(apply_shape_appearance_edits(context.originals[1], preview).fill == context.originals[1].fill);
    dialog->accept();
  });
  const auto edited = request_shape_appearance_settings(nullptr, [&](const auto& value) { preview = value; },
      context.originals.front(), {}, nullptr, nullptr, nullptr, {}, {}, {}, &context);
  CHECK(edited.has_value());
  CHECK(apply_shape_appearance_edits(context.originals[1], *edited).stroke.width == 1);
  CHECK(apply_shape_appearance_edits(context.originals[1], *edited).fill == context.originals[1].fill);
}

void select_style_page(QDialog& dialog, const QString& name) {
  auto* categories = field<QListWidget>(dialog, "layerStyleCategoryList");
  for (int row = 0; row < categories->count(); ++row) {
    if (categories->item(row)->text() == name) { categories->setCurrentRow(row); return; }
  }
  CHECK(false);
}

void ui_batch_style_occurrences_missing_and_navigation() {
  Layer first(1, "First", PixelBuffer()), second(2, "Second", PixelBuffer()), missing(3, "Missing", PixelBuffer());
  LayerDropShadow shadow; shadow.enabled = true; shadow.size = 4; shadow.distance = 3;
  first.layer_style().drop_shadows = {shadow};
  shadow.color = {120, 20, 50}; shadow.size = 19; shadow.distance = 32;
  second.layer_style().drop_shadows = {shadow, shadow};
  AppearanceDialogContext<LayerStyleSettings> context;
  context.originals = {layer_style_settings(first), layer_style_settings(second), layer_style_settings(missing)};
  context.names = {"First", "Second", "Missing"}; context.selected_count = 3;
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Drop Shadow");
    CHECK(field<QSpinBox>(*dialog, "layerStyleDropShadowSizeSpin")->property("appearanceMixed").toBool());
    field<QSpinBox>(*dialog, "layerStyleDropShadowSizeSpin")->setValue(9);
    select_style_page(*dialog, "Stroke");
    select_style_page(*dialog, "Blending Options");
    dialog->accept();
  });
  const auto edited = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(edited.has_value());
  const auto next = apply_layer_style_edits(context.originals[1], *edited);
  CHECK(next.style.drop_shadows.size() == 2);
  CHECK(next.style.drop_shadows.front().size == 9);
  CHECK(next.style.drop_shadows.front().distance == 32);
  CHECK(next.style.drop_shadows.front().color == shadow.color);
  CHECK(next.style.drop_shadows.back().size == 19);
  CHECK(next.style.strokes.empty());
  CHECK(apply_layer_style_edits(context.originals[2], *edited).style.drop_shadows.empty());
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Drop Shadow");
    // Same-value explicit enablement creates the missing occurrence only.
    auto* check = field<QCheckBox>(*dialog, "layerStyleDropShadowCategoryCheck");
    check->setChecked(false); check->setChecked(true);
    dialog->accept();
  });
  const auto enabled = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(enabled.has_value());
  CHECK(apply_layer_style_edits(context.originals[1], *enabled).style.drop_shadows.front().size == 19);
  CHECK(apply_layer_style_edits(context.originals[2], *enabled).style.drop_shadows.front().enabled);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Stroke"); select_style_page(*dialog, "Bevel & Emboss");
    select_style_page(*dialog, "Drop Shadow"); dialog->accept();
  });
  const auto noop = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(noop && noop->edits->operations.empty());
}

void ui_batch_style_gradient_placement_and_picker_checkpoint() {
  Layer first(1, "First", PixelBuffer()), second(2, "Second", PixelBuffer());
  LayerGradientFill effect; effect.enabled = true;
  effect.gradient.color_stops = {{0, {255, 0, 0}}, {1, {0, 0, 255}}};
  effect.gradient.alpha_stops = {{0, 1}, {1, 1}};
  first.layer_style().gradient_fills = {effect};
  effect.gradient.angle_degrees = 23; effect.gradient.scale = 2.0F;
  effect.gradient.offset_x_percent = 32; effect.gradient.color_stops.front().color = {10, 150, 30};
  second.layer_style().gradient_fills = {effect};
  AppearanceDialogContext<LayerStyleSettings> context;
  context.originals = {layer_style_settings(first), layer_style_settings(second)};
  context.names = {"First", "Second"}; context.selected_count = 2;
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Gradient Overlay");
    field<QSpinBox>(*dialog, "layerStyleGradientOpacitySpin")->setValue(70);
    QTimer::singleShot(0, [&] {
      auto* picker_dialog = appearance_dialog("patchyColorDialog");
      field<PatchyColorPicker>(*picker_dialog, "patchyAdvancedColorPicker")->setCurrentColor(QColor(1, 2, 3));
      picker_dialog->reject();
    });
    field<QPushButton>(*dialog, "layerStyleGradientStopSwatchButton")->click();
    dialog->accept();
  });
  const auto edited = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(edited.has_value());
  const auto after = apply_layer_style_edits(context.originals[1], *edited);
  CHECK(after.style.gradient_fills.front().gradient == effect.gradient);
  CHECK(std::abs(after.style.gradient_fills.front().opacity - 0.7F) < 1e-6F);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Gradient Overlay");
    QTimer::singleShot(0, [&] {
      auto* picker = appearance_dialog("patchyColorDialog");
      field<PatchyColorPicker>(*picker, "patchyAdvancedColorPicker")->setCurrentColor(QColor(7, 8, 9));
      picker->accept();
    });
    field<QPushButton>(*dialog, "layerStyleGradientStopSwatchButton")->click();
    dialog->accept();
  });
  const auto ramp = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(ramp.has_value());
  const auto gradient = apply_layer_style_edits(context.originals[1], *ramp).style.gradient_fills.front().gradient;
  CHECK(gradient.color_stops.front().color == (RgbColor{7, 8, 9}));
  CHECK(gradient.angle_degrees == 23 && gradient.scale == 2.0F && gradient.offset_x_percent == 32);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Gradient Overlay");
    QMetaObject::invokeMethod(field<QLineEdit>(*dialog, "layerStyleGradientStopHexEdit"),
        "editingFinished", Qt::DirectConnection);
    dialog->accept();
  });
  const auto focused = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(focused && focused->edits->operations.empty());
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Gradient Overlay");
    auto* hex = field<QLineEdit>(*dialog, "layerStyleGradientStopHexEdit");
    QMetaObject::invokeMethod(hex, "textEdited", Qt::DirectConnection, Q_ARG(QString, hex->text()));
    QMetaObject::invokeMethod(hex, "editingFinished", Qt::DirectConnection);
    dialog->accept();
  });
  const auto same = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(same.has_value());
  const auto same_gradient = apply_layer_style_edits(context.originals[1], *same).style.gradient_fills.front().gradient;
  CHECK(same_gradient.color_stops == std::as_const(first).layer_style().gradient_fills.front().gradient.color_stops);
  CHECK(same_gradient.angle_degrees == 23 && same_gradient.scale == 2.0F && same_gradient.offset_x_percent == 32);
}

void ui_batch_style_stack_order_full_apply_and_no_style() {
  MainWindow window; show_window(window);
  Layer first(1, "First", PixelBuffer()), second(2, "Second", PixelBuffer()), missing(3, "Missing", PixelBuffer());
  LayerDropShadow shadow; shadow.enabled = true; shadow.size = 4;
  first.layer_style().drop_shadows = {shadow};
  shadow.size = 8; first.layer_style().drop_shadows.push_back(shadow);
  shadow.size = 22; shadow.color = {90, 40, 10};
  second.layer_style().drop_shadows = {shadow};
  second.layer_style().blend_clipped_elements = false;
  second.set_opacity(0.6F);
  AppearanceDialogContext<LayerStyleSettings> context;
  context.originals = {layer_style_settings(first), layer_style_settings(second), layer_style_settings(missing)};
  context.names = {"First", "Second", "Missing"}; context.selected_count = 3;
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Drop Shadow");
    field<QPushButton>(*dialog, "layerStyleAddDropShadowInstanceButton")->click();
    QApplication::processEvents();
    field<QSpinBox>(*dialog, "layerStyleDropShadowSizeSpin")->setValue(13);
    select_style_page(*dialog, "Drop Shadow");
    field<QPushButton>(*dialog, "layerStyleRemoveSelectedInstanceButton")->click();
    QApplication::processEvents();
    save_widget_artifact("ui_batch_style_dialog", *dialog);
    dialog->accept();
  });
  const auto edited = request_layer_style_settings(&window, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(edited.has_value());
  const auto one = apply_layer_style_edits(context.originals[0], *edited);
  const auto two = apply_layer_style_edits(context.originals[1], *edited);
  const auto three = apply_layer_style_edits(context.originals[2], *edited);
  CHECK(one.style.drop_shadows.size() == 2 && one.style.drop_shadows[0].size == 13 && one.style.drop_shadows[1].size == 8);
  CHECK(two.style.drop_shadows.size() == 1 && two.style.drop_shadows[0].size == 13);
  CHECK(two.style.drop_shadows[0].color == shadow.color);
  CHECK(three.style.drop_shadows.size() == 1 && three.style.drop_shadows[0].size == 13);
  CHECK(two.opacity == 60 && !two.style.blend_clipped_elements);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    field<QPushButton>(*dialog, "layerStyleApplyAllButton")->click();
    dialog->accept();
  });
  const auto all = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(all.has_value());
  CHECK(layer_style_settings_equal(apply_layer_style_edits(context.originals[1], *all), context.originals[0]));
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    CHECK(QMetaObject::invokeMethod(field<QWidget>(*dialog, "layerStyleStylesBrowser"), "no_style_clicked", Qt::DirectConnection));
    dialog->accept();
  });
  const auto none = request_layer_style_settings(nullptr, first, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, &context);
  CHECK(none.has_value());
  const auto cleared = apply_layer_style_edits(context.originals[1], *none);
  CHECK(cleared.style.drop_shadows.empty());
  CHECK(cleared.opacity == 60 && !cleared.style.blend_clipped_elements);
}

void ui_batch_shape_mixed_geometry_compound_and_reset() {
  MainWindow window; show_window(window);
  auto ellipse = rectangle(1, 10, 110, {}, {20, 70, 200});
  auto ellipse_content = *std::as_const(ellipse).vector_shape();
  ellipse_content.origination.front().kind = LiveShapeKind::Ellipse;
  ellipse_content.path.subpaths = generate_live_shape_subpaths(ellipse_content.origination.front());
  ellipse.set_vector_shape(ellipse_content);
  auto compound = rectangle(3, 230, 60, {}, {20, 40, 60});
  auto compound_content = *std::as_const(compound).vector_shape();
  VectorShapePart a; a.groups = {3}; a.fill = compound_content.fill; a.stroke = compound_content.stroke;
  auto b = a; b.fill.color = {160, 140, 110}; b.stroke.content.color = {110, 10, 10}; b.stroke.enabled = false;
  compound_content.parts = {a, b}; compound.set_vector_shape(compound_content);
  auto unsupported = rectangle(4, 330, 40, {}, {50, 70, 20});
  auto unsupported_content = *std::as_const(unsupported).vector_shape();
  unsupported_content.origination.front().raw_descriptor = {1, 2, 3};
  unsupported.set_vector_shape(unsupported_content);
  std::vector<Layer> layers{ellipse, rectangle(2, 150, 30, {2, 4, 6, 8}, {10, 180, 40}), compound, unsupported};
  auto context = shape_context(layers);
  ShapeAppearanceSettings preview;
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    auto* radius = field<QDoubleSpinBox>(*dialog, "shapeGeometryRadiusTopLeftSpin");
    CHECK(radius->value() == 2);
    CHECK(!radius->property("appearanceMixed").toBool());
    field<QToolButton>(*dialog, "shapeGeometryRadiusLinkButton")->click();
    // A linked same-value commit sets all four corners, even if TL did not change.
    auto* edit = radius->findChild<QLineEdit*>();
    QMetaObject::invokeMethod(edit, "textEdited", Qt::DirectConnection, Q_ARG(QString, edit->text()));
    QMetaObject::invokeMethod(radius, "editingFinished", Qt::DirectConnection);
    field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->setValue(17);
    save_widget_artifact("ui_batch_shape_dialog", *dialog);
    dialog->accept();
  });
  const auto edited = request_shape_appearance_settings(&window, {}, context.originals.front(), {},
      nullptr, nullptr, nullptr, {}, {}, {}, &context);
  CHECK(edited.has_value());
  const auto rect = apply_shape_appearance_edits(context.originals[1], *edited);
  for (const auto radius : rect.geometry->corner_radii) CHECK(radius == 2);
  const auto assembled = assemble_shape_appearance(compound_content, apply_shape_appearance_edits(context.originals[2], *edited));
  CHECK(assembled.parts[0].stroke.width == 17 && assembled.parts[1].stroke.width == 17);
  CHECK(assembled.parts[0].fill == a.fill && assembled.parts[1].fill == b.fill);
  // A width is also the stroke switch (issue 311): typing one turns the off part's stroke on.
  CHECK(assembled.parts[1].stroke.content == b.stroke.content && assembled.parts[1].stroke.enabled);
  CHECK(apply_shape_appearance_edits(context.originals[0], *edited).geometry == context.originals[0].geometry);
  CHECK(!apply_shape_appearance_edits(context.originals[3], *edited).geometry.has_value());
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    field<QCheckBox>(*dialog, "shapeAppearancePreviewCheck")->setChecked(false);
    field<QPushButton>(*dialog, "shapeAppearanceResetButton")->click();
    CHECK(!preview.preview_enabled);
    dialog->accept();
  });
  const auto reset = request_shape_appearance_settings(nullptr, [&](const auto& value) { preview = value; },
      context.originals.front(), {}, nullptr, nullptr, nullptr, {}, {}, {}, &context);
  CHECK(reset.has_value());
  CHECK(apply_shape_appearance_edits(context.originals[1], *reset).geometry == context.originals[1].geometry);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    field<QCheckBox>(*dialog, "shapeAppearancePreviewCheck")->setChecked(false);
    field<QPushButton>(*dialog, "shapeAppearanceApplyAllButton")->click();
    dialog->accept();
  });
  const auto all = request_shape_appearance_settings(nullptr, {}, context.originals.front(), {},
      nullptr, nullptr, nullptr, {}, {}, {}, &context);
  CHECK(all && all->preview_enabled);
  const auto replaced = assemble_shape_appearance(compound_content, apply_shape_appearance_edits(context.originals[2], *all));
  for (const auto& part : replaced.parts) {
    CHECK(part.fill == context.originals.front().fill);
    CHECK(part.stroke == context.originals.front().stroke);
  }
  CHECK(replaced.path == compound_content.path);
}

void select_batch_layers(MainWindow& window, const std::vector<LayerId>& ids) {
  MainWindowTestAccess::refresh_layer_ui(window);
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list);
  QListWidgetItem* current = nullptr;
  {
    const QSignalBlocker blocker(list);
    list->clearSelection();
    for (int row = 0; row < list->count(); ++row) {
      auto* item = list->item(row);
      const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
      if (std::find(ids.begin(), ids.end(), id) != ids.end()) item->setSelected(true);
      if (id == ids.front()) current = item;
    }
    list->setCurrentItem(current, QItemSelectionModel::NoUpdate);
  }
  QMetaObject::invokeMethod(list, "itemSelectionChanged", Qt::DirectConnection);
}

void ui_batch_style_transaction_groups_and_row_selection() {
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto group_id = doc.allocate_layer_id(), child_id = doc.allocate_layer_id(), untouched_id = doc.allocate_layer_id();
  Layer group(group_id, "Folder", LayerKind::Group);
  auto child = rectangle(child_id, 10, 120, {}, {200, 40, 20});
  auto untouched = rectangle(untouched_id, 210, 90, {}, {20, 140, 200});
  LayerDropShadow shadow; shadow.enabled = true; shadow.size = 3;
  group.layer_style().drop_shadows = {shadow}; group.set_opacity(0.734F);
  shadow.size = 17; child.layer_style().drop_shadows = {shadow};
  shadow.size = 31; untouched.layer_style().drop_shadows = {shadow};
  group.children().push_back(std::move(child)); group.children().push_back(std::move(untouched));
  doc.add_layer(std::move(group));
  select_batch_layers(window, {group_id, child_id});
  auto* list = field<QListWidget>(window, "layerList");
  CHECK(list->selectedItems().size() == 2);
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Drop Shadow");
    field<QSpinBox>(*dialog, "layerStyleDropShadowSizeSpin")->setValue(9);
    CHECK(process_events_until([&] { return std::as_const(doc).find_layer(child_id)->layer_style().drop_shadows[0].size == 9; }));
    field<QCheckBox>(*dialog, "layerStylePreviewCheck")->setChecked(false);
    CHECK(process_events_until([&] { return std::as_const(doc).find_layer(child_id)->layer_style().drop_shadows[0].size == 17; }));
    dialog->accept();
  });
  field<QAction>(window, "layerBlendingOptionsAction")->trigger();
  CHECK(std::as_const(doc).find_layer(group_id)->layer_style().drop_shadows[0].size == 9);
  CHECK(std::as_const(doc).find_layer(child_id)->layer_style().drop_shadows[0].size == 9);
  CHECK(std::as_const(doc).find_layer(untouched_id)->layer_style().drop_shadows[0].size == 31);
  CHECK(std::as_const(doc).find_layer(group_id)->opacity() == 0.734F);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(std::as_const(doc).find_layer(group_id)->layer_style().drop_shadows[0].size == 3);
  CHECK(std::as_const(doc).find_layer(child_id)->layer_style().drop_shadows[0].size == 17);
  MainWindowTestAccess::redo(window);
  select_batch_layers(window, {group_id, child_id});
  const auto details = [&] { return field<QLabel>(*list->itemWidget(list->currentItem()), "layerRowDetails"); };
  auto* row = details();
  send_mouse(*row, QEvent::MouseButtonPress, row->rect().center(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  row = details();
  send_mouse(*row, QEvent::MouseButtonRelease, row->rect().center(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    CHECK(field<QLabel>(*dialog, "layerStyleSelectionSummary")->text().contains("2"));
    CHECK(list->selectedItems().size() == 2);
    dialog->accept();
  });
  row = details(); send_double_click(*row, row->rect().center());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  CHECK(list->selectedItems().size() == 2);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    field<QSpinBox>(*dialog, "layerStyleOpacitySpin")->setValue(25);
    dialog->reject();
  });
  field<QAction>(window, "layerBlendingOptionsAction")->trigger();
  CHECK(std::as_const(doc).find_layer(group_id)->opacity() == 0.734F);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
}

void ui_batch_shape_transaction_cancel_and_noop() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(rectangle(first, 10, 130, {1, 2, 3, 4}, {210, 20, 30}));
  doc.add_layer(rectangle(second, 210, 30, {}, {10, 180, 40}));
  select_batch_layers(window, {first, second});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  const auto original = *std::as_const(doc).find_layer(second)->vector_shape();
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->setValue(23);
    field<QDoubleSpinBox>(*dialog, "shapeGeometryRadiusTopLeftSpin")->setValue(40);
    dialog->reject(); // The second raster batch may still be pending.
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
  CHECK(shape_vector_appearance_equal(*std::as_const(doc).find_layer(second)->vector_shape(), original));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);
  QTimer::singleShot(0, [&] { appearance_dialog("shapeAppearanceDialog")->accept(); });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);
  QTimer::singleShot(0, [&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->setValue(8);
    field<QCheckBox>(*dialog, "shapeAppearancePreviewCheck")->setChecked(false);
    CHECK(shape_vector_appearance_equal(*std::as_const(doc).find_layer(second)->vector_shape(), original));
    dialog->accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(std::as_const(doc).find_layer(second)->vector_shape()->stroke.width == 8);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
}

void ui_batch_style_pattern_collision_cancel_undo() {
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(rectangle(first, 10, 130, {}, {210, 20, 30}));
  doc.add_layer(rectangle(second, 210, 30, {}, {10, 180, 40}));
  select_batch_layers(window, {first, second});
  PatternResource tile;
  tile.id = "55555555-1111-2222-3333-444444444444"; tile.name = "Batch Tile";
  tile.tile = solid_pixels(3, 3, PixelFormat::rgba8(), QColor(30, 180, 40));
  tile.provenance = PatternProvenance::Authored;
  auto embedded = tile; embedded.tile = solid_pixels(2, 2, PixelFormat::rgba8(), QColor(190, 20, 30));
  doc.metadata().patterns.adopt(embedded);
  LayerStyle recipe; LayerPatternOverlay overlay;
  overlay.enabled = true; overlay.pattern_id = tile.id; overlay.pattern_name = tile.name;
  recipe.pattern_overlays = {overlay};
  const auto preset = window.style_library().add_style("Batch collision", recipe, {}, std::span<const PatternResource>(&tile, 1));
  CHECK(!preset.isEmpty());
  std::string alias;
  const auto run = [&](bool accept) {
    QTimer::singleShot(0, [&] {
      auto* dialog = appearance_dialog("patchyLayerStyleDialog");
      CHECK(QMetaObject::invokeMethod(field<QWidget>(*dialog, "layerStyleStylesBrowser"), "style_clicked",
                                      Qt::DirectConnection, Q_ARG(QString, preset)));
      const auto& previews = std::as_const(doc);
      CHECK(previews.metadata().patterns.patterns.size() == 2);
      alias = previews.find_layer(first)->layer_style().pattern_overlays.front().pattern_id;
      CHECK(alias != tile.id);
      CHECK(previews.find_layer(second)->layer_style().pattern_overlays.front().pattern_id == alias);
      field<QCheckBox>(*dialog, "layerStylePreviewCheck")->setChecked(false);
      CHECK(previews.metadata().patterns.patterns.size() == 1);
      if (accept) dialog->accept(); else dialog->reject();
    });
    field<QAction>(window, "layerBlendingOptionsAction")->trigger();
  };
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  run(false);
  CHECK(std::as_const(doc).metadata().patterns.patterns.size() == 1);
  CHECK(std::as_const(doc).find_layer(second)->layer_style().pattern_overlays.empty());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);
  run(true);
  CHECK(std::as_const(doc).metadata().patterns.patterns.size() == 2);
  CHECK(std::as_const(doc).metadata().patterns.find(alias)->tile.pixel(0, 0)[1] == 180);
  CHECK(std::as_const(doc).metadata().patterns.find(tile.id)->tile.pixel(0, 0)[0] == 190);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(doc, std::filesystem::path("test-artifacts") / "batch-pattern-styles.psd");
  MainWindowTestAccess::undo(window);
  CHECK(std::as_const(doc).metadata().patterns.patterns.size() == 1);
  CHECK(std::as_const(doc).find_layer(second)->layer_style().pattern_overlays.empty());
  MainWindowTestAccess::redo(window);
  CHECK(std::as_const(doc).find_layer(second)->layer_style().pattern_overlays.front().pattern_id == alias);
  CHECK(window.style_library().remove_style(preset));
}

void ui_batch_shape_editor_entries_and_document_switch() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(rectangle(first, 10, 130, {}, {210, 20, 30}));
  doc.add_layer(rectangle(second, 210, 30, {}, {10, 180, 40}));
  select_batch_layers(window, {first, second});
  auto* list = field<QListWidget>(window, "layerList");
  const auto badge = [&] {
    auto* item = require_layer_item(*list, QString::fromStdString("Rectangle " + std::to_string(second)));
    return field<QToolButton>(*list->itemWidget(item), "layerVectorBadgeButton");
  };
  const auto open_badge = [&](int count) {
    bool seen = false;
    QTimer closer;
    QObject::connect(&closer, &QTimer::timeout, &window, [&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog("shapeAppearanceDialog"));
      if (!dialog) return;
      seen = true; closer.stop();
      CHECK(list->selectedItems().size() == count);
      if (count > 1) CHECK(field<QLabel>(*dialog, "shapeAppearanceSelectionSummary")->text().contains("2 layers selected"));
      dialog->reject();
    });
    closer.start(1);
    badge()->click();
    CHECK(process_events_until([&] { return seen; }));
  };
  open_badge(2);
  select_batch_layers(window, {first});
  open_badge(1);
  CHECK(doc.active_layer_id() == second);
  select_batch_layers(window, {first, second});
  auto* canvas = require_canvas(window);
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  bool canvas_opened = false;
  QTimer closer;
  QObject::connect(&closer, &QTimer::timeout, &window, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog("shapeAppearanceDialog"));
    if (!dialog) return;
    canvas_opened = true;
    CHECK(list->selectedItems().size() == 2);
    dialog->reject();
  });
  closer.start(1);
  send_double_click(*canvas, canvas->widget_position_for_document_point(QPoint(60, 50)));
  CHECK(canvas_opened);
  CHECK(list->selectedItems().size() == 2);
  canvas_opened = false;
  QTimer::singleShot(0, [&] {
    auto* menu = window.findChild<QMenu*>("layerContextMenu");
    CHECK(menu);
    auto* action = field<QAction>(*menu, "layerContextEditShapeAppearanceAction");
    menu->setActiveAction(action);
    send_key(*menu, Qt::Key_Return);
  });
  CHECK(QMetaObject::invokeMethod(list, "customContextMenuRequested", Qt::DirectConnection,
      Q_ARG(QPoint, list->visualItemRect(list->currentItem()).center())));
  closer.stop();
  CHECK(canvas_opened);
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  field<QDoubleSpinBox>(window, "vectorStrokeWidthSpin")->setValue(16);
  Document other(32, 32, PixelFormat::rgba8());
  other.add_pixel_layer("Other", PixelBuffer(32, 32, PixelFormat::rgba8()));
  window.add_document_session(std::move(other), QStringLiteral("Other document"));
  CHECK(std::as_const(doc).find_layer(first)->vector_shape()->stroke.width == 16);
  CHECK(std::as_const(doc).find_layer(second)->vector_shape()->stroke.width == 16);
  CHECK(MainWindowTestAccess::document(window).width() == 32);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0);
}

void ui_batch_shape_toolbar_pattern_collision() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  PatternResource embedded;
  embedded.id = generate_pattern_uuid(); embedded.name = "Embedded red";
  embedded.tile = solid_pixels(3, 3, PixelFormat::rgba8(), QColor(190, 20, 30));
  doc.metadata().patterns.adopt(embedded);
  const auto storage_id = window.pattern_library().add_pattern("Batch green",
      solid_pixels(2, 2, PixelFormat::rgba8(), QColor(30, 180, 40)), {}, QString::fromStdString(embedded.id));
  CHECK(!storage_id.isEmpty());
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  for (const auto id : {first, second}) {
    auto layer = rectangle(id, id == first ? 10 : 210, 80, {}, {});
    auto content = *std::as_const(layer).vector_shape();
    content.fill.kind = VectorFillKind::Pattern;
    content.fill.pattern_id = embedded.id;
    content.fill.pattern_scale = id == first ? 1.0 : 2.5;
    layer.set_vector_shape(std::move(content));
    doc.add_layer(std::move(layer));
  }
  select_batch_layers(window, {first, second});
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  QTimer::singleShot(0, [&] {
    auto* menu = field<QMenu>(window, "vectorFillPaintMenu");
    QTimer::singleShot(0, [&] {
      auto* manager = appearance_dialog("patternManagerDialog");
      CHECK(field<QLineEdit>(*manager, "patternManagerNameEdit")->text() == "Batch green");
      field<QPushButton>(*manager, "patternManagerUseButton")->click();
    });
    field<QAction>(*menu, "vectorFillPatternAction")->trigger();
    menu->close();
  });
  field<QToolButton>(window, "vectorFillSwatchButton")->click();
  const auto alias = std::as_const(doc).find_layer(first)->vector_shape()->fill.pattern_id;
  CHECK(alias != embedded.id);
  CHECK(std::as_const(doc).find_layer(second)->vector_shape()->fill.pattern_id == alias);
  CHECK(std::as_const(doc).find_layer(second)->vector_shape()->fill.pattern_scale == 2.5);
  CHECK(std::as_const(doc).metadata().patterns.patterns.size() == 2);
  CHECK(std::as_const(doc).metadata().patterns.find(alias)->tile.pixel(0, 0)[1] == 180);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(std::as_const(doc).metadata().patterns.patterns.size() == 1);
  CHECK(std::as_const(doc).find_layer(second)->vector_shape()->fill.pattern_id == embedded.id);
  CHECK(window.pattern_library().remove_pattern(storage_id));
}

void ui_batch_shape_toolbar_undo_selection_and_psd() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(rectangle(first, 10, 130, {}, {210, 20, 30}));
  doc.add_layer(rectangle(second, 210, 30, {1, 3, 5, 7}, {10, 180, 40}));
  select_batch_layers(window, {first, second});
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  field<QDoubleSpinBox>(window, "vectorStrokeWidthSpin")->setValue(11);
  // The pending edit finishes against its original targets before selection changes.
  select_batch_layers(window, {first});
  CHECK(std::as_const(doc).find_layer(first)->vector_shape()->stroke.width == 11);
  CHECK(std::as_const(doc).find_layer(second)->vector_shape()->stroke.width == 11);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(std::as_const(doc).find_layer(second)->vector_shape()->stroke.width == static_cast<double>(second));
  MainWindowTestAccess::redo(window);
  select_batch_layers(window, {first, second});
  field<QSpinBox>(window, "shapeCornerRadiusSpin")->setValue(120);
  CHECK(process_events_until([&] {
    return std::as_const(doc).find_layer(second)->vector_shape()->origination.front().corner_radii[0] == 120;
  }));
  const auto* shape = std::as_const(doc).find_layer(second)->vector_shape();
  CHECK(shape->origination.front().left == 210 && shape->origination.front().right == 240);
  for (auto radius : shape->origination.front().corner_radii) CHECK(radius == 120);
  for (bool psb : {false, true}) {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(doc, {psb});
    const auto reopened = patchy::psd::DocumentIo::read(bytes);
    const auto found = std::find_if(reopened.layers().begin(), reopened.layers().end(), [&](const auto& layer) {
      return layer.name() == "Rectangle " + std::to_string(second);
    });
    CHECK(found != reopened.layers().end() && found->vector_shape());
    CHECK(found->vector_shape()->stroke.width == 11);
    CHECK(found->vector_shape()->origination.front().corner_radii[0] == 120);
    ensure_artifact_dir();
    patchy::psd::DocumentIo::write_layered_rgb8_file(doc,
        std::filesystem::path("test-artifacts") / (psb ? "batch-appearance.psb" : "batch-appearance.psd"), {psb});
  }
  field<QSpinBox>(window, "shapeCornerRadiusSpin")->setValue(0);
  QMetaObject::invokeMethod(field<QSpinBox>(window, "shapeCornerRadiusSpin"), "editingFinished", Qt::DirectConnection);
  for (const auto id : {first, second}) {
    const auto& content = *std::as_const(doc).find_layer(id)->vector_shape();
    for (const auto radius : content.origination.front().corner_radii) CHECK(radius == 0);
    for (const auto& anchor : content.path.subpaths.front().anchors) {
      CHECK(anchor.in_x == anchor.anchor_x && anchor.in_y == anchor.anchor_y);
      CHECK(anchor.out_x == anchor.anchor_x && anchor.out_y == anchor.anchor_y);
    }
  }
  auto* second_layer = doc.find_layer(second);
  second_layer->set_lock_flags(kLayerLockAll);
  const auto radius_before = MainWindowTestAccess::active_session_undo_depth(window);
  select_batch_layers(window, {second});
  auto* spin = field<QSpinBox>(window, "shapeCornerRadiusSpin");
  spin->setValue(20);
  QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection);
  CHECK(require_canvas(window)->shape_corner_radius() == 20);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == radius_before);
  select_batch_layers(window, {first});
  CHECK(spin->value() == 0);
  auto* edit = spin->findChild<QLineEdit*>();
  QMetaObject::invokeMethod(edit, "textEdited", Qt::DirectConnection, Q_ARG(QString, edit->text()));
  QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection);
  CHECK(require_canvas(window)->shape_corner_radius() == 0);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == radius_before);
}

} // namespace

std::vector<patchy::test::TestCase> batch_appearance_tests() {
  return {
    {"ui_batch_shape_fields_radii_and_full_apply", ui_batch_shape_fields_radii_and_full_apply},
    {"ui_batch_shape_same_value_and_nested_picker_cancel", ui_batch_shape_same_value_and_nested_picker_cancel},
    {"ui_batch_style_occurrences_missing_and_navigation", ui_batch_style_occurrences_missing_and_navigation},
    {"ui_batch_style_gradient_placement_and_picker_checkpoint", ui_batch_style_gradient_placement_and_picker_checkpoint},
    {"ui_batch_style_stack_order_full_apply_and_no_style", ui_batch_style_stack_order_full_apply_and_no_style},
    {"ui_batch_shape_mixed_geometry_compound_and_reset", ui_batch_shape_mixed_geometry_compound_and_reset},
    {"ui_batch_style_transaction_groups_and_row_selection", ui_batch_style_transaction_groups_and_row_selection},
    {"ui_batch_shape_transaction_cancel_and_noop", ui_batch_shape_transaction_cancel_and_noop},
    {"ui_batch_style_pattern_collision_cancel_undo", ui_batch_style_pattern_collision_cancel_undo},
    {"ui_batch_shape_editor_entries_and_document_switch", ui_batch_shape_editor_entries_and_document_switch},
    {"ui_batch_shape_toolbar_pattern_collision", ui_batch_shape_toolbar_pattern_collision},
    {"ui_batch_shape_toolbar_undo_selection_and_psd", ui_batch_shape_toolbar_undo_selection_and_psd},
  };
}
