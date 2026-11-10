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
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QGroupBox>
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    CHECK(field<QLabel>(*dialog, "shapeAppearanceSelectionSummary")->text().contains("2"));
    auto* radius_link = field<QToolButton>(*dialog, "shapeGeometryRadiusLinkButton");
    CHECK(!radius_link->isChecked());
    CHECK(!field<QDoubleSpinBox>(*dialog, "shapeGeometryWidthSpin")->isEnabled());
    CHECK(field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->property("appearanceMixed").toBool());
    field<QDoubleSpinBox>(*dialog, "shapeGeometryRadiusTopLeftSpin")->setValue(90);
    field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->setValue(7);
    dialog->accept();
  }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    auto* width = field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin");
    auto* text = width->findChild<QLineEdit*>();
    CHECK(text);
    // An explicit commit of the displayed value applies it to the other shape.
    QMetaObject::invokeMethod(text, "textEdited", Qt::DirectConnection, Q_ARG(QString, text->text()));
    QMetaObject::invokeMethod(width, "editingFinished", Qt::DirectConnection);
    QTimer::singleShot(0, guarded([&] {
      auto* picker_dialog = appearance_dialog("patchyColorDialog");
      auto* picker = field<PatchyColorPicker>(*picker_dialog, "patchyAdvancedColorPicker");
      picker->setCurrentColor(QColor(30, 40, 230));
      CHECK(apply_shape_appearance_edits(context.originals[1], preview).fill.color.blue == 230);
      picker_dialog->reject();
    }));
    field<QPushButton>(*dialog, "shapeFillColorButton")->click();
    CHECK(apply_shape_appearance_edits(context.originals[1], preview).fill == context.originals[1].fill);
    dialog->accept();
  }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Drop Shadow");
    CHECK(field<QSpinBox>(*dialog, "layerStyleDropShadowSizeSpin")->property("appearanceMixed").toBool());
    field<QSpinBox>(*dialog, "layerStyleDropShadowSizeSpin")->setValue(9);
    select_style_page(*dialog, "Stroke");
    select_style_page(*dialog, "Blending Options");
    dialog->accept();
  }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    CHECK(QMetaObject::invokeMethod(field<QWidget>(*dialog, "layerStyleStylesBrowser"), "no_style_clicked", Qt::DirectConnection));
    dialog->accept();
  }));
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
  QTimer::singleShot(0, guarded([&] {
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
  }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    field<QCheckBox>(*dialog, "shapeAppearancePreviewCheck")->setChecked(false);
    field<QPushButton>(*dialog, "shapeAppearanceResetButton")->click();
    CHECK(!preview.preview_enabled);
    dialog->accept();
  }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    select_style_page(*dialog, "Drop Shadow");
    field<QSpinBox>(*dialog, "layerStyleDropShadowSizeSpin")->setValue(9);
    CHECK(process_events_until([&] { return std::as_const(doc).find_layer(child_id)->layer_style().drop_shadows[0].size == 9; }));
    field<QCheckBox>(*dialog, "layerStylePreviewCheck")->setChecked(false);
    CHECK(process_events_until([&] { return std::as_const(doc).find_layer(child_id)->layer_style().drop_shadows[0].size == 17; }));
    dialog->accept();
  }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("patchyLayerStyleDialog");
    CHECK(field<QLabel>(*dialog, "layerStyleSelectionSummary")->text().contains("2"));
    CHECK(list->selectedItems().size() == 2);
    dialog->accept();
  }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = appearance_dialog("shapeAppearanceDialog");
    field<QDoubleSpinBox>(*dialog, "shapeStrokeWidthSpin")->setValue(8);
    field<QCheckBox>(*dialog, "shapeAppearancePreviewCheck")->setChecked(false);
    CHECK(shape_vector_appearance_equal(*std::as_const(doc).find_layer(second)->vector_shape(), original));
    dialog->accept();
  }));
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
    QTimer::singleShot(0, guarded([&] {
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
    }));
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
  QTimer::singleShot(0, guarded([&] {
    auto* menu = window.findChild<QMenu*>("layerContextMenu");
    CHECK(menu);
    auto* action = field<QAction>(*menu, "layerContextEditShapeAppearanceAction");
    menu->setActiveAction(action);
    send_key(*menu, Qt::Key_Return);
  }));
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
    QTimer::singleShot(0, guarded([&] {
      auto* manager = appearance_dialog("patternManagerDialog");
      CHECK(field<QLineEdit>(*manager, "patternManagerNameEdit")->text() == "Batch green");
      field<QPushButton>(*manager, "patternManagerUseButton")->click();
    }));
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

Layer line_layer(LayerId id, double weight, bool arrow_start, bool arrow_end) {
  Layer layer(id, "Line " + std::to_string(id), PixelBuffer());
  LiveShapeParams params;
  params.kind = LiveShapeKind::Line;
  params.line_start_x = 60; params.line_start_y = 80.0 * static_cast<double>(id);
  params.line_end_x = 400; params.line_end_y = params.line_start_y;
  params.line_weight = weight; params.arrow_start = arrow_start; params.arrow_end = arrow_end;
  give_line_arrowheads_default_size(params);
  params.index = static_cast<int>(id);
  VectorShapeContent content;
  content.path.subpaths = generate_live_shape_subpaths(params);
  if (const auto bounds = content.path.bounds()) {
    params.left = bounds->left; params.top = bounds->top; params.right = bounds->right; params.bottom = bounds->bottom;
  }
  content.origination.push_back(params);
  content.fill.kind = VectorFillKind::Solid; content.fill.color = {0, 0, 0};
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  update_vector_shape_raster(layer, Rect::from_size(600, 400), nullptr);
  return layer;
}

const LiveShapeParams& line_params(const Document& doc, LayerId id) {
  return std::as_const(doc).find_layer(id)->vector_shape()->origination.front();
}

// GitHub #622: the Line bar's Weight and arrowhead toggles edit a selected line, one undo
// step each, and show the selected line's values.
void ui_line_options_bar_edits_selected_line() {
  SettingsValueRestorer arrow_start_setting(QStringLiteral("tools/lineArrowStart"));
  SettingsValueRestorer arrow_end_setting(QStringLiteral("tools/lineArrowEnd"));
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(line_layer(first, 10, false, false));
  doc.add_layer(line_layer(second, 12, true, false));
  require_action_by_text(window, QStringLiteral("Line"))->trigger();
  auto* weight = field<QDoubleSpinBox>(window, "vectorLineWeightSpin");
  auto* arrow_start = field<QPushButton>(window, "lineArrowStartButton");
  auto* arrow_end = field<QPushButton>(window, "lineArrowEndButton");
  // Selecting a line shows its weight and heads.
  select_batch_layers(window, {second});
  CHECK(weight->value() == 12);
  CHECK(arrow_start->isChecked() && !arrow_end->isChecked());
  select_batch_layers(window, {first});
  CHECK(weight->value() == 10);
  CHECK(!arrow_start->isChecked() && !arrow_end->isChecked());

  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  // A spin drag is one undo step.
  weight->setValue(14);
  weight->setValue(16);
  QMetaObject::invokeMethod(weight, "editingFinished", Qt::DirectConnection);
  CHECK(line_params(doc, first).line_weight == 16);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  CHECK(line_params(doc, second).line_weight == 12);
  // The layer's outline follows: a 16 px band around y = 80.
  const auto bounds = std::as_const(doc).find_layer(first)->vector_shape()->path.bounds();
  CHECK(bounds && std::abs(bounds->bottom - bounds->top - 16.0) < 1e-6);

  arrow_end->click();
  CHECK(line_params(doc, first).arrow_end && !line_params(doc, first).arrow_start);
  CHECK(line_params(doc, first).arrow_width == 80 && line_params(doc, first).arrow_length == 160);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 2);
  // A weight change rescales the heads.
  weight->setValue(8);
  QMetaObject::invokeMethod(weight, "editingFinished", Qt::DirectConnection);
  CHECK(line_params(doc, first).line_weight == 8);
  CHECK(line_params(doc, first).arrow_width == 40 && line_params(doc, first).arrow_length == 80);
  arrow_start->click();
  CHECK(line_params(doc, first).arrow_start && line_params(doc, first).arrow_end);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 4);
  // Saved and reopened, the heads and weight survive in the live-shape data.
  const auto reopened = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(doc, {}));
  const auto found = std::find_if(reopened.layers().begin(), reopened.layers().end(),
                                  [&](const auto& layer) { return layer.name() == "Line " + std::to_string(first); });
  CHECK(found != reopened.layers().end() && found->vector_shape() && found->vector_shape()->origination.size() == 1);
  CHECK(found->vector_shape()->origination.front().line_weight == 8);
  CHECK(found->vector_shape()->origination.front().arrow_start && found->vector_shape()->origination.front().arrow_end);

  MainWindowTestAccess::undo(window);
  CHECK(!line_params(doc, first).arrow_start && line_params(doc, first).arrow_end);
  MainWindowTestAccess::undo(window);
  CHECK(line_params(doc, first).line_weight == 16 && line_params(doc, first).arrow_width == 80);
  MainWindowTestAccess::undo(window);
  CHECK(!line_params(doc, first).arrow_end);
  MainWindowTestAccess::undo(window);
  CHECK(line_params(doc, first).line_weight == 10);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);
  // Two lines: one click sets both.
  select_batch_layers(window, {first, second});
  {
    const QSignalBlocker blocker(arrow_end);
    arrow_end->setChecked(false);
  }
  arrow_end->click();
  CHECK(line_params(doc, first).arrow_end && line_params(doc, second).arrow_end);
  CHECK(line_params(doc, second).arrow_start && line_params(doc, second).arrow_width == 60);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
}

// GitHub #622: Shape Appearance has Start and End arrowhead checkboxes for a line; they
// preview live, Cancel restores the line, OK is one undo step.
bool inside(const QWidget* widget, const char* group);

// GitHub #622 follow-up 3: the Start / End row is labeled Heads:, in the Arrowheads label
// column (the same left edge as Width:) and on the checkboxes' line.
void check_heads_label(QDialog& dialog) {
  auto* label = field<QLabel>(dialog, "shapeArrowHeadsLabel");
  auto* start = field<QCheckBox>(dialog, "shapeGeometryLineArrowStartCheck");
  if (label == nullptr || start == nullptr) return;
  CHECK(label->text() == QStringLiteral("Heads:"));
  CHECK(label->isVisible() && inside(label, "shapeArrowheadsGroup"));
  QLabel* width_label = nullptr;
  for (auto* candidate : dialog.findChildren<QLabel*>())
    if (candidate->text() == QStringLiteral("Width:") && inside(candidate, "shapeArrowheadsGroup"))
      width_label = candidate;
  CHECK(width_label != nullptr);
  if (width_label != nullptr)
    CHECK(label->mapTo(&dialog, QPoint(0, 0)).x() == width_label->mapTo(&dialog, QPoint(0, 0)).x());
  const auto label_middle = label->mapTo(&dialog, label->rect().center()).y();
  const auto start_top = start->mapTo(&dialog, QPoint(0, 0)).y();
  CHECK(label_middle >= start_top && label_middle <= start_top + start->height());
  CHECK(label->mapTo(&dialog, QPoint(label->width(), 0)).x() <= start->mapTo(&dialog, QPoint(0, 0)).x());
}

void ui_shape_appearance_line_arrowheads() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = doc.allocate_layer_id();
  doc.add_layer(line_layer(id, 6, false, true));
  select_batch_layers(window, {id});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* start = field<QCheckBox>(dialog, "shapeGeometryLineArrowStartCheck");
    auto* end = field<QCheckBox>(dialog, "shapeGeometryLineArrowEndCheck");
    CHECK(!start->isChecked() && end->isChecked());
    check_heads_label(dialog);
    start->setChecked(true);
    end->setChecked(false);
    CHECK(process_events_until([&] { return line_params(doc, id).arrow_start && !line_params(doc, id).arrow_end; }));
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
  CHECK(!line_params(doc, id).arrow_start && line_params(doc, id).arrow_end);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);

  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    field<QCheckBox>(dialog, "shapeGeometryLineArrowStartCheck")->setChecked(true);
    field<QDoubleSpinBox>(dialog, "shapeGeometryLineWeightSpin")->setValue(9);
    QApplication::processEvents();
    save_widget_artifact("ui_shape_appearance_line_arrowheads", dialog);
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(line_params(doc, id).arrow_start && line_params(doc, id).arrow_end);
  CHECK(line_params(doc, id).line_weight == 9);
  CHECK(line_params(doc, id).arrow_width == 45 && line_params(doc, id).arrow_length == 90);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(!line_params(doc, id).arrow_start && line_params(doc, id).line_weight == 6);
}

// GitHub #622 follow-up: Shape Appearance's Arrowheads section sets a line's head Width and
// Length (percent of the weight) and Concavity, previews live, OK is one undo step, the
// sizes survive a PSD save, and a later weight change keeps the line's own proportions.
void ui_shape_appearance_arrowhead_size() {
  SettingsValueRestorer arrow_start_setting(QStringLiteral("tools/lineArrowStart"));
  SettingsValueRestorer arrow_end_setting(QStringLiteral("tools/lineArrowEnd"));
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = doc.allocate_layer_id(), bare = doc.allocate_layer_id();
  doc.add_layer(line_layer(id, 6, false, true));
  doc.add_layer(line_layer(bare, 4, false, false));
  // A line without heads: the size rows wait for one; Cancel keeps the line.
  select_batch_layers(window, {bare});
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    CHECK(dialog.findChild<QWidget*>(QStringLiteral("shapeArrowheadsGroup")) != nullptr);
    auto* width = field<QSpinBox>(dialog, "shapeArrowWidthSpin");
    CHECK(!width->isEnabled() && !field<QSpinBox>(dialog, "shapeArrowConcavitySpin")->isEnabled());
    field<QCheckBox>(dialog, "shapeGeometryLineArrowEndCheck")->setChecked(true);
    CHECK(width->isEnabled());
    CHECK(process_events_until([&] { return line_params(doc, bare).arrow_end; }));
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
  CHECK(!line_params(doc, bare).arrow_end);

  select_batch_layers(window, {id});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* width = field<QSpinBox>(dialog, "shapeArrowWidthSpin");
    auto* length = field<QSpinBox>(dialog, "shapeArrowLengthSpin");
    auto* concavity = field<QSpinBox>(dialog, "shapeArrowConcavitySpin");
    CHECK(width->value() == 500 && length->value() == 1000 && concavity->value() == 0);
    CHECK(width->isEnabled() && width->minimum() == 10 && width->maximum() == 1000);
    CHECK(length->minimum() == 10 && length->maximum() == 5000);
    CHECK(concavity->minimum() == -50 && concavity->maximum() == 50);
    width->setValue(300);
    length->setValue(800);
    concavity->setValue(25);
    CHECK(process_events_until([&] {
      const auto& params = line_params(doc, id);
      return params.arrow_width == 18 && params.arrow_length == 48 && params.arrow_concavity == 25;
    }));
    CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
    save_widget_artifact("ui_shape_appearance_arrowhead_size", dialog);
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(line_params(doc, id).arrow_width == 18 && line_params(doc, id).arrow_length == 48);
  CHECK(line_params(doc, id).arrow_concavity == 25 && line_params(doc, id).line_weight == 6);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  // The heads are 18 px wide: the layer's outline is that tall.
  const auto bounds = std::as_const(doc).find_layer(id)->vector_shape()->path.bounds();
  CHECK(bounds && std::abs(bounds->bottom - bounds->top - 18.0) < 1e-6);
  {
    // The concave end head, zoomed in, for the review.
    auto* canvas = require_canvas(window);
    const auto& params = line_params(doc, id);
    canvas->zoom_to_document_rect(QRect(static_cast<int>(params.line_end_x) - 100,
                                        static_cast<int>(params.line_end_y) - 30, 120, 60));
    QApplication::processEvents();
    save_widget_artifact("ui_shape_appearance_arrowhead_canvas", *canvas);
  }

  // A Line bar weight change keeps the 300% / 800% proportions.
  require_action_by_text(window, QStringLiteral("Line"))->trigger();
  select_batch_layers(window, {id});
  auto* weight = field<QDoubleSpinBox>(window, "vectorLineWeightSpin");
  weight->setValue(10);
  QMetaObject::invokeMethod(weight, "editingFinished", Qt::DirectConnection);
  CHECK(line_params(doc, id).line_weight == 10);
  CHECK(std::abs(line_params(doc, id).arrow_width - 30) < 1e-9 && std::abs(line_params(doc, id).arrow_length - 80) < 1e-9);
  CHECK(line_params(doc, id).arrow_concavity == 25);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 2);
  // And so does the dialog's Weight field.
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    CHECK(field<QSpinBox>(dialog, "shapeArrowWidthSpin")->value() == 300);
    field<QDoubleSpinBox>(dialog, "shapeGeometryLineWeightSpin")->setValue(4);
    QApplication::processEvents();
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(std::abs(line_params(doc, id).arrow_width - 12) < 1e-9 && std::abs(line_params(doc, id).arrow_length - 32) < 1e-9);

  // Saved and reopened, the head sizes and concavity survive.
  const auto reopened = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(doc, {}));
  const auto found = std::find_if(reopened.layers().begin(), reopened.layers().end(),
                                  [&](const auto& layer) { return layer.name() == "Line " + std::to_string(id); });
  CHECK(found != reopened.layers().end() && found->vector_shape() && found->vector_shape()->origination.size() == 1);
  const auto& saved = found->vector_shape()->origination.front();
  CHECK(std::abs(saved.arrow_width - 12) < 1e-9 && std::abs(saved.arrow_length - 32) < 1e-9);
  CHECK(saved.arrow_concavity == 25 && saved.line_weight == 4);

  MainWindowTestAccess::undo(window);
  MainWindowTestAccess::undo(window);
  CHECK(line_params(doc, id).line_weight == 6 && line_params(doc, id).arrow_width == 18);
  MainWindowTestAccess::undo(window);
  CHECK(line_params(doc, id).arrow_width == 30 && line_params(doc, id).arrow_length == 60);
  CHECK(line_params(doc, id).arrow_concavity == 0);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);
}

// GitHub #622 follow-up: with several lines selected the Arrowheads fields show mixed values
// and a field applies on its own: Width 300% gives each line 3 times its own weight and
// leaves the weights and heads alone.
void ui_shape_appearance_arrowheads_multiple_lines() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(line_layer(first, 10, false, true));
  doc.add_layer(line_layer(second, 4, true, true));
  select_batch_layers(window, {first, second});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("shapeArrowheadSelectionSummary")) != nullptr);
    check_heads_label(dialog);
    auto* weight = field<QDoubleSpinBox>(dialog, "shapeGeometryLineWeightSpin");
    auto* width = field<QSpinBox>(dialog, "shapeArrowWidthSpin");
    CHECK(weight->property("appearanceMixed").toBool());
    CHECK(field<QCheckBox>(dialog, "shapeGeometryLineArrowStartCheck")->property("appearanceMixed").toBool());
    CHECK(!width->property("appearanceMixed").toBool() && width->value() == 500 && width->isEnabled());
    width->setValue(300);
    CHECK(process_events_until([&] {
      return line_params(doc, first).arrow_width == 30 && line_params(doc, second).arrow_width == 12;
    }));
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(line_params(doc, first).arrow_width == 30 && line_params(doc, second).arrow_width == 12);
  CHECK(line_params(doc, first).line_weight == 10 && line_params(doc, second).line_weight == 4);
  CHECK(line_params(doc, first).arrow_length == 100 && line_params(doc, second).arrow_length == 40);
  CHECK(!line_params(doc, first).arrow_start && line_params(doc, second).arrow_start);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(line_params(doc, first).arrow_width == 50 && line_params(doc, second).arrow_width == 20);
}

Layer polygon_layer(LayerId id, double left, int sides, int inset) {
  Layer layer(id, "Polygon " + std::to_string(id), PixelBuffer());
  LiveShapeParams params;
  params.kind = LiveShapeKind::Polygon;
  params.left = left; params.top = 40; params.right = left + 100; params.bottom = 140;
  params.polygon_sides = sides; params.polygon_star_inset = inset;
  params.index = static_cast<int>(id);
  populate_live_shape_box_corners(params);
  VectorShapeContent content;
  content.path.subpaths = generate_live_shape_subpaths(params);
  content.origination.push_back(params);
  content.fill.kind = VectorFillKind::Solid; content.fill.color = {40, 90, 200};
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  update_vector_shape_raster(layer, Rect::from_size(600, 400), nullptr);
  return layer;
}

// A plain path with no live shape: a triangle stamped into a box, as the Custom Shape tool
// writes one (or the Pen draws one).
Layer path_layer(LayerId id, double left, double top, double width, double height) {
  Layer layer(id, "Custom Shape " + std::to_string(id), PixelBuffer());
  PathSubpath triangle;
  for (const auto& [x, y] : {std::pair{0.5, 0.0}, std::pair{1.0, 1.0}, std::pair{0.0, 1.0}})
    triangle.anchors.push_back({x, y, x, y, x, y, false});
  VectorShapeContent content;
  content.path.subpaths.push_back(triangle);
  transform_vector_path(content.path, {width, 0.0, 0.0, height, left, top});
  content.fill.kind = VectorFillKind::Solid; content.fill.color = {200, 60, 40};
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  update_vector_shape_raster(layer, Rect::from_size(600, 400), nullptr);
  return layer;
}

const VectorShapeContent& shape_content(const Document& doc, LayerId id) {
  return *std::as_const(doc).find_layer(id)->vector_shape();
}

bool box_is(const std::optional<VectorPathBounds>& bounds, double left, double top, double right, double bottom) {
  return bounds && std::abs(bounds->left - left) < 1e-6 && std::abs(bounds->top - top) < 1e-6 &&
         std::abs(bounds->right - right) < 1e-6 && std::abs(bounds->bottom - bottom) < 1e-6;
}

bool inside(const QWidget* widget, const char* group) {
  for (auto* parent = widget != nullptr ? widget->parentWidget() : nullptr; parent != nullptr;
       parent = parent->parentWidget())
    if (parent->objectName() == QLatin1String(group)) return true;
  return false;
}

int dialog_y(QDialog& dialog, const char* name) {
  auto* widget = dialog.findChild<QWidget*>(QLatin1String(name));
  CHECK(widget != nullptr);
  return widget != nullptr ? widget->mapTo(&dialog, QPoint(0, 0)).y() : 0;
}

// GitHub #642, #643: a polygon has Geometry (X, Y, Width, Height) and a Polygon section
// above Edge with Sides and Star inset; edits preview live, Cancel restores, OK is one
// undo step.
void ui_shape_appearance_polygon_geometry_and_sides() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = doc.allocate_layer_id();
  doc.add_layer(polygon_layer(id, 60, 5, 0));
  select_batch_layers(window, {id});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    CHECK(field<QDoubleSpinBox>(dialog, "shapeGeometryXSpin")->value() == 60);
    CHECK(field<QDoubleSpinBox>(dialog, "shapeGeometryWidthSpin")->value() == 100);
    CHECK(dialog.findChild<QWidget*>(QStringLiteral("shapeRadiusGroup")) == nullptr);
    auto* sides = field<QSpinBox>(dialog, "shapePolygonSidesSpin");
    auto* inset = field<QSpinBox>(dialog, "shapePolygonStarInsetSpin");
    CHECK(sides->value() == 5 && sides->minimum() == 3 && sides->maximum() == 100);
    CHECK(inset->value() == 0 && inset->minimum() == 0 && inset->maximum() == 99);
    CHECK(inside(sides, "shapePolygonGroup"));
    // Directly above Edge, under Geometry.
    CHECK(dialog_y(dialog, "shapePolygonGroup") > dialog_y(dialog, "shapeGeometryGroup"));
    CHECK(dialog_y(dialog, "shapePolygonGroup") < dialog_y(dialog, "shapeFeatherSpin"));
    sides->setValue(8);
    CHECK(process_events_until([&] { return shape_content(doc, id).origination.front().polygon_sides == 8; }));
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
  CHECK(shape_content(doc, id).origination.front().polygon_sides == 5);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);

  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    field<QDoubleSpinBox>(dialog, "shapeGeometryXSpin")->setValue(100);
    field<QDoubleSpinBox>(dialog, "shapeGeometryWidthSpin")->setValue(200);
    field<QSpinBox>(dialog, "shapePolygonSidesSpin")->setValue(7);
    field<QSpinBox>(dialog, "shapePolygonStarInsetSpin")->setValue(40);
    CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
    save_widget_artifact("ui_shape_appearance_polygon_section", dialog);
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  const auto& params = shape_content(doc, id).origination.front();
  CHECK(params.kind == LiveShapeKind::Polygon);
  CHECK(params.left == 100 && params.right == 300 && params.top == 40 && params.bottom == 140);
  CHECK(params.polygon_sides == 7 && params.polygon_star_inset == 40);
  // The outline is regenerated: a 7-point star has 14 corners.
  CHECK(shape_content(doc, id).path.subpaths.size() == 1);
  CHECK(shape_content(doc, id).path.subpaths.front().anchors.size() == 14);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(shape_content(doc, id).origination.front().polygon_sides == 5);
  CHECK(shape_content(doc, id).origination.front().left == 60);
}

// GitHub #643: with several polygons selected a Polygon field applies on its own: Star
// inset 30 reaches both and leaves each one's sides.
void ui_shape_appearance_polygons_multiple() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(polygon_layer(first, 40, 5, 0));
  doc.add_layer(polygon_layer(second, 300, 6, 10));
  select_batch_layers(window, {first, second});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* sides = field<QSpinBox>(dialog, "shapePolygonSidesSpin");
    auto* inset = field<QSpinBox>(dialog, "shapePolygonStarInsetSpin");
    CHECK(sides->property("appearanceMixed").toBool() && inset->property("appearanceMixed").toBool());
    inset->setValue(30);
    CHECK(process_events_until([&] {
      return shape_content(doc, first).origination.front().polygon_star_inset == 30 &&
             shape_content(doc, second).origination.front().polygon_star_inset == 30;
    }));
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(shape_content(doc, first).origination.front().polygon_sides == 5);
  CHECK(shape_content(doc, second).origination.front().polygon_sides == 6);
  CHECK(shape_content(doc, first).origination.front().polygon_star_inset == 30);
  CHECK(shape_content(doc, second).origination.front().polygon_star_inset == 30);
  CHECK(shape_content(doc, second).origination.front().left == 300);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(shape_content(doc, first).origination.front().polygon_star_inset == 0);
  CHECK(shape_content(doc, second).origination.front().polygon_star_inset == 10);
}

// GitHub #642: a plain path (custom shape, Pen outline) has Geometry: X, Y, Width, Height
// of its bounds; editing them moves and scales the path, one undo step. A layer that holds
// a modeled live shape among other paths gets none (moving it would drop the live shape).
void ui_shape_appearance_custom_path_geometry() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = doc.allocate_layer_id();
  doc.add_layer(path_layer(id, 50, 60, 100, 80));
  select_batch_layers(window, {id});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* x = field<QDoubleSpinBox>(dialog, "shapeGeometryXSpin");
    auto* width = field<QDoubleSpinBox>(dialog, "shapeGeometryWidthSpin");
    auto* height = field<QDoubleSpinBox>(dialog, "shapeGeometryHeightSpin");
    CHECK(x->value() == 50 && field<QDoubleSpinBox>(dialog, "shapeGeometryYSpin")->value() == 60);
    CHECK(width->value() == 100 && height->value() == 80);
    CHECK(inside(x, "shapeGeometryGroup"));
    // The proportion link works here too.
    field<QToolButton>(dialog, "shapeGeometryLinkButton")->setChecked(true);
    width->setValue(200);
    CHECK(height->value() == 160);
    x->setValue(120);
    CHECK(process_events_until([&] { return box_is(shape_content(doc, id).path.bounds(), 120, 60, 320, 220); }));
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(box_is(shape_content(doc, id).path.bounds(), 120, 60, 320, 220));
  // Still the same triangle: the apex in the middle of the top edge.
  const auto& apex = shape_content(doc, id).path.subpaths.front().anchors.front();
  CHECK(std::abs(apex.anchor_x - 220) < 1e-6 && std::abs(apex.anchor_y - 60) < 1e-6);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(box_is(shape_content(doc, id).path.bounds(), 50, 60, 150, 140));

  // A merged layer holding a live rectangle and a plain path: no box to move.
  auto merged = rectangle(doc.allocate_layer_id(), 300, 60, {}, {0, 0, 0});
  auto merged_content = *std::as_const(merged).vector_shape();
  const auto extra_layer = path_layer(merged.id(), 400, 200, 50, 50);
  auto extra = *extra_layer.vector_shape();
  extra.path.subpaths.front().shape_group = merged_content.path.next_shape_group();
  merged_content.path.subpaths.push_back(extra.path.subpaths.front());
  merged.set_vector_shape(merged_content);
  const auto settings = shape_appearance_settings(merged);
  CHECK(!settings.geometry && !settings.path_box);
}

// GitHub #644: a plain-path layer's Geometry ends with the Custom Shape tool's Shape picker,
// opening with no shape chosen. A pick replaces the outline with that shape fitted to the
// box shown (edits included), previews live, Cancel restores, OK is one undo step; with
// several layers selected each gets the shape in its own box.
void ui_shape_appearance_custom_shape_picker() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = doc.allocate_layer_id(), other = doc.allocate_layer_id();
  doc.add_layer(path_layer(id, 50, 60, 100, 80));
  doc.add_layer(path_layer(other, 300, 100, 60, 120));
  const auto original_path = shape_content(doc, id).path;
  select_batch_layers(window, {id});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  std::size_t picked_subpaths = 0;
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* combo = field<QComboBox>(dialog, "shapeGeometryCustomShapeCombo");
    CHECK(combo->currentIndex() == -1 && combo->count() > 0);
    CHECK(inside(combo, "shapeGeometryGroup"));
    CHECK(dialog_y(dialog, "shapeGeometryCustomShapeCombo") > dialog_y(dialog, "shapeGeometryHeightSpin"));
    combo->setCurrentIndex(0);
    CHECK(process_events_until([&] { return shape_content(doc, id).path != original_path; }));
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
  CHECK(shape_content(doc, id).path == original_path);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);

  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    field<QDoubleSpinBox>(dialog, "shapeGeometryWidthSpin")->setValue(150);
    auto* combo = field<QComboBox>(dialog, "shapeGeometryCustomShapeCombo");
    const auto& library = MainWindowTestAccess::custom_shape_library(window);
    const auto* entry = library.find_entry_by_shape_id(combo->itemData(1).toString());
    CHECK(entry != nullptr);
    picked_subpaths = entry != nullptr ? entry->path.subpaths.size() : 0;
    combo->setCurrentIndex(1);
    CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
    save_widget_artifact("ui_shape_appearance_custom_shape_picker", dialog);
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(shape_content(doc, id).path.subpaths.size() == picked_subpaths);
  CHECK(box_is(shape_content(doc, id).path.bounds(), 50, 60, 200, 140));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  MainWindowTestAccess::undo(window);
  CHECK(shape_content(doc, id).path == original_path);

  // Two plain-path layers: no single-layer box, the picker alone, each layer's own box.
  select_batch_layers(window, {id, other});
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    CHECK(dialog.findChild<QWidget*>(QStringLiteral("shapeGeometryXSpin")) == nullptr);
    auto* combo = field<QComboBox>(dialog, "shapeGeometryCustomShapeCombo");
    CHECK(combo->currentIndex() == -1);
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(shape_content(doc, id).path.subpaths.size() == picked_subpaths);
  CHECK(shape_content(doc, other).path.subpaths.size() == picked_subpaths);
  CHECK(box_is(shape_content(doc, id).path.bounds(), 50, 60, 150, 140));
  CHECK(box_is(shape_content(doc, other).path.bounds(), 300, 100, 360, 220));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
}

// GitHub #643 follow-up: a polygon drawn with the Polygon tool (not built from parameters)
// opens Shape Appearance with its Polygon section, not the plain-path Shape picker, and so
// does the same layer after a PSD save and reopen.
void ui_shape_appearance_polygon_drawn_with_tool() {
  VectorSettingsGuard guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(CanvasTool::Polygon);
  auto* sides = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  auto* inset = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(sides != nullptr && inset != nullptr);
  if (sides == nullptr || inset == nullptr) return;
  sides->setValue(6);
  inset->setValue(30);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
       canvas->widget_position_for_document_point(QPoint(250, 150)));
  QApplication::processEvents();
  const auto id = doc.active_layer_id();
  CHECK(id.has_value());
  if (!id) return;
  CHECK(shape_content(doc, *id).origination.size() == 1);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    CHECK(dialog.findChild<QWidget*>(QStringLiteral("shapePolygonGroup")) != nullptr);
    CHECK(dialog.findChild<QWidget*>(QStringLiteral("shapeGeometryCustomShapeCombo")) == nullptr);
    CHECK(field<QSpinBox>(dialog, "shapePolygonSidesSpin")->value() == 6);
    CHECK(field<QSpinBox>(dialog, "shapePolygonStarInsetSpin")->value() == 30);
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);

  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(doc), {});
  const Layer* reopened = nullptr;
  for (const auto& layer : reread.layers())
    if (layer.vector_shape() != nullptr) reopened = &layer;
  CHECK(reopened != nullptr);
  if (reopened == nullptr) return;
  const auto settings = shape_appearance_settings(*reopened);
  CHECK(appearance_has_editable_polygon(settings));
  CHECK(!settings.path_box.has_value());
  CHECK(settings.geometry && settings.geometry->polygon_sides == 6 && settings.geometry->polygon_star_inset == 30);
}

// GitHub #643 follow-up 2: philipzae's route. A fresh document, the Polygon tool on its
// default settings, a drag that turns the polygon while it grows, then a Ctrl+drag (the
// shape tool's temporary Path Select) that moves it: the polygon stays live and Shape
// Appearance still shows its Polygon section, at the moved position. A star moved with a
// nudge stays live too; a point edit still ends it.
void ui_shape_appearance_polygon_moved_after_drawing() {
  VectorSettingsGuard guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  {
    auto settings = app_settings();
    settings.remove(QStringLiteral("tools/polygonSides"));
    settings.remove(QStringLiteral("tools/polygonStarInset"));
    settings.sync();
  }
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(CanvasTool::Polygon);
  canvas->setFocus();
  const int sides = canvas->polygon_sides();
  // Center (200, 200); the cursor sweeps round while it moves out, turning the polygon.
  drag_document_path(*canvas, {QPoint(200, 200), QPoint(230, 215), QPoint(250, 240), QPoint(255, 265)}, 4);
  QApplication::processEvents();
  const auto id = doc.active_layer_id();
  CHECK(id.has_value());
  if (!id) return;
  const auto drawn = shape_appearance_settings(*std::as_const(doc).find_layer(*id));
  CHECK(appearance_has_editable_polygon(drawn));
  if (!drawn.geometry) return;

  // Ctrl+drag inside the polygon moves it by (30, -20).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 200)),
       canvas->widget_position_for_document_point(QPoint(230, 180)), Qt::ControlModifier);
  QApplication::processEvents();
  const auto& moved_content = shape_content(doc, *id);
  CHECK(std::abs(moved_content.path.subpaths.front().anchors.front().anchor_x -
                 generate_live_shape_subpaths(*drawn.geometry).front().anchors.front().anchor_x - 30.0) < 0.5);
  // Shape Appearance from the moved polygon: the Polygon section, no Shape picker.
  bool has_polygon_section = false, has_shape_picker = true;
  int dialog_sides = 0;
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    save_widget_artifact("ui_shape_appearance_polygon_moved_after_drawing", dialog);
    has_polygon_section = dialog.findChild<QWidget*>(QStringLiteral("shapePolygonGroup")) != nullptr;
    has_shape_picker = dialog.findChild<QWidget*>(QStringLiteral("shapeGeometryCustomShapeCombo")) != nullptr;
    if (auto* spin = dialog.findChild<QSpinBox*>(QStringLiteral("shapePolygonSidesSpin"))) dialog_sides = spin->value();
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(has_polygon_section);
  CHECK(!has_shape_picker);
  CHECK(dialog_sides == sides);
  const auto moved = shape_appearance_settings(*std::as_const(doc).find_layer(*id));
  CHECK(appearance_has_editable_polygon(moved));
  CHECK(!moved.path_box.has_value());
  CHECK(moved.geometry && std::abs(moved.geometry->left - drawn.geometry->left - 30.0) < 0.5 &&
        std::abs(moved.geometry->top - drawn.geometry->top + 20.0) < 0.5);

  // A star, moved with an arrow-key nudge under Path Select.
  auto* inset = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(inset != nullptr);
  if (inset == nullptr) return;
  inset->setValue(40);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(450, 220)),
       canvas->widget_position_for_document_point(QPoint(500, 250)));
  QApplication::processEvents();
  const auto star_id = doc.active_layer_id();
  CHECK(star_id.has_value() && star_id != id);
  if (!star_id || star_id == id) return;
  const auto star = shape_appearance_settings(*std::as_const(doc).find_layer(*star_id));
  CHECK(appearance_has_editable_polygon(star) && star.geometry->polygon_star_inset == 40);
  canvas->set_tool(CanvasTool::PathSelect);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(450, 220)),
       canvas->widget_position_for_document_point(QPoint(450, 220)));
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  send_key(*canvas, Qt::Key_Right);
  QApplication::processEvents();
  const auto nudged = shape_appearance_settings(*std::as_const(doc).find_layer(*star_id));
  CHECK(appearance_has_editable_polygon(nudged));
  CHECK(nudged.geometry && std::abs(nudged.geometry->left - star.geometry->left - 1.0) < 0.01 &&
        nudged.geometry->polygon_star_inset == 40);

  // A point edit (Direct Select drags one corner) ends the live polygon, as before.
  const auto corner = shape_content(doc, *star_id).path.subpaths.front().anchors.front();
  canvas->set_tool(CanvasTool::DirectSelect);
  drag(*canvas, canvas->widget_position_for_document_point(
                    QPoint(static_cast<int>(std::lround(corner.anchor_x)), static_cast<int>(std::lround(corner.anchor_y)))),
       canvas->widget_position_for_document_point(QPoint(static_cast<int>(std::lround(corner.anchor_x)) + 15,
                                                         static_cast<int>(std::lround(corner.anchor_y)) + 25)));
  QApplication::processEvents();
  CHECK(!appearance_has_editable_polygon(shape_appearance_settings(*std::as_const(doc).find_layer(*star_id))));
}

// GitHub #644 follow-up: a custom shape stamped with the Custom Shape tool opens Shape
// Appearance with that shape chosen in the picker; a pick there is shown next time (and undo
// brings back the stamped one); once the outline is no longer that shape (a point edit) the
// picker opens empty.
void ui_shape_appearance_picker_shows_stamped_shape() {
  VectorSettingsGuard guard;
  SettingsValueRestorer saved_shape(QStringLiteral("tools/customShapeId"));
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(CanvasTool::CustomShape);
  auto* tool_combo = window.findChild<QComboBox*>(QStringLiteral("customShapeCombo"));
  CHECK(tool_combo != nullptr);
  if (tool_combo == nullptr) return;
  const auto heart = QStringLiteral("shape.builtin.heart");
  CHECK(tool_combo->findData(heart) >= 0);
  tool_combo->setCurrentIndex(tool_combo->findData(heart));
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(220, 190)));
  QApplication::processEvents();
  const auto id = doc.active_layer_id();
  CHECK(id.has_value());
  if (!id) return;
  CHECK(shape_content(doc, *id).library_shape_id == heart.toStdString());

  QString picked;
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* combo = field<QComboBox>(dialog, "shapeGeometryCustomShapeCombo");
    CHECK(combo->currentData().toString() == heart);
    save_widget_artifact("ui_shape_appearance_picker_shows_stamped_shape", dialog);
    for (int index = 0; index < combo->count() && picked.isEmpty(); ++index)
      if (combo->itemData(index).toString() != heart) picked = combo->itemData(index).toString();
    combo->setCurrentIndex(combo->findData(picked));
    CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(shape_content(doc, *id).library_shape_id == picked.toStdString());
  const auto picker_shows = [&](const QString& expected) {
    when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
      auto* combo = field<QComboBox>(dialog, "shapeGeometryCustomShapeCombo");
      CHECK(expected.isEmpty() ? combo->currentIndex() == -1 : combo->currentData().toString() == expected);
      dialog.reject();
    });
    MainWindowTestAccess::edit_active_shape_appearance(window);
  };
  picker_shows(picked);
  MainWindowTestAccess::undo(window);
  CHECK(shape_content(doc, *id).library_shape_id == heart.toStdString());
  picker_shows(heart);

  // A point edit clears the id; an outline changed some other way no longer matches it.
  auto edited = shape_content(doc, *id);
  auto copy = edited;
  drop_live_shape_origination(copy, {copy.path.subpaths.front().shape_group});
  CHECK(copy.library_shape_id.empty());
  auto& anchor = edited.path.subpaths.front().anchors.front();
  anchor.anchor_x += 9.0;
  anchor.in_x += 9.0;
  anchor.out_x += 9.0;
  doc.find_layer(*id)->set_vector_shape(std::move(edited));
  picker_shows(QString());
}

// GitHub #641: a rectangle's four corner radii are their own Radius section under Geometry,
// labeled Top left: and so on, with their link; every left-column section starts its
// fields at the same x.
void ui_shape_appearance_radius_section_layout() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = doc.allocate_layer_id();
  doc.add_layer(rectangle(id, 40, 120, {6, 6, 6, 6}, {30, 120, 200}));
  select_batch_layers(window, {id});
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* radius_group = dialog.findChild<QGroupBox*>(QStringLiteral("shapeRadiusGroup"));
    CHECK(radius_group != nullptr);
    for (const char* name : {"shapeGeometryRadiusTopLeftSpin", "shapeGeometryRadiusTopRightSpin",
                             "shapeGeometryRadiusBottomRightSpin", "shapeGeometryRadiusBottomLeftSpin",
                             "shapeGeometryRadiusLinkButton"}) {
      auto* widget = field<QWidget>(dialog, name);
      CHECK(inside(widget, "shapeRadiusGroup") && !inside(widget, "shapeGeometryGroup"));
    }
    QStringList labels;
    for (auto* label : radius_group->findChildren<QLabel*>()) labels.append(label->text());
    for (const char* text : {"Top left:", "Top right:", "Bottom right:", "Bottom left:"})
      CHECK(labels.contains(QLatin1String(text)));
    CHECK(dialog_y(dialog, "shapeRadiusGroup") > dialog_y(dialog, "shapeGeometryGroup"));
    CHECK(field<QToolButton>(dialog, "shapeGeometryRadiusLinkButton")->isChecked());
    // One field column: Layer, Geometry, Radius and Edge fields start at the same x.
    const auto x = [&](const char* name) { return field<QWidget>(dialog, name)->mapTo(&dialog, QPoint(0, 0)).x(); };
    const int column = x("shapeGeometryXSpin");
    CHECK(x("shapeGeometryRadiusTopLeftSpin") == column);
    CHECK(x("shapeFeatherSpin") == column);
    CHECK(x("shapeLayerOpacitySlider") == column);
    // And the brackets share their column.
    CHECK(x("shapeGeometryLinkButton") == x("shapeGeometryRadiusLinkButton"));
    field<QDoubleSpinBox>(dialog, "shapeGeometryRadiusTopRightSpin")->setValue(14);
    CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));
    save_widget_artifact("ui_shape_appearance_radius_section", dialog);
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  for (const auto radius : shape_content(doc, id).origination.front().corner_radii) CHECK(radius == 14);
}

// GitHub #622 follow-up 2: Weight is a Geometry row after End Y for one line; with several
// lines selected a Geometry section holds Weight alone above Arrowheads, still applying to
// every line.
void ui_shape_appearance_line_weight_in_geometry() {
  VectorSettingsGuard guard;
  MainWindow window; show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto first = doc.allocate_layer_id(), second = doc.allocate_layer_id();
  doc.add_layer(line_layer(first, 10, false, true));
  doc.add_layer(line_layer(second, 4, true, true));
  select_batch_layers(window, {first});
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* weight = field<QDoubleSpinBox>(dialog, "shapeGeometryLineWeightSpin");
    CHECK(inside(weight, "shapeGeometryGroup") && !inside(weight, "shapeArrowheadsGroup"));
    CHECK(dialog_y(dialog, "shapeGeometryLineWeightSpin") > dialog_y(dialog, "shapeGeometryLineEndYSpin"));
    CHECK(dialog_y(dialog, "shapeArrowheadsGroup") > dialog_y(dialog, "shapeGeometryLineWeightSpin"));
    CHECK(field<QWidget>(dialog, "shapeGeometryLineStartXSpin")->mapTo(&dialog, QPoint(0, 0)).x() ==
          weight->mapTo(&dialog, QPoint(0, 0)).x());
    save_widget_artifact("ui_shape_appearance_line_weight_in_geometry", dialog);
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(process_events_until([&] { return !require_canvas(window)->processing_operation_active(); }));

  select_batch_layers(window, {first, second});
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* weight = field<QDoubleSpinBox>(dialog, "shapeGeometryLineWeightSpin");
    CHECK(inside(weight, "shapeGeometryGroup"));
    CHECK(dialog.findChild<QWidget*>(QStringLiteral("shapeGeometryLineStartXSpin")) == nullptr);
    CHECK(dialog_y(dialog, "shapeArrowheadsGroup") > dialog_y(dialog, "shapeGeometryGroup"));
    CHECK(weight->property("appearanceMixed").toBool());
    weight->setValue(6);
    CHECK(process_events_until([&] {
      return line_params(doc, first).line_weight == 6 && line_params(doc, second).line_weight == 6;
    }));
    save_widget_artifact("ui_shape_appearance_line_weight_multiple", dialog);
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  CHECK(line_params(doc, first).line_weight == 6 && line_params(doc, second).line_weight == 6);
  // Each line's heads keep their own proportions.
  CHECK(line_params(doc, first).arrow_width == 30 && line_params(doc, second).arrow_width == 30);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
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
    {"ui_line_options_bar_edits_selected_line", ui_line_options_bar_edits_selected_line},
    {"ui_shape_appearance_line_arrowheads", ui_shape_appearance_line_arrowheads},
    {"ui_shape_appearance_arrowhead_size", ui_shape_appearance_arrowhead_size},
    {"ui_shape_appearance_arrowheads_multiple_lines", ui_shape_appearance_arrowheads_multiple_lines},
    {"ui_shape_appearance_polygon_geometry_and_sides", ui_shape_appearance_polygon_geometry_and_sides},
    {"ui_shape_appearance_polygons_multiple", ui_shape_appearance_polygons_multiple},
    {"ui_shape_appearance_custom_path_geometry", ui_shape_appearance_custom_path_geometry},
    {"ui_shape_appearance_custom_shape_picker", ui_shape_appearance_custom_shape_picker},
    {"ui_shape_appearance_polygon_drawn_with_tool", ui_shape_appearance_polygon_drawn_with_tool},
    {"ui_shape_appearance_polygon_moved_after_drawing", ui_shape_appearance_polygon_moved_after_drawing},
    {"ui_shape_appearance_picker_shows_stamped_shape", ui_shape_appearance_picker_shows_stamped_shape},
    {"ui_shape_appearance_radius_section_layout", ui_shape_appearance_radius_section_layout},
    {"ui_shape_appearance_line_weight_in_geometry", ui_shape_appearance_line_weight_in_geometry},
  };
}
