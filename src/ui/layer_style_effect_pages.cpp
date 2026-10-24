#include "ui/layer_style_effect_pages.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_qss.hpp"
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>
namespace patchy::ui::layer_style_detail {
namespace {
// Photoshop's Drop/Inner Shadow Distance maximum.
constexpr int kShadowDistanceMaximum = 30000;
// Forwards a left double-click on a passive color patch to its page's Choose
// Color button, so the swatch itself opens the picker (Photoshop behavior).
// Parented to the watched widget; the button is tracked by QPointer in case it
// dies first.
class DoubleClickClicksButton : public QObject {
 public:
  DoubleClickClicksButton(QAbstractButton* button, QWidget* watched)
      : QObject(watched), button_(button) {
    watched->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonDblClick &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && button_ != nullptr) {
      button_->click();
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QPointer<QAbstractButton> button_;
};

}

BevelPageControls build_bevel_page(LayerStylePageContext& context, QVBoxLayout* bevel_layout, const LayerBevelEmboss& bevel, const StyleContour& custom_gloss_contour) {
  auto& controls = context.controls;
  auto& make_contour_combo = context.make_contour_combo;
  auto* bevel_group = new QGroupBox(QObject::tr("Bevel & Emboss"), controls);
  auto* bevel_form = new QFormLayout(bevel_group);
  auto* bevel_style = new QComboBox(bevel_group);
  bevel_style->setObjectName(QStringLiteral("layerStyleBevelStyleCombo"));
  bevel_style->addItem(QObject::tr("Inner Bevel"), static_cast<int>(BevelEmbossStyleKind::InnerBevel));
  bevel_style->addItem(QObject::tr("Outer Bevel"), static_cast<int>(BevelEmbossStyleKind::OuterBevel));
  bevel_style->addItem(QObject::tr("Emboss"), static_cast<int>(BevelEmbossStyleKind::Emboss));
  bevel_style->addItem(QObject::tr("Pillow Emboss"), static_cast<int>(BevelEmbossStyleKind::PillowEmboss));
  bevel_style->addItem(QObject::tr("Stroke Emboss"), static_cast<int>(BevelEmbossStyleKind::StrokeEmboss));
  bevel_style->setCurrentIndex(std::max(0, bevel_style->findData(static_cast<int>(bevel.style))));
  bevel_form->addRow(QObject::tr("Style"), bevel_style);
  auto* bevel_technique = new QComboBox(bevel_group);
  bevel_technique->setObjectName(QStringLiteral("layerStyleBevelTechniqueCombo"));
  bevel_technique->addItem(QObject::tr("Smooth"), static_cast<int>(BevelTechnique::Smooth));
  bevel_technique->addItem(QObject::tr("Chisel Hard"), static_cast<int>(BevelTechnique::ChiselHard));
  bevel_technique->addItem(QObject::tr("Chisel Soft"), static_cast<int>(BevelTechnique::ChiselSoft));
  bevel_technique->setCurrentIndex(
      std::max(0, bevel_technique->findData(static_cast<int>(bevel.technique))));
  bevel_form->addRow(QObject::tr("Technique"), bevel_technique);
  auto* bevel_size = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Size"),
                                         QStringLiteral("layerStyleBevelSizeSpin"), 1, 250,
                                         static_cast<int>(std::round(bevel.size)));
  auto* bevel_soften = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Soften"),
                                           QStringLiteral("layerStyleBevelSoftenSpin"), 0, 16,
                                           static_cast<int>(std::round(bevel.soften)));
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
  auto* bevel_gloss_row = new QWidget(bevel_group);
  auto* bevel_gloss_layout = new QHBoxLayout(bevel_gloss_row);
  bevel_gloss_layout->setContentsMargins(0, 0, 0, 0);
  bevel_gloss_layout->setSpacing(8);
  auto* bevel_gloss_contour = make_contour_combo(bevel_gloss_row,
                                                 QStringLiteral("layerStyleBevelGlossContourCombo"),
                                                 bevel.gloss_contour, custom_gloss_contour);
  bevel_gloss_layout->addWidget(bevel_gloss_contour, 1);
  auto* bevel_gloss_anti_aliased = new QCheckBox(QObject::tr("Anti-aliased"), bevel_gloss_row);
  bevel_gloss_anti_aliased->setObjectName(QStringLiteral("layerStyleBevelGlossAntiAliasedCheck"));
  bevel_gloss_anti_aliased->setChecked(bevel.gloss_anti_aliased);
  bevel_gloss_layout->addWidget(bevel_gloss_anti_aliased);
  bevel_form->addRow(QObject::tr("Gloss Contour"), bevel_gloss_row);

  auto bevel_highlight_color = bevel.highlight_color;
  auto* bevel_highlight_group = new QGroupBox(QObject::tr("Highlight"), bevel_group);
  auto* bevel_highlight_form = new QFormLayout(bevel_highlight_group);
  auto* bevel_highlight_blend = new QComboBox(bevel_highlight_group);
  bevel_highlight_blend->setObjectName(QStringLiteral("layerStyleBevelHighlightBlendModeCombo"));
  add_blend_mode_items(bevel_highlight_blend, BlendModeMenu::Effect);
  bevel_highlight_blend->setCurrentIndex(
      std::max(0, bevel_highlight_blend->findData(static_cast<int>(bevel.highlight_blend_mode))));
  bevel_highlight_form->addRow(QObject::tr("Blend Mode"), bevel_highlight_blend);
  auto* bevel_highlight_opacity =
      add_slider_spin_row(bevel_highlight_form, bevel_highlight_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleBevelHighlightOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.highlight_opacity * 100.0F)), QStringLiteral("%"));
  auto* bevel_highlight_color_button = new QPushButton(bevel_highlight_group);
  bevel_highlight_color_button->setObjectName(QStringLiteral("layerStyleBevelHighlightColorButton"));
  bevel_highlight_color_button->setFixedSize(34, 24);
  bevel_highlight_color_button->setToolTip(QObject::tr("Choose Color..."));
  bevel_highlight_form->addRow(QObject::tr("Color"), bevel_highlight_color_button);
  bevel_form->addRow(bevel_highlight_group);

  auto bevel_shadow_color = bevel.shadow_color;
  auto* bevel_shadow_group = new QGroupBox(QObject::tr("Shadow"), bevel_group);
  auto* bevel_shadow_form = new QFormLayout(bevel_shadow_group);
  auto* bevel_shadow_blend = new QComboBox(bevel_shadow_group);
  bevel_shadow_blend->setObjectName(QStringLiteral("layerStyleBevelShadowBlendModeCombo"));
  add_blend_mode_items(bevel_shadow_blend, BlendModeMenu::Effect);
  bevel_shadow_blend->setCurrentIndex(
      std::max(0, bevel_shadow_blend->findData(static_cast<int>(bevel.shadow_blend_mode))));
  bevel_shadow_form->addRow(QObject::tr("Blend Mode"), bevel_shadow_blend);
  auto* bevel_shadow_opacity =
      add_slider_spin_row(bevel_shadow_form, bevel_shadow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleBevelShadowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.shadow_opacity * 100.0F)), QStringLiteral("%"));
  auto* bevel_shadow_color_button = new QPushButton(bevel_shadow_group);
  bevel_shadow_color_button->setObjectName(QStringLiteral("layerStyleBevelShadowColorButton"));
  bevel_shadow_color_button->setFixedSize(34, 24);
  bevel_shadow_color_button->setToolTip(QObject::tr("Choose Color..."));
  bevel_shadow_form->addRow(QObject::tr("Color"), bevel_shadow_color_button);
  bevel_form->addRow(bevel_shadow_group);
  bevel_layout->addWidget(bevel_group);
  bevel_layout->addStretch(1);

  return {bevel_style, bevel_technique, bevel_size, bevel_soften, bevel_depth, bevel_angle, bevel_altitude, bevel_direction, bevel_gloss_contour, bevel_gloss_anti_aliased, bevel_highlight_color, bevel_highlight_blend, bevel_highlight_opacity, bevel_highlight_color_button, bevel_shadow_color, bevel_shadow_blend, bevel_shadow_opacity, bevel_shadow_color_button};
}

BevelContourPageControls build_bevel_contour_page(LayerStylePageContext& context, QVBoxLayout* bevel_contour_layout, const LayerBevelEmboss& bevel, const StyleContour& custom_bevel_sub_contour) {
  auto& controls = context.controls;
  auto& make_contour_combo = context.make_contour_combo;
  auto* bevel_contour_group = new QGroupBox(QObject::tr("Contour"), controls);
  auto* bevel_contour_form = new QFormLayout(bevel_contour_group);
  auto* bevel_contour_row = new QWidget(bevel_contour_group);
  auto* bevel_contour_row_layout = new QHBoxLayout(bevel_contour_row);
  bevel_contour_row_layout->setContentsMargins(0, 0, 0, 0);
  bevel_contour_row_layout->setSpacing(8);
  auto* bevel_contour_combo = make_contour_combo(bevel_contour_row,
                                                 QStringLiteral("layerStyleBevelContourCombo"),
                                                 bevel.contour.contour, custom_bevel_sub_contour);
  bevel_contour_row_layout->addWidget(bevel_contour_combo, 1);
  auto* bevel_contour_anti_aliased = new QCheckBox(QObject::tr("Anti-aliased"), bevel_contour_row);
  bevel_contour_anti_aliased->setObjectName(QStringLiteral("layerStyleBevelContourAntiAliasedCheck"));
  bevel_contour_anti_aliased->setChecked(bevel.contour.anti_aliased);
  bevel_contour_row_layout->addWidget(bevel_contour_anti_aliased);
  bevel_contour_form->addRow(QObject::tr("Contour"), bevel_contour_row);
  auto* bevel_contour_range = add_slider_spin_row(
      bevel_contour_form, bevel_contour_group, QObject::tr("Range"),
      QStringLiteral("layerStyleBevelContourRangeSpin"), 1, 100,
      static_cast<int>(std::round(bevel.contour.range * 100.0F)), QStringLiteral("%"));
  bevel_contour_layout->addWidget(bevel_contour_group);
  bevel_contour_layout->addStretch(1);

  return {bevel_contour_combo, bevel_contour_anti_aliased, bevel_contour_range};
}

BevelTexturePageControls build_bevel_texture_page(LayerStylePageContext& context, QVBoxLayout* bevel_texture_layout, const LayerBevelEmboss& bevel) {
  auto& controls = context.controls;
  auto& make_pattern_combo = context.make_pattern_combo;
  auto* bevel_texture_group = new QGroupBox(QObject::tr("Texture"), controls);
  auto* bevel_texture_form = new QFormLayout(bevel_texture_group);
  auto* bevel_texture_pattern_row = new QWidget(bevel_texture_group);
  auto* bevel_texture_pattern_layout = new QHBoxLayout(bevel_texture_pattern_row);
  bevel_texture_pattern_layout->setContentsMargins(0, 0, 0, 0);
  bevel_texture_pattern_layout->setSpacing(8);
  auto* bevel_texture_pattern = make_pattern_combo(bevel_texture_pattern_row,
                                                   QStringLiteral("layerStyleBevelTexturePatternCombo"),
                                                   bevel.texture.pattern_id, bevel.texture.pattern_name);
  bevel_texture_pattern_layout->addWidget(bevel_texture_pattern, 1);
  auto* bevel_texture_manage = new QPushButton(QObject::tr("Manage…"), bevel_texture_pattern_row);
  bevel_texture_manage->setObjectName(QStringLiteral("layerStyleBevelTextureManageButton"));
  bevel_texture_manage->setToolTip(QObject::tr("Open Pattern Manager"));
  bevel_texture_pattern_layout->addWidget(bevel_texture_manage);
  bevel_texture_form->addRow(QObject::tr("Pattern"), bevel_texture_pattern_row);
  auto* bevel_texture_scale = add_slider_spin_row(
      bevel_texture_form, bevel_texture_group, QObject::tr("Scale"),
      QStringLiteral("layerStyleBevelTextureScaleSpin"), 1, 1000,
      static_cast<int>(std::round(bevel.texture.scale * 100.0F)), QStringLiteral("%"));
  auto* bevel_texture_depth = add_slider_spin_row(
      bevel_texture_form, bevel_texture_group, QObject::tr("Depth"),
      QStringLiteral("layerStyleBevelTextureDepthSpin"), -1000, 1000,
      static_cast<int>(std::round(bevel.texture.depth * 100.0F)), QStringLiteral("%"));
  auto* bevel_texture_invert = new QCheckBox(QObject::tr("Invert"), bevel_texture_group);
  bevel_texture_invert->setObjectName(QStringLiteral("layerStyleBevelTextureInvertCheck"));
  bevel_texture_invert->setChecked(bevel.texture.invert);
  bevel_texture_form->addRow(QString(), bevel_texture_invert);
  auto* bevel_texture_link = new QCheckBox(QObject::tr("Link with Layer"), bevel_texture_group);
  bevel_texture_link->setObjectName(QStringLiteral("layerStyleBevelTextureLinkCheck"));
  bevel_texture_link->setChecked(bevel.texture.link_with_layer);
  bevel_texture_link->setToolTip(
      QObject::tr("Anchor the pattern to the layer so it follows when the layer moves"));
  bevel_texture_form->addRow(QString(), bevel_texture_link);
  auto* bevel_texture_snap = new QPushButton(QObject::tr("Snap to Origin"), bevel_texture_group);
  bevel_texture_snap->setObjectName(QStringLiteral("layerStyleBevelTextureSnapOriginButton"));
  bevel_texture_form->addRow(QString(), bevel_texture_snap);
  bevel_texture_layout->addWidget(bevel_texture_group);
  bevel_texture_layout->addStretch(1);

  return {bevel_texture_pattern, bevel_texture_manage, bevel_texture_scale, bevel_texture_depth, bevel_texture_invert, bevel_texture_link, bevel_texture_snap};
}

PatternOverlayPageControls build_pattern_overlay_page(LayerStylePageContext& context, QVBoxLayout* pattern_overlay_layout, const LayerPatternOverlay& pattern_overlay) {
  auto& controls = context.controls;
  auto& make_pattern_combo = context.make_pattern_combo;
  auto* pattern_overlay_group = new QGroupBox(QObject::tr("Pattern Overlay"), controls);
  auto* pattern_overlay_form = new QFormLayout(pattern_overlay_group);
  auto* pattern_overlay_blend = new QComboBox(pattern_overlay_group);
  pattern_overlay_blend->setObjectName(QStringLiteral("layerStylePatternOverlayBlendModeCombo"));
  add_blend_mode_items(pattern_overlay_blend, BlendModeMenu::Effect);
  pattern_overlay_blend->setCurrentIndex(
      std::max(0, pattern_overlay_blend->findData(static_cast<int>(pattern_overlay.blend_mode))));
  pattern_overlay_form->addRow(QObject::tr("Blend Mode"), pattern_overlay_blend);
  auto* pattern_overlay_opacity = add_slider_spin_row(
      pattern_overlay_form, pattern_overlay_group, QObject::tr("Opacity"),
      QStringLiteral("layerStylePatternOverlayOpacitySpin"), 0, 100,
      static_cast<int>(std::round(pattern_overlay.opacity * 100.0F)), QStringLiteral("%"));
  auto* pattern_overlay_pattern_row = new QWidget(pattern_overlay_group);
  auto* pattern_overlay_pattern_layout = new QHBoxLayout(pattern_overlay_pattern_row);
  pattern_overlay_pattern_layout->setContentsMargins(0, 0, 0, 0);
  pattern_overlay_pattern_layout->setSpacing(8);
  auto* pattern_overlay_pattern = make_pattern_combo(
      pattern_overlay_pattern_row, QStringLiteral("layerStylePatternOverlayPatternCombo"),
      pattern_overlay.pattern_id, pattern_overlay.pattern_name);
  pattern_overlay_pattern_layout->addWidget(pattern_overlay_pattern, 1);
  auto* pattern_overlay_manage = new QPushButton(QObject::tr("Manage…"), pattern_overlay_pattern_row);
  pattern_overlay_manage->setObjectName(QStringLiteral("layerStylePatternOverlayManageButton"));
  pattern_overlay_manage->setToolTip(QObject::tr("Open Pattern Manager"));
  pattern_overlay_pattern_layout->addWidget(pattern_overlay_manage);
  pattern_overlay_form->addRow(QObject::tr("Pattern"), pattern_overlay_pattern_row);
  auto* pattern_overlay_angle = add_slider_spin_row(
      pattern_overlay_form, pattern_overlay_group, QObject::tr("Angle"),
      QStringLiteral("layerStylePatternOverlayAngleSpin"), -180, 180,
      static_cast<int>(std::round(pattern_overlay.angle_degrees)));
  auto* pattern_overlay_scale = add_slider_spin_row(
      pattern_overlay_form, pattern_overlay_group, QObject::tr("Scale"),
      QStringLiteral("layerStylePatternOverlayScaleSpin"), 1, 1000,
      static_cast<int>(std::round(pattern_overlay.scale * 100.0F)), QStringLiteral("%"));
  auto* pattern_overlay_link = new QCheckBox(QObject::tr("Link with Layer"), pattern_overlay_group);
  pattern_overlay_link->setObjectName(QStringLiteral("layerStylePatternOverlayLinkCheck"));
  pattern_overlay_link->setChecked(pattern_overlay.link_with_layer);
  pattern_overlay_link->setToolTip(
      QObject::tr("Anchor the pattern to the layer so it follows when the layer moves"));
  pattern_overlay_form->addRow(QString(), pattern_overlay_link);
  auto* pattern_overlay_snap = new QPushButton(QObject::tr("Snap to Origin"), pattern_overlay_group);
  pattern_overlay_snap->setObjectName(QStringLiteral("layerStylePatternOverlaySnapOriginButton"));
  pattern_overlay_form->addRow(QString(), pattern_overlay_snap);
  pattern_overlay_layout->addWidget(pattern_overlay_group);
  pattern_overlay_layout->addStretch(1);

  return {pattern_overlay_blend, pattern_overlay_opacity, pattern_overlay_pattern, pattern_overlay_manage, pattern_overlay_angle, pattern_overlay_scale, pattern_overlay_link, pattern_overlay_snap};
}

StrokePageControls build_stroke_page(LayerStylePageContext& context, QVBoxLayout* stroke_layout, const LayerStroke& stroke) {
  auto& dialog = context.dialog;
  auto& controls = context.controls;
  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), controls);
  auto* stroke_form = new QFormLayout(stroke_group);
  auto* stroke_size = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Size"),
                                          QStringLiteral("layerStyleStrokeSizeSpin"), 1, 250,
                                          static_cast<int>(std::round(stroke.size)));
  auto* stroke_position = new QComboBox(stroke_group);
  stroke_position->setObjectName(QStringLiteral("layerStyleStrokePositionCombo"));
  stroke_position->addItem(QObject::tr("Outside"), static_cast<int>(LayerStrokePosition::Outside));
  stroke_position->addItem(QObject::tr("Inside"), static_cast<int>(LayerStrokePosition::Inside));
  stroke_position->addItem(QObject::tr("Center"), static_cast<int>(LayerStrokePosition::Center));
  stroke_position->setCurrentIndex(std::max(0, stroke_position->findData(static_cast<int>(stroke.position))));
  stroke_form->addRow(QObject::tr("Position"), stroke_position);
  auto* stroke_blend = new QComboBox(stroke_group);
  stroke_blend->setObjectName(QStringLiteral("layerStyleStrokeBlendModeCombo"));
  add_blend_mode_items(stroke_blend, BlendModeMenu::Effect);
  stroke_blend->setCurrentIndex(std::max(0, stroke_blend->findData(static_cast<int>(stroke.blend_mode))));
  stroke_form->addRow(QObject::tr("Blend Mode"), stroke_blend);
  auto* stroke_opacity = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleStrokeOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(stroke.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* stroke_overprint = new QCheckBox(QObject::tr("Overprint"), stroke_group);
  stroke_overprint->setObjectName(QStringLiteral("layerStyleStrokeOverprintCheck"));
  stroke_overprint->setChecked(stroke.overprint);
  stroke_overprint->setToolTip(QObject::tr(
      "Blend the stroke against the layer's own content. When off, the stroke knocks the content out of "
      "its band and blends with the layers below, like Photoshop"));
  stroke_form->addRow(QString(), stroke_overprint);
  auto* stroke_fill = new QComboBox(stroke_group);
  stroke_fill->setObjectName(QStringLiteral("layerStyleStrokeFillCombo"));
  stroke_fill->addItem(QObject::tr("Color"), false);
  stroke_fill->addItem(QObject::tr("Gradient"), true);
  stroke_fill->setCurrentIndex(stroke.uses_gradient ? 1 : 0);
  stroke_form->addRow(QObject::tr("Fill"), stroke_fill);
  const auto stroke_rows =
      make_color_rows(stroke_form, stroke_group, QStringLiteral("layerStyleStroke"), stroke.color);
  auto* stroke_red = stroke_rows.red;
  auto* stroke_green = stroke_rows.green;
  auto* stroke_blue = stroke_rows.blue;
  auto* stroke_gradient_group = new QGroupBox(QObject::tr("Gradient"), stroke_group);
  stroke_gradient_group->setObjectName(QStringLiteral("layerStyleStrokeGradientGroup"));
  auto* stroke_gradient_form = new QFormLayout(stroke_gradient_group);
  auto stroke_gradient_controls =
      make_gradient_controls(context.gradient_context, stroke_gradient_form, stroke_gradient_group,
                             QStringLiteral("layerStyleStrokeGradient"), stroke.gradient,
                             QObject::tr("Choose Gradient Stop Color"), /*offer_shape_burst=*/true);
  stroke_form->addRow(stroke_gradient_group);
  auto update_stroke_fill_visibility = [stroke_fill, stroke_form, stroke_rows, stroke_gradient_group] {
    const bool uses_gradient = stroke_fill->currentData().toBool();
    stroke_form->setRowVisible(stroke_rows.row_container, !uses_gradient);
    stroke_form->setRowVisible(stroke_gradient_group, uses_gradient);
  };
  // The stroke page scrolls instead of sizing the dialog: a stacked layout's
  // minimum is the max over all pages, and this page's nested gradient block
  // (Fill = Gradient) would otherwise floor the whole dialog ~180px above the
  // tallest overlay page.
  auto* stroke_scroll = new QScrollArea(&dialog);
  stroke_scroll->setObjectName(QStringLiteral("layerStyleStrokeScroll"));
  stroke_scroll->setWidgetResizable(true);
  stroke_scroll->setFrameShape(QFrame::NoFrame);
  stroke_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  set_themed_style(*stroke_scroll,
                   QStringLiteral("QScrollArea#layerStyleStrokeScroll { background: transparent; }"
                                  "QScrollArea#layerStyleStrokeScroll > QWidget > QWidget { "
                                  "background: transparent; }"));
  auto* stroke_scroll_page = new QWidget(stroke_scroll);
  stroke_scroll_page->setObjectName(QStringLiteral("layerStyleStrokeScrollPage"));
  auto* stroke_scroll_column = new QVBoxLayout(stroke_scroll_page);
  stroke_scroll_column->setContentsMargins(0, 0, 6, 0);
  stroke_scroll_column->addWidget(stroke_group);
  stroke_scroll_column->addStretch(1);
  stroke_scroll->setWidget(stroke_scroll_page);
  // Measured while the gradient group is still visible so the sliders keep
  // their full width when the vertical scrollbar appears.
  stroke_scroll->setMinimumWidth(stroke_scroll_page->minimumSizeHint().width() +
                                 stroke_scroll->verticalScrollBar()->sizeHint().width());
  update_stroke_fill_visibility();
  stroke_layout->addWidget(stroke_scroll, 1);

  return {stroke_size, stroke_position, stroke_blend, stroke_opacity, stroke_overprint, stroke_fill, stroke_rows, stroke_red, stroke_green, stroke_blue, std::move(stroke_gradient_controls), update_stroke_fill_visibility};
}

InnerShadowPageControls build_inner_shadow_page(LayerStylePageContext& context, QVBoxLayout* inner_shadow_layout, const LayerInnerShadow& inner_shadow) {
  auto& controls = context.controls;
  auto* inner_shadow_group = new QGroupBox(QObject::tr("Inner Shadow"), controls);
  auto* inner_shadow_form = new QFormLayout(inner_shadow_group);
  auto* inner_shadow_blend = new QComboBox(inner_shadow_group);
  inner_shadow_blend->setObjectName(QStringLiteral("layerStyleInnerShadowBlendModeCombo"));
  add_blend_mode_items(inner_shadow_blend, BlendModeMenu::Effect);
  inner_shadow_blend->setCurrentIndex(
      std::max(0, inner_shadow_blend->findData(static_cast<int>(inner_shadow.blend_mode))));
  inner_shadow_form->addRow(QObject::tr("Blend Mode"), inner_shadow_blend);
  auto* inner_shadow_opacity =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleInnerShadowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(inner_shadow.opacity * 100.0F)), QStringLiteral("%"));
  auto* inner_shadow_angle =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Angle"),
                          QStringLiteral("layerStyleInnerShadowAngleSpin"), -180, 180,
                          static_cast<int>(std::round(inner_shadow.angle_degrees)));
  // Photoshop's 0..30000 px typed range; the slider keeps its 0..1000 reach.
  auto* inner_shadow_distance =
      add_size_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Distance"),
                               QStringLiteral("layerStyleInnerShadowDistanceSpin"), 0, kShadowDistanceMaximum,
                               static_cast<int>(std::round(inner_shadow.distance)), 1000);
  auto* inner_shadow_size = add_size_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Size"),
                                                     QStringLiteral("layerStyleInnerShadowSizeSpin"), 0, 1000,
                                                     static_cast<int>(std::round(inner_shadow.size)));
  auto* inner_shadow_choke =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Choke"),
                          QStringLiteral("layerStyleInnerShadowChokeSpin"), 0, 100,
                          static_cast<int>(std::round(inner_shadow.choke)), QStringLiteral("%"));
  const auto inner_shadow_rows = make_color_rows(
      inner_shadow_form, inner_shadow_group, QStringLiteral("layerStyleInnerShadow"), inner_shadow.color);
  auto* inner_shadow_red = inner_shadow_rows.red;
  auto* inner_shadow_green = inner_shadow_rows.green;
  auto* inner_shadow_blue = inner_shadow_rows.blue;
  auto* inner_shadow_instance_row = new QWidget(inner_shadow_group);
  auto* inner_shadow_instance_layout = new QHBoxLayout(inner_shadow_instance_row);
  inner_shadow_instance_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_inner_shadow = new QPushButton(QStringLiteral("+"), inner_shadow_instance_row);
  add_inner_shadow->setObjectName(QStringLiteral("layerStyleAddInnerShadowButton"));
  add_inner_shadow->setToolTip(QObject::tr("Add Inner Shadow"));
  configure_compact_symbol_button(add_inner_shadow);
  auto* remove_inner_shadow = new QPushButton(QStringLiteral("-"), inner_shadow_instance_row);
  remove_inner_shadow->setObjectName(QStringLiteral("layerStyleRemoveInnerShadowButton"));
  remove_inner_shadow->setToolTip(QObject::tr("Remove Inner Shadow"));
  configure_compact_symbol_button(remove_inner_shadow);
  inner_shadow_instance_layout->addWidget(add_inner_shadow);
  inner_shadow_instance_layout->addWidget(remove_inner_shadow);
  inner_shadow_instance_layout->addStretch(1);
  inner_shadow_form->addRow(QObject::tr("Instances"), inner_shadow_instance_row);
  inner_shadow_layout->addWidget(inner_shadow_group);
  inner_shadow_layout->addStretch(1);

  return {inner_shadow_blend, inner_shadow_opacity, inner_shadow_angle, inner_shadow_distance, inner_shadow_size, inner_shadow_choke, inner_shadow_rows, inner_shadow_red, inner_shadow_green, inner_shadow_blue, add_inner_shadow, remove_inner_shadow};
}

InnerGlowPageControls build_inner_glow_page(LayerStylePageContext& context, QVBoxLayout* inner_glow_layout, const LayerInnerGlow& inner_glow) {
  auto& controls = context.controls;
  auto* inner_glow_group = new QGroupBox(QObject::tr("Inner Glow"), controls);
  auto* inner_glow_form = new QFormLayout(inner_glow_group);
  auto* inner_glow_blend = new QComboBox(inner_glow_group);
  inner_glow_blend->setObjectName(QStringLiteral("layerStyleInnerGlowBlendModeCombo"));
  add_blend_mode_items(inner_glow_blend, BlendModeMenu::Effect);
  inner_glow_blend->setCurrentIndex(std::max(0, inner_glow_blend->findData(static_cast<int>(inner_glow.blend_mode))));
  inner_glow_form->addRow(QObject::tr("Blend Mode"), inner_glow_blend);
  auto* inner_glow_technique = new QComboBox(inner_glow_group);
  inner_glow_technique->setObjectName(QStringLiteral("layerStyleInnerGlowTechniqueCombo"));
  inner_glow_technique->addItem(QObject::tr("Softer"), static_cast<int>(LayerGlowTechnique::Softer));
  inner_glow_technique->addItem(QObject::tr("Precise"), static_cast<int>(LayerGlowTechnique::Precise));
  inner_glow_technique->setCurrentIndex(
      std::max(0, inner_glow_technique->findData(static_cast<int>(inner_glow.technique))));
  inner_glow_form->addRow(QObject::tr("Technique"), inner_glow_technique);
  auto* inner_glow_opacity =
      add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleInnerGlowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(inner_glow.opacity * 100.0F)), QStringLiteral("%"));
  auto* inner_glow_size = add_size_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Size"),
                                                   QStringLiteral("layerStyleInnerGlowSizeSpin"), 0, 1000,
                                                   static_cast<int>(std::round(inner_glow.size)));
  auto* inner_glow_choke =
      add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Choke"),
                          QStringLiteral("layerStyleInnerGlowChokeSpin"), 0, 100,
                          static_cast<int>(std::round(inner_glow.choke)), QStringLiteral("%"));
  auto* inner_glow_range =
      add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Range"),
                          QStringLiteral("layerStyleInnerGlowRangeSpin"), 1, 100,
                          static_cast<int>(std::round(inner_glow.range)), QStringLiteral("%"));
  auto* inner_glow_source = new QComboBox(inner_glow_group);
  inner_glow_source->setObjectName(QStringLiteral("layerStyleInnerGlowSourceCombo"));
  inner_glow_source->addItem(QObject::tr("Edge"), static_cast<int>(LayerInnerGlowSource::Edge));
  inner_glow_source->addItem(QObject::tr("Center"), static_cast<int>(LayerInnerGlowSource::Center));
  inner_glow_source->setCurrentIndex(std::max(0, inner_glow_source->findData(static_cast<int>(inner_glow.source))));
  inner_glow_form->addRow(QObject::tr("Source"), inner_glow_source);
  const auto inner_glow_rows = make_color_rows(inner_glow_form, inner_glow_group,
                                               QStringLiteral("layerStyleInnerGlow"), inner_glow.color);
  auto* inner_glow_red = inner_glow_rows.red;
  auto* inner_glow_green = inner_glow_rows.green;
  auto* inner_glow_blue = inner_glow_rows.blue;
  auto* inner_glow_instance_row = new QWidget(inner_glow_group);
  auto* inner_glow_instance_layout = new QHBoxLayout(inner_glow_instance_row);
  inner_glow_instance_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_inner_glow = new QPushButton(QStringLiteral("+"), inner_glow_instance_row);
  add_inner_glow->setObjectName(QStringLiteral("layerStyleAddInnerGlowButton"));
  add_inner_glow->setToolTip(QObject::tr("Add Inner Glow"));
  configure_compact_symbol_button(add_inner_glow);
  auto* remove_inner_glow = new QPushButton(QStringLiteral("-"), inner_glow_instance_row);
  remove_inner_glow->setObjectName(QStringLiteral("layerStyleRemoveInnerGlowButton"));
  remove_inner_glow->setToolTip(QObject::tr("Remove Inner Glow"));
  configure_compact_symbol_button(remove_inner_glow);
  inner_glow_instance_layout->addWidget(add_inner_glow);
  inner_glow_instance_layout->addWidget(remove_inner_glow);
  inner_glow_instance_layout->addStretch(1);
  inner_glow_form->addRow(QObject::tr("Instances"), inner_glow_instance_row);
  inner_glow_layout->addWidget(inner_glow_group);
  inner_glow_layout->addStretch(1);

  return {inner_glow_blend, inner_glow_technique, inner_glow_opacity, inner_glow_size, inner_glow_choke, inner_glow_range, inner_glow_source, inner_glow_rows, inner_glow_red, inner_glow_green, inner_glow_blue, add_inner_glow, remove_inner_glow};
}

SatinPageControls build_satin_page(LayerStylePageContext& context, QVBoxLayout* satin_layout, const LayerSatin& satin) {
  auto& controls = context.controls;
  auto* satin_group = new QGroupBox(QObject::tr("Satin"), controls);
  auto* satin_form = new QFormLayout(satin_group);
  auto* satin_blend = new QComboBox(satin_group);
  satin_blend->setObjectName(QStringLiteral("layerStyleSatinBlendModeCombo"));
  add_blend_mode_items(satin_blend, BlendModeMenu::Effect);
  satin_blend->setCurrentIndex(std::max(0, satin_blend->findData(static_cast<int>(satin.blend_mode))));
  satin_form->addRow(QObject::tr("Blend Mode"), satin_blend);
  auto* satin_opacity =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleSatinOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(satin.opacity * 100.0F)), QStringLiteral("%"));
  auto* satin_angle =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Angle"),
                          QStringLiteral("layerStyleSatinAngleSpin"), -180, 180,
                          static_cast<int>(std::round(satin.angle_degrees)));
  auto* satin_distance =
      add_size_slider_spin_row(satin_form, satin_group, QObject::tr("Distance"),
                               QStringLiteral("layerStyleSatinDistanceSpin"), 0, 1000,
                               static_cast<int>(std::round(satin.distance)));
  auto* satin_size = add_size_slider_spin_row(satin_form, satin_group, QObject::tr("Size"),
                                              QStringLiteral("layerStyleSatinSizeSpin"), 0, 1000,
                                              static_cast<int>(std::round(satin.size)));
  auto* satin_invert = new QCheckBox(QObject::tr("Invert"), satin_group);
  satin_invert->setObjectName(QStringLiteral("layerStyleSatinInvertCheck"));
  satin_invert->setChecked(satin.invert);
  satin_form->addRow(QString(), satin_invert);
  const auto satin_rows =
      make_color_rows(satin_form, satin_group, QStringLiteral("layerStyleSatin"), satin.color);
  auto* satin_red = satin_rows.red;
  auto* satin_green = satin_rows.green;
  auto* satin_blue = satin_rows.blue;
  satin_layout->addWidget(satin_group);
  satin_layout->addStretch(1);

  return {satin_blend, satin_opacity, satin_angle, satin_distance, satin_size, satin_invert, satin_rows, satin_red, satin_green, satin_blue};
}

ColorOverlayPageControls build_color_overlay_page(LayerStylePageContext& context, QVBoxLayout* color_overlay_layout, const LayerColorOverlay& color_overlay) {
  auto& controls = context.controls;
  auto* color_overlay_group = new QGroupBox(QObject::tr("Color Overlay"), controls);
  auto* color_overlay_form = new QFormLayout(color_overlay_group);
  auto* color_overlay_blend = new QComboBox(color_overlay_group);
  color_overlay_blend->setObjectName(QStringLiteral("layerStyleColorOverlayBlendModeCombo"));
  add_blend_mode_items(color_overlay_blend, BlendModeMenu::Effect);
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
  color_overlay_color_preview->setToolTip(QObject::tr("Choose Color..."));
  auto* color_overlay_pick_color = new QPushButton(QObject::tr("Choose Color..."), color_overlay_group);
  color_overlay_pick_color->setObjectName(QStringLiteral("layerStyleColorOverlayPickColorButton"));
  // Double-clicking the patch opens the same picker as the button.
  new DoubleClickClicksButton(color_overlay_pick_color, color_overlay_color_preview);
  color_overlay_preview_layout->addWidget(color_overlay_color_preview);
  color_overlay_preview_layout->addWidget(color_overlay_pick_color);
  color_overlay_preview_layout->addStretch(1);
  color_overlay_color_layout->addWidget(color_overlay_preview_row);
  RgbColorRowWidgets color_overlay_rows;
  color_overlay_rows.red = color_overlay_red;
  color_overlay_rows.green = color_overlay_green;
  color_overlay_rows.blue = color_overlay_blue;
  color_overlay_rows.preview = color_overlay_color_preview;
  color_overlay_rows.picker = color_overlay_pick_color;
  color_overlay_rows.row_container = color_overlay_color_row;
  color_overlay_rows.update_preview();
  color_overlay_form->addRow(QObject::tr("Color RGB"), color_overlay_color_row);
  color_overlay_layout->addWidget(color_overlay_group);
  color_overlay_layout->addStretch(1);

  return {color_overlay_blend, color_overlay_opacity, color_overlay_red, color_overlay_green, color_overlay_blue, color_overlay_rows};
}

GradientPageControls build_gradient_page(LayerStylePageContext& context, QVBoxLayout* gradient_layout, const LayerGradientFill& gradient) {
  auto& controls = context.controls;
  auto* gradient_group = new QGroupBox(QObject::tr("Gradient Overlay"), controls);
  auto* gradient_form = new QFormLayout(gradient_group);
  auto* gradient_blend = new QComboBox(gradient_group);
  gradient_blend->setObjectName(QStringLiteral("layerStyleGradientBlendModeCombo"));
  add_blend_mode_items(gradient_blend, BlendModeMenu::Effect);
  gradient_blend->setCurrentIndex(std::max(0, gradient_blend->findData(static_cast<int>(gradient.blend_mode))));
  gradient_form->addRow(QObject::tr("Blend Mode"), gradient_blend);
  auto* gradient_opacity =
      add_slider_spin_row(gradient_form, gradient_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleGradientOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(gradient.opacity * 100.0F)), QStringLiteral("%"));
  auto gradient_controls =
      make_gradient_controls(context.gradient_context, gradient_form, gradient_group, QStringLiteral("layerStyleGradient"),
                             gradient.gradient, QObject::tr("Choose Gradient Stop Color"));
  gradient_layout->addWidget(gradient_group);
  gradient_layout->addStretch(1);

  return {gradient_blend, gradient_opacity, std::move(gradient_controls)};
}

OuterGlowPageControls build_outer_glow_page(LayerStylePageContext& context, QVBoxLayout* outer_glow_layout, const LayerOuterGlow& outer_glow) {
  auto& controls = context.controls;
  auto* outer_glow_group = new QGroupBox(QObject::tr("Outer Glow"), controls);
  auto* outer_glow_form = new QFormLayout(outer_glow_group);
  auto* outer_glow_blend = new QComboBox(outer_glow_group);
  outer_glow_blend->setObjectName(QStringLiteral("layerStyleOuterGlowBlendModeCombo"));
  add_blend_mode_items(outer_glow_blend, BlendModeMenu::Effect);
  outer_glow_blend->setCurrentIndex(std::max(0, outer_glow_blend->findData(static_cast<int>(outer_glow.blend_mode))));
  outer_glow_form->addRow(QObject::tr("Blend Mode"), outer_glow_blend);
  auto* outer_glow_technique = new QComboBox(outer_glow_group);
  outer_glow_technique->setObjectName(QStringLiteral("layerStyleOuterGlowTechniqueCombo"));
  outer_glow_technique->addItem(QObject::tr("Softer"), static_cast<int>(LayerGlowTechnique::Softer));
  outer_glow_technique->addItem(QObject::tr("Precise"), static_cast<int>(LayerGlowTechnique::Precise));
  outer_glow_technique->setCurrentIndex(
      std::max(0, outer_glow_technique->findData(static_cast<int>(outer_glow.technique))));
  outer_glow_form->addRow(QObject::tr("Technique"), outer_glow_technique);
  auto* outer_glow_opacity =
      add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleOuterGlowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(outer_glow.opacity * 100.0F)), QStringLiteral("%"));
  auto* outer_glow_size = add_size_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Size"),
                                                   QStringLiteral("layerStyleOuterGlowSizeSpin"), 0, 1000,
                                                   static_cast<int>(std::round(outer_glow.size)));
  auto* outer_glow_spread = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Spread"),
                                                QStringLiteral("layerStyleOuterGlowSpreadSpin"), 0, 100,
                                                static_cast<int>(std::round(outer_glow.spread)),
                                                QStringLiteral("%"));
  auto* outer_glow_range = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Range"),
                                               QStringLiteral("layerStyleOuterGlowRangeSpin"), 1, 100,
                                               static_cast<int>(std::round(outer_glow.range)),
                                               QStringLiteral("%"));
  const auto outer_glow_rows = make_color_rows(outer_glow_form, outer_glow_group,
                                               QStringLiteral("layerStyleOuterGlow"), outer_glow.color);
  auto* outer_glow_red = outer_glow_rows.red;
  auto* outer_glow_green = outer_glow_rows.green;
  auto* outer_glow_blue = outer_glow_rows.blue;
  outer_glow_layout->addWidget(outer_glow_group);
  outer_glow_layout->addStretch(1);

  return {outer_glow_blend, outer_glow_technique, outer_glow_opacity, outer_glow_size, outer_glow_spread, outer_glow_range, outer_glow_rows, outer_glow_red, outer_glow_green, outer_glow_blue};
}

ShadowPageControls build_shadow_page(LayerStylePageContext& context, QVBoxLayout* shadow_layout, const LayerDropShadow& shadow) {
  auto& controls = context.controls;
  auto& patchy_only_banner = context.patchy_only_banner;
  auto& continuous_shadow_explanation = context.continuous_shadow_explanation;
  auto* shadow_group = new QGroupBox(QObject::tr("Drop Shadow"), controls);
  auto* shadow_form = new QFormLayout(shadow_group);
  auto* shadow_blend = new QComboBox(shadow_group);
  shadow_blend->setObjectName(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
  add_blend_mode_items(shadow_blend, BlendModeMenu::Effect);
  shadow_blend->setCurrentIndex(std::max(0, shadow_blend->findData(static_cast<int>(shadow.blend_mode))));
  shadow_form->addRow(QObject::tr("Blend Mode"), shadow_blend);
  auto* shadow_opacity = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleDropShadowOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(shadow.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* shadow_angle = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Angle"),
                                           QStringLiteral("layerStyleDropShadowAngleSpin"), -180, 180,
                                           static_cast<int>(std::round(shadow.angle_degrees)));
  // Photoshop's 0..30000 px typed range (it round-trips through DrSh); the
  // slider keeps the 0..2000 reach of Photopea's long-shadow range.
  auto* shadow_distance = add_size_slider_spin_row(shadow_form, shadow_group, QObject::tr("Distance"),
                                                   QStringLiteral("layerStyleDropShadowDistanceSpin"), 0,
                                                   kShadowDistanceMaximum,
                                                   static_cast<int>(std::round(shadow.distance)), 2000);
  auto* shadow_size = add_size_slider_spin_row(shadow_form, shadow_group, QObject::tr("Size"),
                                               QStringLiteral("layerStyleDropShadowSizeSpin"), 0, 1000,
                                               static_cast<int>(std::round(shadow.size)));
  auto* shadow_spread = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Spread"),
                                            QStringLiteral("layerStyleDropShadowSpreadSpin"), 0, 100,
                                            static_cast<int>(std::round(shadow.spread)), QStringLiteral("%"));
  auto* shadow_continuous_row = new QWidget(shadow_group);
  auto* shadow_continuous_layout = new QHBoxLayout(shadow_continuous_row);
  shadow_continuous_layout->setContentsMargins(0, 0, 0, 0);
  shadow_continuous_layout->setSpacing(8);
  auto* shadow_continuous = new QCheckBox(QObject::tr("Continuous (long shadow)"), shadow_continuous_row);
  shadow_continuous->setObjectName(QStringLiteral("layerStyleDropShadowContinuousCheck"));
  shadow_continuous->setChecked(shadow.continuous);
  shadow_continuous->setToolTip(
      QObject::tr("Extend the shadow from the layer all the way out to Distance, the flat long-shadow look") +
      QStringLiteral("\n\n") + continuous_shadow_explanation);
  shadow_continuous_layout->addWidget(shadow_continuous);
  shadow_continuous_layout->addWidget(make_patchy_only_badge(shadow_continuous_row, continuous_shadow_explanation));
  shadow_continuous_layout->addStretch(1);
  shadow_form->addRow(QString(), shadow_continuous_row);
  auto* shadow_fade = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Fade"),
                                          QStringLiteral("layerStyleDropShadowFadeSpin"), 0, 100,
                                          static_cast<int>(std::round(shadow.fade)), QStringLiteral("%"));
  shadow_fade->setToolTip(QObject::tr("How much the long shadow fades out by its far end"));
  // The slider, field, and step buttons share one row widget; Fade only means
  // something while the sweep is on, and the banner follows the same state.
  auto* shadow_fade_row = shadow_fade->parentWidget();
  const auto sync_continuous_controls = [shadow_continuous, shadow_fade_row, patchy_only_banner] {
    const bool on = shadow_continuous->isChecked();
    if (shadow_fade_row != nullptr) {
      shadow_fade_row->setEnabled(on);
    }
    patchy_only_banner->setVisible(on);
  };
  sync_continuous_controls();
  auto* shadow_conceals = new QCheckBox(QObject::tr("Layer Knocks Out Drop Shadow"), shadow_group);
  shadow_conceals->setObjectName(QStringLiteral("layerStyleDropShadowConcealsCheck"));
  shadow_conceals->setChecked(shadow.layer_conceals);
  shadow_conceals->setToolTip(QObject::tr(
      "Hide the shadow under the layer's own shape so it never shows through knocked-out or "
      "semi-transparent content, like Photoshop"));
  shadow_form->addRow(QString(), shadow_conceals);
  const auto shadow_rows =
      make_color_rows(shadow_form, shadow_group, QStringLiteral("layerStyleDropShadow"), shadow.color);
  auto* shadow_red = shadow_rows.red;
  auto* shadow_green = shadow_rows.green;
  auto* shadow_blue = shadow_rows.blue;
  shadow_layout->addWidget(shadow_group);
  shadow_layout->addStretch(1);

  return {shadow_blend, shadow_opacity, shadow_angle, shadow_distance, shadow_size, shadow_spread, shadow_continuous, shadow_fade, sync_continuous_controls, shadow_conceals, shadow_rows, shadow_red, shadow_green, shadow_blue};
}

}  // namespace patchy::ui::layer_style_detail
