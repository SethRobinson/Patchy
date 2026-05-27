#include "ui/layer_style_dialog.hpp"

#include "ui/blend_mode_ui.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace patchy::ui {

namespace {

LayerStyleGradient default_layer_style_gradient() {
  LayerStyleGradient gradient;
  gradient.angle_degrees = 90.0F;
  gradient.scale = 1.0F;
  gradient.color_stops.push_back(GradientColorStop{0.0F, RgbColor{255, 255, 255}});
  gradient.color_stops.push_back(GradientColorStop{1.0F, RgbColor{32, 32, 32}});
  gradient.alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
  gradient.alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  return gradient;
}

LayerDropShadow default_drop_shadow() {
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = RgbColor{0, 0, 0};
  shadow.opacity = 0.75F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 5.0F;
  shadow.spread = 0.0F;
  shadow.size = 5.0F;
  return shadow;
}

LayerOuterGlow default_outer_glow() {
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Screen;
  glow.color = RgbColor{255, 255, 190};
  glow.opacity = 0.75F;
  glow.spread = 0.0F;
  glow.size = 5.0F;
  return glow;
}

LayerColorOverlay default_color_overlay() {
  LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = BlendMode::Normal;
  overlay.color = RgbColor{255, 0, 0};
  overlay.opacity = 1.0F;
  return overlay;
}

LayerGradientFill default_gradient_fill() {
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient = default_layer_style_gradient();
  return fill;
}

LayerStroke default_stroke() {
  LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = BlendMode::Normal;
  stroke.color = RgbColor{0, 0, 0};
  stroke.opacity = 1.0F;
  stroke.size = 3.0F;
  stroke.position = LayerStrokePosition::Outside;
  return stroke;
}

LayerBevelEmboss default_bevel_emboss() {
  LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = BlendMode::Screen;
  bevel.highlight_color = RgbColor{255, 255, 255};
  bevel.highlight_opacity = 0.75F;
  bevel.shadow_blend_mode = BlendMode::Multiply;
  bevel.shadow_color = RgbColor{0, 0, 0};
  bevel.shadow_opacity = 0.75F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 1.0F;
  bevel.size = 5.0F;
  bevel.direction_up = true;
  return bevel;
}

QListWidgetItem* add_layer_style_category(QListWidget* list, const QString& text, bool checkable, bool checked) {
  auto* item = new QListWidgetItem(text, list);
  if (checkable) {
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
  } else {
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
  }
  return item;
}

void update_color_preview_label(QWidget* widget, int red, int green, int blue) {
  widget->setStyleSheet(QStringLiteral("%1 { background: rgb(%2, %3, %4); border: 1px solid #9aa4b2; }")
                           .arg(widget->metaObject()->className())
                           .arg(red)
                           .arg(green)
                           .arg(blue));
}

}  // namespace

std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer, std::function<void(const LayerStyleSettings&)> preview_changed) {
  auto style = layer.layer_style();
  auto shadow = style.drop_shadows.empty() ? default_drop_shadow() : style.drop_shadows.front();
  auto outer_glow = style.outer_glows.empty() ? default_outer_glow() : style.outer_glows.front();
  auto color_overlay = style.color_overlays.empty() ? default_color_overlay() : style.color_overlays.front();
  auto gradient = style.gradient_fills.empty() ? default_gradient_fill() : style.gradient_fills.front();
  auto stroke = style.strokes.empty() ? default_stroke() : style.strokes.front();
  auto bevel = style.bevels.empty() ? default_bevel_emboss() : style.bevels.front();
  if (gradient.gradient.color_stops.empty()) {
    gradient.gradient = default_layer_style_gradient();
  }
  if (stroke.uses_gradient && stroke.gradient.color_stops.empty()) {
    stroke.gradient = default_layer_style_gradient();
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyLayerStyleDialog"));
  dialog.resize(760, 520);
  auto* root = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Layer Style"));

  auto* name_row = new QHBoxLayout();
  name_row->addWidget(new QLabel(QObject::tr("Name:"), &dialog));
  auto* name = new QLineEdit(QString::fromStdString(layer.name()), &dialog);
  name->setObjectName(QStringLiteral("layerStyleLayerNameEdit"));
  name->setReadOnly(true);
  name_row->addWidget(name, 1);
  root->addLayout(name_row);

  auto* body = new QHBoxLayout();
  root->addLayout(body, 1);

  auto* categories = new QListWidget(&dialog);
  categories->setObjectName(QStringLiteral("layerStyleCategoryList"));
  categories->setMinimumWidth(210);
  categories->setMaximumWidth(230);
  add_layer_style_category(categories, QObject::tr("Blending Options"), false, true);
  auto* bevel_item = add_layer_style_category(categories, QObject::tr("Bevel & Emboss"), true,
                                             !style.bevels.empty() && style.bevels.front().enabled);
  auto* stroke_item =
      add_layer_style_category(categories, QObject::tr("Stroke"), true, !style.strokes.empty() && style.strokes.front().enabled);
  auto* color_overlay_item =
      add_layer_style_category(categories, QObject::tr("Color Overlay"), true,
                               !style.color_overlays.empty() && style.color_overlays.front().enabled);
  auto* gradient_item =
      add_layer_style_category(categories, QObject::tr("Gradient Overlay"), true,
                               !style.gradient_fills.empty() && style.gradient_fills.front().enabled);
  auto* outer_glow_item =
      add_layer_style_category(categories, QObject::tr("Outer Glow"), true,
                               !style.outer_glows.empty() && style.outer_glows.front().enabled);
  auto* shadow_item =
      add_layer_style_category(categories, QObject::tr("Drop Shadow"), true,
                               !style.drop_shadows.empty() && style.drop_shadows.front().enabled);
  auto install_category_checkbox = [&dialog, categories](QListWidgetItem* item, const QString& object_name) {
    auto* check = new QCheckBox(item->text(), categories);
    check->setObjectName(object_name);
    check->setChecked(item->checkState() == Qt::Checked);
    check->setMinimumHeight(26);
    check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    item->setSizeHint(QSize(0, 28));
    categories->setItemWidget(item, check);
    QObject::connect(check, &QCheckBox::toggled, &dialog, [categories, item](bool checked) {
      item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
      categories->setCurrentItem(item);
    });
    QObject::connect(categories, &QListWidget::itemChanged, &dialog, [item, check](QListWidgetItem* changed) {
      if (changed != item) {
        return;
      }
      QSignalBlocker blocker(check);
      check->setChecked(changed->checkState() == Qt::Checked);
    });
  };
  install_category_checkbox(bevel_item, QStringLiteral("layerStyleBevelEmbossCategoryCheck"));
  install_category_checkbox(stroke_item, QStringLiteral("layerStyleStrokeCategoryCheck"));
  install_category_checkbox(color_overlay_item, QStringLiteral("layerStyleColorOverlayCategoryCheck"));
  install_category_checkbox(gradient_item, QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
  install_category_checkbox(outer_glow_item, QStringLiteral("layerStyleOuterGlowCategoryCheck"));
  install_category_checkbox(shadow_item, QStringLiteral("layerStyleDropShadowCategoryCheck"));
  categories->setCurrentRow(0);
  body->addWidget(categories);

  auto* controls = new QStackedWidget(&dialog);
  controls->setObjectName(QStringLiteral("layerStyleOptionsStack"));
  body->addWidget(controls, 1);
  auto make_page = [controls](const QString& object_name) {
    auto* page = new QWidget(controls);
    page->setObjectName(object_name);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    controls->addWidget(page);
    return layout;
  };
  auto slider_object_name = [](QString spin_object_name) {
    if (spin_object_name.endsWith(QStringLiteral("Spin"))) {
      spin_object_name.chop(4);
    }
    return spin_object_name + QStringLiteral("Slider");
  };
  auto add_slider_spin_row = [&slider_object_name](QFormLayout* form, QWidget* parent, const QString& label,
                                                   const QString& spin_object_name, int minimum, int maximum,
                                                   int value, const QString& suffix = {}, int spin_width = 72) {
    auto* row = new QWidget(parent);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(slider_object_name(spin_object_name));
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    auto* spin = new QSpinBox(row);
    spin->setObjectName(spin_object_name);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    if (!suffix.isEmpty()) {
      spin->setSuffix(suffix);
    }
    configure_dialog_spinbox(spin, spin_width);
    row_layout->addWidget(slider, 1);
    row_layout->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    form->addRow(label, row);
    return spin;
  };
  auto add_color_slider_row = [&slider_object_name](QVBoxLayout* layout, QWidget* parent, const QString& label,
                                                    const QString& spin_object_name, std::uint8_t value) {
    auto* row = new QWidget(parent);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);
    auto* channel_label = new QLabel(label, row);
    channel_label->setFixedWidth(18);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(slider_object_name(spin_object_name));
    slider->setRange(0, 255);
    slider->setValue(value);
    auto* spin = new QSpinBox(row);
    spin->setObjectName(spin_object_name);
    spin->setRange(0, 255);
    spin->setValue(value);
    configure_dialog_spinbox(spin, 54);
    row_layout->addWidget(channel_label);
    row_layout->addWidget(slider, 1);
    row_layout->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    layout->addWidget(row);
    return spin;
  };

  auto* blending_layout = make_page(QStringLiteral("layerStyleBlendingPage"));
  auto* bevel_layout = make_page(QStringLiteral("layerStyleBevelEmbossPage"));
  auto* stroke_layout = make_page(QStringLiteral("layerStyleStrokePage"));
  auto* color_overlay_layout = make_page(QStringLiteral("layerStyleColorOverlayPage"));
  auto* gradient_layout = make_page(QStringLiteral("layerStyleGradientOverlayPage"));
  auto* outer_glow_layout = make_page(QStringLiteral("layerStyleOuterGlowPage"));
  auto* shadow_layout = make_page(QStringLiteral("layerStyleDropShadowPage"));

  auto* blending_group = new QGroupBox(QObject::tr("Blending Options"), controls);
  auto* blending_form = new QFormLayout(blending_group);
  auto* blend = new QComboBox(blending_group);
  blend->setObjectName(QStringLiteral("layerStyleBlendModeCombo"));
  add_blend_mode_items(blend);
  blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(layer.blend_mode()))));
  blending_form->addRow(QObject::tr("Blend Mode"), blend);
  auto* opacity = add_slider_spin_row(blending_form, blending_group, QObject::tr("Opacity"),
                                      QStringLiteral("layerStyleOpacitySpin"), 0, 100,
                                      static_cast<int>(std::round(layer.opacity() * 100.0F)), QStringLiteral("%"));
  blending_layout->addWidget(blending_group);

  auto* preview_check = new QCheckBox(QObject::tr("Preview"), controls);
  preview_check->setObjectName(QStringLiteral("layerStylePreviewCheck"));
  preview_check->setChecked(style.effects_visible);
  blending_layout->addWidget(preview_check);
  blending_layout->addStretch(1);

  auto* bevel_group = new QGroupBox(QObject::tr("Bevel & Emboss"), controls);
  auto* bevel_form = new QFormLayout(bevel_group);
  auto* bevel_size = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Size"),
                                         QStringLiteral("layerStyleBevelSizeSpin"), 1, 250,
                                         static_cast<int>(std::round(bevel.size)));
  auto* bevel_depth = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Depth"),
                                          QStringLiteral("layerStyleBevelDepthSpin"), 1, 1000,
                                          static_cast<int>(std::round(bevel.depth * 100.0F)), QStringLiteral("%"));
  auto* bevel_angle = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Angle"),
                                          QStringLiteral("layerStyleBevelAngleSpin"), -180, 180,
                                          static_cast<int>(std::round(bevel.angle_degrees)));
  auto* bevel_altitude = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Altitude"),
                                             QStringLiteral("layerStyleBevelAltitudeSpin"), 0, 90,
                                             static_cast<int>(std::round(bevel.altitude_degrees)));
  auto* bevel_direction = new QComboBox(bevel_group);
  bevel_direction->setObjectName(QStringLiteral("layerStyleBevelDirectionCombo"));
  bevel_direction->addItem(QObject::tr("Up"), true);
  bevel_direction->addItem(QObject::tr("Down"), false);
  bevel_direction->setCurrentIndex(bevel.direction_up ? 0 : 1);
  bevel_form->addRow(QObject::tr("Direction"), bevel_direction);
  auto* bevel_highlight_opacity =
      add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Highlight Opacity"),
                          QStringLiteral("layerStyleBevelHighlightOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.highlight_opacity * 100.0F)), QStringLiteral("%"));
  auto* bevel_shadow_opacity =
      add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Shadow Opacity"),
                          QStringLiteral("layerStyleBevelShadowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.shadow_opacity * 100.0F)), QStringLiteral("%"));
  bevel_layout->addWidget(bevel_group);
  bevel_layout->addStretch(1);

  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), controls);
  auto* stroke_form = new QFormLayout(stroke_group);
  auto* stroke_size = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Size"),
                                          QStringLiteral("layerStyleStrokeSizeSpin"), 1, 250,
                                          static_cast<int>(std::round(stroke.size)));
  auto* stroke_opacity = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleStrokeOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(stroke.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* stroke_color_row = new QWidget(stroke_group);
  auto* stroke_color_layout = new QVBoxLayout(stroke_color_row);
  stroke_color_layout->setContentsMargins(0, 0, 0, 0);
  stroke_color_layout->setSpacing(4);
  auto* stroke_red =
      add_color_slider_row(stroke_color_layout, stroke_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleStrokeRedSpin"), stroke.color.red);
  auto* stroke_green =
      add_color_slider_row(stroke_color_layout, stroke_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleStrokeGreenSpin"), stroke.color.green);
  auto* stroke_blue =
      add_color_slider_row(stroke_color_layout, stroke_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleStrokeBlueSpin"), stroke.color.blue);
  auto* stroke_preview_row = new QWidget(stroke_color_row);
  auto* stroke_preview_layout = new QHBoxLayout(stroke_preview_row);
  stroke_preview_layout->setContentsMargins(26, 0, 0, 0);
  stroke_preview_layout->setSpacing(8);
  auto* stroke_color_preview = new QPushButton(stroke_group);
  stroke_color_preview->setObjectName(QStringLiteral("layerStyleStrokeColorPreview"));
  stroke_color_preview->setFixedSize(28, 22);
  stroke_color_preview->setToolTip(QObject::tr("Choose Color..."));
  stroke_preview_layout->addWidget(stroke_color_preview);
  stroke_preview_layout->addStretch(1);
  stroke_color_layout->addWidget(stroke_preview_row);
  auto update_stroke_color_preview = [stroke_color_preview, stroke_red, stroke_green, stroke_blue] {
    update_color_preview_label(stroke_color_preview, stroke_red->value(), stroke_green->value(), stroke_blue->value());
  };
  update_stroke_color_preview();
  stroke_form->addRow(QObject::tr("Color RGB"), stroke_color_row);
  auto* stroke_position = new QComboBox(stroke_group);
  stroke_position->setObjectName(QStringLiteral("layerStyleStrokePositionCombo"));
  stroke_position->addItem(QObject::tr("Outside"), static_cast<int>(LayerStrokePosition::Outside));
  stroke_position->addItem(QObject::tr("Inside"), static_cast<int>(LayerStrokePosition::Inside));
  stroke_position->addItem(QObject::tr("Center"), static_cast<int>(LayerStrokePosition::Center));
  stroke_position->setCurrentIndex(std::max(0, stroke_position->findData(static_cast<int>(stroke.position))));
  stroke_form->addRow(QObject::tr("Position"), stroke_position);
  stroke_layout->addWidget(stroke_group);
  stroke_layout->addStretch(1);

  auto* color_overlay_group = new QGroupBox(QObject::tr("Color Overlay"), controls);
  auto* color_overlay_form = new QFormLayout(color_overlay_group);
  auto* color_overlay_blend = new QComboBox(color_overlay_group);
  color_overlay_blend->setObjectName(QStringLiteral("layerStyleColorOverlayBlendModeCombo"));
  add_blend_mode_items(color_overlay_blend);
  color_overlay_blend->setCurrentIndex(
      std::max(0, color_overlay_blend->findData(static_cast<int>(color_overlay.blend_mode))));
  color_overlay_form->addRow(QObject::tr("Blend Mode"), color_overlay_blend);
  auto* color_overlay_opacity =
      add_slider_spin_row(color_overlay_form, color_overlay_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleColorOverlayOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(color_overlay.opacity * 100.0F)), QStringLiteral("%"));
  auto* color_overlay_color_row = new QWidget(color_overlay_group);
  auto* color_overlay_color_layout = new QVBoxLayout(color_overlay_color_row);
  color_overlay_color_layout->setContentsMargins(0, 0, 0, 0);
  color_overlay_color_layout->setSpacing(4);
  auto* color_overlay_red =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleColorOverlayRedSpin"), color_overlay.color.red);
  auto* color_overlay_green =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleColorOverlayGreenSpin"), color_overlay.color.green);
  auto* color_overlay_blue =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleColorOverlayBlueSpin"), color_overlay.color.blue);
  auto* color_overlay_preview_row = new QWidget(color_overlay_color_row);
  auto* color_overlay_preview_layout = new QHBoxLayout(color_overlay_preview_row);
  color_overlay_preview_layout->setContentsMargins(26, 0, 0, 0);
  color_overlay_preview_layout->setSpacing(8);
  auto* color_overlay_color_preview = new QLabel(color_overlay_group);
  color_overlay_color_preview->setObjectName(QStringLiteral("layerStyleColorOverlayColorPreview"));
  color_overlay_color_preview->setFixedSize(34, 24);
  auto* color_overlay_pick_color = new QPushButton(QObject::tr("Choose Color..."), color_overlay_group);
  color_overlay_pick_color->setObjectName(QStringLiteral("layerStyleColorOverlayPickColorButton"));
  color_overlay_preview_layout->addWidget(color_overlay_color_preview);
  color_overlay_preview_layout->addWidget(color_overlay_pick_color);
  color_overlay_preview_layout->addStretch(1);
  color_overlay_color_layout->addWidget(color_overlay_preview_row);
  auto update_color_overlay_color_preview = [color_overlay_color_preview, color_overlay_red, color_overlay_green,
                                             color_overlay_blue] {
    update_color_preview_label(color_overlay_color_preview, color_overlay_red->value(), color_overlay_green->value(),
                               color_overlay_blue->value());
  };
  update_color_overlay_color_preview();
  color_overlay_form->addRow(QObject::tr("Color RGB"), color_overlay_color_row);
  color_overlay_layout->addWidget(color_overlay_group);
  color_overlay_layout->addStretch(1);

  auto* gradient_group = new QGroupBox(QObject::tr("Gradient Overlay"), controls);
  auto* gradient_form = new QFormLayout(gradient_group);
  auto* gradient_opacity =
      add_slider_spin_row(gradient_form, gradient_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleGradientOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(gradient.opacity * 100.0F)), QStringLiteral("%"));
  auto* gradient_angle = add_slider_spin_row(gradient_form, gradient_group, QObject::tr("Angle"),
                                             QStringLiteral("layerStyleGradientAngleSpin"), -180, 180,
                                             static_cast<int>(std::round(gradient.gradient.angle_degrees)));
  auto* gradient_scale = add_slider_spin_row(gradient_form, gradient_group, QObject::tr("Scale"),
                                             QStringLiteral("layerStyleGradientScaleSpin"), 1, 1000,
                                             static_cast<int>(std::round(gradient.gradient.scale * 100.0F)),
                                             QStringLiteral("%"));
  auto* gradient_preview = new QLabel(gradient_group);
  gradient_preview->setObjectName(QStringLiteral("layerStyleGradientPreview"));
  gradient_preview->setFixedHeight(28);
  gradient_preview->setMinimumWidth(260);
  gradient_form->addRow(QObject::tr("Preview"), gradient_preview);
  auto* gradient_stops = new QTableWidget(0, 4, gradient_group);
  gradient_stops->setObjectName(QStringLiteral("layerStyleGradientStopsTable"));
  gradient_stops->setHorizontalHeaderLabels(
      {QObject::tr("Location %"), QObject::tr("R"), QObject::tr("G"), QObject::tr("B")});
  gradient_stops->verticalHeader()->setVisible(false);
  gradient_stops->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  gradient_stops->setSelectionBehavior(QAbstractItemView::SelectRows);
  gradient_stops->setSelectionMode(QAbstractItemView::SingleSelection);
  gradient_stops->setMinimumHeight(128);
  auto gradient_stop_cell_value = [gradient_stops](int row, int column, int fallback) {
    const auto* item = gradient_stops->item(row, column);
    bool ok = false;
    const auto value = item == nullptr ? fallback : item->text().toInt(&ok);
    return ok ? value : fallback;
  };
  auto update_gradient_stop_row_color = [gradient_stops, &gradient_stop_cell_value](int row) {
    if (row < 0 || row >= gradient_stops->rowCount()) {
      return;
    }
    const auto red = std::clamp(gradient_stop_cell_value(row, 1, 255), 0, 255);
    const auto green = std::clamp(gradient_stop_cell_value(row, 2, 255), 0, 255);
    const auto blue = std::clamp(gradient_stop_cell_value(row, 3, 255), 0, 255);
    const QColor color(red, green, blue);
    const QColor text = red * 3 + green * 6 + blue > 1280 ? QColor(20, 24, 30) : QColor(245, 248, 252);
    for (int column = 1; column < gradient_stops->columnCount(); ++column) {
      auto* item = gradient_stops->item(row, column);
      if (item == nullptr) {
        continue;
      }
      item->setBackground(color);
      item->setForeground(text);
    }
  };
  auto* gradient_selected_row = new QWidget(gradient_group);
  auto* gradient_selected_layout = new QHBoxLayout(gradient_selected_row);
  gradient_selected_layout->setContentsMargins(0, 0, 0, 0);
  gradient_selected_layout->setSpacing(8);
  auto* gradient_selected_color_preview = new QLabel(gradient_group);
  gradient_selected_color_preview->setObjectName(QStringLiteral("layerStyleGradientSelectedColorPreview"));
  gradient_selected_color_preview->setFixedSize(34, 24);
  auto* pick_gradient_stop_color = new QPushButton(QObject::tr("Choose Color..."), gradient_group);
  pick_gradient_stop_color->setObjectName(QStringLiteral("layerStyleGradientPickColorButton"));
  gradient_selected_layout->addWidget(gradient_selected_color_preview);
  gradient_selected_layout->addWidget(pick_gradient_stop_color);
  gradient_selected_layout->addStretch(1);
  auto set_stop_item = [gradient_stops](int row, int column, int value) {
    auto* item = new QTableWidgetItem(QString::number(value));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gradient_stops->setItem(row, column, item);
  };
  auto add_gradient_stop_row = [gradient_stops, &set_stop_item, &update_gradient_stop_row_color](float location,
                                                                                                 RgbColor color) {
    const auto row = gradient_stops->rowCount();
    gradient_stops->insertRow(row);
    set_stop_item(row, 0, static_cast<int>(std::round(std::clamp(location, 0.0F, 1.0F) * 100.0F)));
    set_stop_item(row, 1, color.red);
    set_stop_item(row, 2, color.green);
    set_stop_item(row, 3, color.blue);
    update_gradient_stop_row_color(row);
  };
  for (const auto& stop : gradient.gradient.color_stops) {
    add_gradient_stop_row(stop.location, stop.color);
  }
  if (gradient_stops->rowCount() == 0) {
    const auto fallback = default_layer_style_gradient();
    for (const auto& stop : fallback.color_stops) {
      add_gradient_stop_row(stop.location, stop.color);
    }
  }
  auto update_gradient_stop_previews = [&] {
    const QSignalBlocker blocker(gradient_stops);
    std::vector<GradientColorStop> stops;
    stops.reserve(static_cast<std::size_t>(gradient_stops->rowCount()));
    for (int row = 0; row < gradient_stops->rowCount(); ++row) {
      update_gradient_stop_row_color(row);
      stops.push_back(GradientColorStop{
          std::clamp(static_cast<float>(gradient_stop_cell_value(row, 0, row == 0 ? 0 : 100)) / 100.0F, 0.0F, 1.0F),
          RgbColor{static_cast<std::uint8_t>(std::clamp(gradient_stop_cell_value(row, 1, 255), 0, 255)),
                   static_cast<std::uint8_t>(std::clamp(gradient_stop_cell_value(row, 2, 255), 0, 255)),
                   static_cast<std::uint8_t>(std::clamp(gradient_stop_cell_value(row, 3, 255), 0, 255))}});
    }
    if (stops.empty()) {
      stops = default_layer_style_gradient().color_stops;
    }
    std::sort(stops.begin(), stops.end(), [](const GradientColorStop& lhs, const GradientColorStop& rhs) {
      return lhs.location < rhs.location;
    });
    QStringList css_stops;
    for (const auto& stop : stops) {
      css_stops << QStringLiteral("stop:%1 rgb(%2, %3, %4)")
                       .arg(static_cast<double>(stop.location), 0, 'f', 3)
                       .arg(stop.color.red)
                       .arg(stop.color.green)
                       .arg(stop.color.blue);
    }
    gradient_preview->setStyleSheet(QStringLiteral(
        "QLabel { background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, %1); border: 1px solid #9aa4b2; }")
                                        .arg(css_stops.join(QStringLiteral(", "))));

    const auto selected_row = std::clamp(gradient_stops->currentRow(), 0, std::max(0, gradient_stops->rowCount() - 1));
    update_color_preview_label(gradient_selected_color_preview,
                               std::clamp(gradient_stop_cell_value(selected_row, 1, 255), 0, 255),
                               std::clamp(gradient_stop_cell_value(selected_row, 2, 255), 0, 255),
                               std::clamp(gradient_stop_cell_value(selected_row, 3, 255), 0, 255));
  };
  if (gradient_stops->rowCount() > 0) {
    gradient_stops->setCurrentCell(0, 0);
  }
  update_gradient_stop_previews();
  gradient_form->addRow(QObject::tr("Color Stops"), gradient_stops);
  gradient_form->addRow(QObject::tr("Selected Stop"), gradient_selected_row);
  auto* gradient_stop_buttons = new QWidget(gradient_group);
  auto* gradient_stop_button_layout = new QHBoxLayout(gradient_stop_buttons);
  gradient_stop_button_layout->setContentsMargins(0, 0, 0, 0);
  gradient_stop_button_layout->setSpacing(6);
  auto* add_gradient_stop = new QPushButton(QObject::tr("Add Stop"), gradient_stop_buttons);
  add_gradient_stop->setObjectName(QStringLiteral("layerStyleGradientAddStopButton"));
  auto* remove_gradient_stop = new QPushButton(QObject::tr("Remove Stop"), gradient_stop_buttons);
  remove_gradient_stop->setObjectName(QStringLiteral("layerStyleGradientRemoveStopButton"));
  gradient_stop_button_layout->addWidget(add_gradient_stop);
  gradient_stop_button_layout->addWidget(remove_gradient_stop);
  gradient_stop_button_layout->addStretch(1);
  gradient_form->addRow(QString(), gradient_stop_buttons);
  gradient_layout->addWidget(gradient_group);
  gradient_layout->addStretch(1);

  auto* outer_glow_group = new QGroupBox(QObject::tr("Outer Glow"), controls);
  auto* outer_glow_form = new QFormLayout(outer_glow_group);
  auto* outer_glow_blend = new QComboBox(outer_glow_group);
  outer_glow_blend->setObjectName(QStringLiteral("layerStyleOuterGlowBlendModeCombo"));
  add_blend_mode_items(outer_glow_blend);
  outer_glow_blend->setCurrentIndex(std::max(0, outer_glow_blend->findData(static_cast<int>(outer_glow.blend_mode))));
  outer_glow_form->addRow(QObject::tr("Blend Mode"), outer_glow_blend);
  auto* outer_glow_opacity =
      add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleOuterGlowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(outer_glow.opacity * 100.0F)), QStringLiteral("%"));
  auto* outer_glow_size = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Size"),
                                              QStringLiteral("layerStyleOuterGlowSizeSpin"), 0, 1000,
                                              static_cast<int>(std::round(outer_glow.size)));
  auto* outer_glow_spread = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Spread"),
                                                QStringLiteral("layerStyleOuterGlowSpreadSpin"), 0, 100,
                                                static_cast<int>(std::round(outer_glow.spread)),
                                                QStringLiteral("%"));
  auto* outer_glow_color_row = new QWidget(outer_glow_group);
  auto* outer_glow_color_layout = new QVBoxLayout(outer_glow_color_row);
  outer_glow_color_layout->setContentsMargins(0, 0, 0, 0);
  outer_glow_color_layout->setSpacing(4);
  auto* outer_glow_red =
      add_color_slider_row(outer_glow_color_layout, outer_glow_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleOuterGlowRedSpin"), outer_glow.color.red);
  auto* outer_glow_green =
      add_color_slider_row(outer_glow_color_layout, outer_glow_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleOuterGlowGreenSpin"), outer_glow.color.green);
  auto* outer_glow_blue =
      add_color_slider_row(outer_glow_color_layout, outer_glow_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleOuterGlowBlueSpin"), outer_glow.color.blue);
  auto* outer_glow_preview_row = new QWidget(outer_glow_color_row);
  auto* outer_glow_preview_layout = new QHBoxLayout(outer_glow_preview_row);
  outer_glow_preview_layout->setContentsMargins(26, 0, 0, 0);
  outer_glow_preview_layout->setSpacing(8);
  auto* outer_glow_color_preview = new QPushButton(outer_glow_group);
  outer_glow_color_preview->setObjectName(QStringLiteral("layerStyleOuterGlowColorPreview"));
  outer_glow_color_preview->setFixedSize(28, 22);
  outer_glow_color_preview->setToolTip(QObject::tr("Choose Color..."));
  outer_glow_preview_layout->addWidget(outer_glow_color_preview);
  outer_glow_preview_layout->addStretch(1);
  outer_glow_color_layout->addWidget(outer_glow_preview_row);
  auto update_outer_glow_color_preview = [outer_glow_color_preview, outer_glow_red, outer_glow_green,
                                          outer_glow_blue] {
    update_color_preview_label(outer_glow_color_preview, outer_glow_red->value(), outer_glow_green->value(),
                               outer_glow_blue->value());
  };
  update_outer_glow_color_preview();
  outer_glow_form->addRow(QObject::tr("Color RGB"), outer_glow_color_row);
  outer_glow_layout->addWidget(outer_glow_group);
  outer_glow_layout->addStretch(1);

  auto* shadow_group = new QGroupBox(QObject::tr("Drop Shadow"), controls);
  auto* shadow_form = new QFormLayout(shadow_group);
  auto* shadow_opacity = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleDropShadowOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(shadow.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* shadow_angle = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Angle"),
                                           QStringLiteral("layerStyleDropShadowAngleSpin"), -180, 180,
                                           static_cast<int>(std::round(shadow.angle_degrees)));
  auto* shadow_distance = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Distance"),
                                              QStringLiteral("layerStyleDropShadowDistanceSpin"), 0, 1000,
                                              static_cast<int>(std::round(shadow.distance)));
  auto* shadow_size = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Size"),
                                          QStringLiteral("layerStyleDropShadowSizeSpin"), 0, 1000,
                                          static_cast<int>(std::round(shadow.size)));
  auto* shadow_spread = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Spread"),
                                            QStringLiteral("layerStyleDropShadowSpreadSpin"), 0, 100,
                                            static_cast<int>(std::round(shadow.spread)), QStringLiteral("%"));
  shadow_layout->addWidget(shadow_group);
  shadow_layout->addStretch(1);

  QObject::connect(categories, &QListWidget::currentRowChanged, controls, &QStackedWidget::setCurrentIndex);
  controls->setCurrentIndex(categories->currentRow());

  auto build_current_settings = [&]() {
    LayerStyle result = style;
    result.effects_visible = preview_check->isChecked();
    const auto shadow_enabled = shadow_item->checkState() == Qt::Checked;
    const auto outer_glow_enabled = outer_glow_item->checkState() == Qt::Checked;
    const auto color_overlay_enabled = color_overlay_item->checkState() == Qt::Checked;
    const auto gradient_enabled = gradient_item->checkState() == Qt::Checked;
    const auto stroke_enabled = stroke_item->checkState() == Qt::Checked;
    const auto bevel_enabled = bevel_item->checkState() == Qt::Checked;

    if (bevel_enabled || !result.bevels.empty()) {
      if (result.bevels.empty()) {
        result.bevels.push_back(default_bevel_emboss());
      }
      auto& target = result.bevels.front();
      target.enabled = bevel_enabled;
      target.size = static_cast<float>(bevel_size->value());
      target.depth = static_cast<float>(bevel_depth->value()) / 100.0F;
      target.angle_degrees = static_cast<float>(bevel_angle->value());
      target.altitude_degrees = static_cast<float>(bevel_altitude->value());
      target.direction_up = bevel_direction->currentData().toBool();
      target.highlight_opacity = static_cast<float>(bevel_highlight_opacity->value()) / 100.0F;
      target.shadow_opacity = static_cast<float>(bevel_shadow_opacity->value()) / 100.0F;
    } else {
      result.bevels.clear();
    }

    if (outer_glow_enabled || !result.outer_glows.empty()) {
      if (result.outer_glows.empty()) {
        result.outer_glows.push_back(default_outer_glow());
      }
      auto& target = result.outer_glows.front();
      target.enabled = outer_glow_enabled;
      target.blend_mode = static_cast<BlendMode>(outer_glow_blend->currentData().toInt());
      target.opacity = static_cast<float>(outer_glow_opacity->value()) / 100.0F;
      target.size = static_cast<float>(outer_glow_size->value());
      target.spread = static_cast<float>(outer_glow_spread->value());
      target.color = RgbColor{static_cast<std::uint8_t>(outer_glow_red->value()),
                              static_cast<std::uint8_t>(outer_glow_green->value()),
                              static_cast<std::uint8_t>(outer_glow_blue->value())};
    } else {
      result.outer_glows.clear();
    }

    if (color_overlay_enabled || !result.color_overlays.empty()) {
      if (result.color_overlays.empty()) {
        result.color_overlays.push_back(default_color_overlay());
      }
      auto& target = result.color_overlays.front();
      target.enabled = color_overlay_enabled;
      target.blend_mode = static_cast<BlendMode>(color_overlay_blend->currentData().toInt());
      target.opacity = static_cast<float>(color_overlay_opacity->value()) / 100.0F;
      target.color = RgbColor{static_cast<std::uint8_t>(color_overlay_red->value()),
                              static_cast<std::uint8_t>(color_overlay_green->value()),
                              static_cast<std::uint8_t>(color_overlay_blue->value())};
    } else {
      result.color_overlays.clear();
    }

    if (shadow_enabled || !result.drop_shadows.empty()) {
      if (result.drop_shadows.empty()) {
        result.drop_shadows.push_back(default_drop_shadow());
      }
      auto& target = result.drop_shadows.front();
      target.enabled = shadow_enabled;
      target.opacity = static_cast<float>(shadow_opacity->value()) / 100.0F;
      target.angle_degrees = static_cast<float>(shadow_angle->value());
      target.distance = static_cast<float>(shadow_distance->value());
      target.size = static_cast<float>(shadow_size->value());
      target.spread = static_cast<float>(shadow_spread->value());
    } else {
      result.drop_shadows.clear();
    }

    if (gradient_enabled || !result.gradient_fills.empty()) {
      if (result.gradient_fills.empty()) {
        result.gradient_fills.push_back(default_gradient_fill());
      }
      auto& target = result.gradient_fills.front();
      target.enabled = gradient_enabled;
      target.opacity = static_cast<float>(gradient_opacity->value()) / 100.0F;
      if (target.gradient.color_stops.empty()) {
        target.gradient = default_layer_style_gradient();
      }
      target.gradient.angle_degrees = static_cast<float>(gradient_angle->value());
      target.gradient.scale = static_cast<float>(gradient_scale->value()) / 100.0F;
      target.gradient.color_stops.clear();
      for (int row = 0; row < gradient_stops->rowCount(); ++row) {
        auto cell_value = [gradient_stops, row](int column, int fallback) {
          const auto* item = gradient_stops->item(row, column);
          bool ok = false;
          const auto value = item == nullptr ? fallback : item->text().toInt(&ok);
          return ok ? value : fallback;
        };
        target.gradient.color_stops.push_back(GradientColorStop{
            std::clamp(static_cast<float>(cell_value(0, row == 0 ? 0 : 100)) / 100.0F, 0.0F, 1.0F),
            RgbColor{static_cast<std::uint8_t>(std::clamp(cell_value(1, 255), 0, 255)),
                     static_cast<std::uint8_t>(std::clamp(cell_value(2, 255), 0, 255)),
                     static_cast<std::uint8_t>(std::clamp(cell_value(3, 255), 0, 255))}});
      }
      if (target.gradient.color_stops.empty()) {
        target.gradient.color_stops = default_layer_style_gradient().color_stops;
      }
      std::sort(target.gradient.color_stops.begin(), target.gradient.color_stops.end(),
                [](const GradientColorStop& lhs, const GradientColorStop& rhs) {
                  return lhs.location < rhs.location;
                });
    } else {
      result.gradient_fills.clear();
    }

    if (stroke_enabled || !result.strokes.empty()) {
      if (result.strokes.empty()) {
        result.strokes.push_back(default_stroke());
      }
      auto& target = result.strokes.front();
      target.enabled = stroke_enabled;
      target.size = static_cast<float>(stroke_size->value());
      target.opacity = static_cast<float>(stroke_opacity->value()) / 100.0F;
      target.color = RgbColor{static_cast<std::uint8_t>(stroke_red->value()),
                              static_cast<std::uint8_t>(stroke_green->value()),
                              static_cast<std::uint8_t>(stroke_blue->value())};
      target.position = static_cast<LayerStrokePosition>(stroke_position->currentData().toInt());
    } else {
      result.strokes.clear();
    }

    return LayerStyleSettings{opacity->value(), static_cast<BlendMode>(blend->currentData().toInt()),
                              std::move(result)};
  };

  auto emit_preview = [&] {
    update_stroke_color_preview();
    update_color_overlay_color_preview();
    update_gradient_stop_previews();
    update_outer_glow_color_preview();
    if (preview_changed) {
      preview_changed(build_current_settings());
    }
  };
  QObject::connect(categories, &QListWidget::itemChanged, &dialog, [&emit_preview](QListWidgetItem*) { emit_preview(); });
  QObject::connect(blend, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(preview_check, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(); });
  for (auto* spin : {bevel_size, bevel_depth, bevel_angle, bevel_altitude, bevel_highlight_opacity,
                     bevel_shadow_opacity, stroke_size, stroke_opacity, stroke_red, stroke_green, stroke_blue,
                     color_overlay_opacity, color_overlay_red, color_overlay_green, color_overlay_blue,
                     gradient_opacity, gradient_angle, gradient_scale, outer_glow_opacity, outer_glow_size,
                     outer_glow_spread, outer_glow_red, outer_glow_green, outer_glow_blue, shadow_opacity,
                     shadow_angle, shadow_distance, shadow_size, shadow_spread}) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&emit_preview](int) { emit_preview(); });
  }
  QObject::connect(bevel_direction, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(color_overlay_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(); });
  QObject::connect(outer_glow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(); });
  QObject::connect(stroke_position, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(); });
  QObject::connect(gradient_stops, &QTableWidget::itemChanged, &dialog, [&emit_preview](QTableWidgetItem*) {
    emit_preview();
  });
  QObject::connect(gradient_stops, &QTableWidget::currentCellChanged, &dialog,
                   [&update_gradient_stop_previews](int, int, int, int) { update_gradient_stop_previews(); });
  QObject::connect(stroke_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen =
        request_patchy_color(&dialog, QColor(stroke_red->value(), stroke_green->value(), stroke_blue->value()),
                             QObject::tr("Choose Stroke Color"));
    if (!chosen.has_value()) {
      return;
    }
    stroke_red->setValue(chosen->red());
    stroke_green->setValue(chosen->green());
    stroke_blue->setValue(chosen->blue());
    emit_preview();
  });
  QObject::connect(color_overlay_pick_color, &QPushButton::clicked, &dialog, [&] {
    const auto chosen =
        request_patchy_color(&dialog, QColor(color_overlay_red->value(), color_overlay_green->value(), color_overlay_blue->value()),
                                QObject::tr("Choose Color Overlay Color"));
    if (!chosen.has_value()) {
      return;
    }
    color_overlay_red->setValue(chosen->red());
    color_overlay_green->setValue(chosen->green());
    color_overlay_blue->setValue(chosen->blue());
    emit_preview();
  });
  QObject::connect(pick_gradient_stop_color, &QPushButton::clicked, &dialog, [&] {
    if (gradient_stops->rowCount() <= 0) {
      return;
    }
    const auto row = std::clamp(gradient_stops->currentRow(), 0, gradient_stops->rowCount() - 1);
    const auto chosen =
        request_patchy_color(&dialog,
                                QColor(std::clamp(gradient_stop_cell_value(row, 1, 255), 0, 255),
                                       std::clamp(gradient_stop_cell_value(row, 2, 255), 0, 255),
                                       std::clamp(gradient_stop_cell_value(row, 3, 255), 0, 255)),
                                QObject::tr("Choose Gradient Stop Color"));
    if (!chosen.has_value()) {
      return;
    }
    gradient_stops->item(row, 1)->setText(QString::number(chosen->red()));
    gradient_stops->item(row, 2)->setText(QString::number(chosen->green()));
    gradient_stops->item(row, 3)->setText(QString::number(chosen->blue()));
    emit_preview();
  });
  QObject::connect(outer_glow_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen = request_patchy_color(
        &dialog, QColor(outer_glow_red->value(), outer_glow_green->value(), outer_glow_blue->value()),
        QObject::tr("Choose Outer Glow Color"));
    if (!chosen.has_value()) {
      return;
    }
    outer_glow_red->setValue(chosen->red());
    outer_glow_green->setValue(chosen->green());
    outer_glow_blue->setValue(chosen->blue());
    emit_preview();
  });
  QObject::connect(add_gradient_stop, &QPushButton::clicked, &dialog, [&] {
    const QSignalBlocker blocker(gradient_stops);
    const auto source_row = std::clamp(gradient_stops->currentRow(), 0, std::max(0, gradient_stops->rowCount() - 1));
    const auto location =
        std::clamp(gradient_stop_cell_value(source_row, 0, 50) + (gradient_stops->rowCount() > 0 ? 10 : 0), 0, 100);
    const auto red = gradient_stop_cell_value(source_row, 1, 255);
    const auto green = gradient_stop_cell_value(source_row, 2, 255);
    const auto blue = gradient_stop_cell_value(source_row, 3, 255);
    add_gradient_stop_row(static_cast<float>(location) / 100.0F,
                          RgbColor{static_cast<std::uint8_t>(std::clamp(red, 0, 255)),
                                   static_cast<std::uint8_t>(std::clamp(green, 0, 255)),
                                   static_cast<std::uint8_t>(std::clamp(blue, 0, 255))});
    gradient_stops->setCurrentCell(gradient_stops->rowCount() - 1, 0);
    emit_preview();
  });
  QObject::connect(remove_gradient_stop, &QPushButton::clicked, &dialog, [&] {
    if (gradient_stops->rowCount() <= 2) {
      return;
    }
    const QSignalBlocker blocker(gradient_stops);
    const auto row = std::clamp(gradient_stops->currentRow(), 0, gradient_stops->rowCount() - 1);
    gradient_stops->removeRow(row);
    gradient_stops->setCurrentCell(std::min(row, gradient_stops->rowCount() - 1), 0);
    emit_preview();
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  return build_current_settings();
}


}  // namespace patchy::ui
