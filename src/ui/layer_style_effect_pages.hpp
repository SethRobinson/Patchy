#pragma once
#include "ui/layer_style_dialog_controls.hpp"
#include "ui/layer_style_gradient_controls.hpp"
#include <functional>
#include <memory>
class QDialog;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
namespace patchy::ui::layer_style_detail {
// Construction-only access to the dialog host. Retained callbacks use copied
// widget handles or the separate dialog-lifetime gradient context.
struct LayerStylePageContext {
  QDialog& dialog;
  QWidget* controls;
  LayerStyleGradientContext& gradient_context;
  std::function<QComboBox*(QWidget*, const QString&, const StyleContour&, const StyleContour&)> make_contour_combo;
  std::function<QComboBox*(QWidget*, const QString&, const std::string&, const std::string&)> make_pattern_combo;
  QLabel* patchy_only_banner;
  QString continuous_shadow_explanation;
};

struct BevelPageControls {
  QComboBox* bevel_style{};
  QComboBox* bevel_technique{};
  QSpinBox* bevel_size{};
  QSpinBox* bevel_soften{};
  QSpinBox* bevel_depth{};
  QSpinBox* bevel_angle{};
  QSpinBox* bevel_altitude{};
  QComboBox* bevel_direction{};
  QComboBox* bevel_gloss_contour{};
  QCheckBox* bevel_gloss_anti_aliased{};
  RgbColor bevel_highlight_color{};
  QComboBox* bevel_highlight_blend{};
  QSpinBox* bevel_highlight_opacity{};
  QPushButton* bevel_highlight_color_button{};
  RgbColor bevel_shadow_color{};
  QComboBox* bevel_shadow_blend{};
  QSpinBox* bevel_shadow_opacity{};
  QPushButton* bevel_shadow_color_button{};
};
BevelPageControls build_bevel_page(LayerStylePageContext& context, QVBoxLayout* bevel_layout, const LayerBevelEmboss& bevel, const StyleContour& custom_gloss_contour);

struct BevelContourPageControls {
  QComboBox* bevel_contour_combo{};
  QCheckBox* bevel_contour_anti_aliased{};
  QSpinBox* bevel_contour_range{};
};
BevelContourPageControls build_bevel_contour_page(LayerStylePageContext& context, QVBoxLayout* bevel_contour_layout, const LayerBevelEmboss& bevel, const StyleContour& custom_bevel_sub_contour);

struct BevelTexturePageControls {
  QComboBox* bevel_texture_pattern{};
  QPushButton* bevel_texture_manage{};
  QSpinBox* bevel_texture_scale{};
  QSpinBox* bevel_texture_depth{};
  QCheckBox* bevel_texture_invert{};
  QCheckBox* bevel_texture_link{};
  QPushButton* bevel_texture_snap{};
};
BevelTexturePageControls build_bevel_texture_page(LayerStylePageContext& context, QVBoxLayout* bevel_texture_layout, const LayerBevelEmboss& bevel);

struct PatternOverlayPageControls {
  QComboBox* pattern_overlay_blend{};
  QSpinBox* pattern_overlay_opacity{};
  QComboBox* pattern_overlay_pattern{};
  QPushButton* pattern_overlay_manage{};
  QSpinBox* pattern_overlay_angle{};
  QSpinBox* pattern_overlay_scale{};
  QCheckBox* pattern_overlay_link{};
  QPushButton* pattern_overlay_snap{};
};
PatternOverlayPageControls build_pattern_overlay_page(LayerStylePageContext& context, QVBoxLayout* pattern_overlay_layout, const LayerPatternOverlay& pattern_overlay);

struct StrokePageControls {
  QSpinBox* stroke_size{};
  QComboBox* stroke_position{};
  QComboBox* stroke_blend{};
  QSpinBox* stroke_opacity{};
  QCheckBox* stroke_overprint{};
  QComboBox* stroke_fill{};
  RgbColorRowWidgets stroke_rows{};
  QSpinBox* stroke_red{};
  QSpinBox* stroke_green{};
  QSpinBox* stroke_blue{};
  std::unique_ptr<LayerStyleGradientControls> stroke_gradient_controls{};
  std::function<void()> update_stroke_fill_visibility{};
};
StrokePageControls build_stroke_page(LayerStylePageContext& context, QVBoxLayout* stroke_layout, const LayerStroke& stroke);

struct InnerShadowPageControls {
  QComboBox* inner_shadow_blend{};
  QSpinBox* inner_shadow_opacity{};
  QSpinBox* inner_shadow_angle{};
  QSpinBox* inner_shadow_distance{};
  QSpinBox* inner_shadow_size{};
  QSpinBox* inner_shadow_choke{};
  RgbColorRowWidgets inner_shadow_rows{};
  QSpinBox* inner_shadow_red{};
  QSpinBox* inner_shadow_green{};
  QSpinBox* inner_shadow_blue{};
  QPushButton* add_inner_shadow{};
  QPushButton* remove_inner_shadow{};
};
InnerShadowPageControls build_inner_shadow_page(LayerStylePageContext& context, QVBoxLayout* inner_shadow_layout, const LayerInnerShadow& inner_shadow);

struct InnerGlowPageControls {
  QComboBox* inner_glow_blend{};
  QComboBox* inner_glow_technique{};
  QSpinBox* inner_glow_opacity{};
  QSpinBox* inner_glow_size{};
  QSpinBox* inner_glow_choke{};
  QSpinBox* inner_glow_range{};
  QComboBox* inner_glow_source{};
  RgbColorRowWidgets inner_glow_rows{};
  QSpinBox* inner_glow_red{};
  QSpinBox* inner_glow_green{};
  QSpinBox* inner_glow_blue{};
  QPushButton* add_inner_glow{};
  QPushButton* remove_inner_glow{};
};
InnerGlowPageControls build_inner_glow_page(LayerStylePageContext& context, QVBoxLayout* inner_glow_layout, const LayerInnerGlow& inner_glow);

struct SatinPageControls {
  QComboBox* satin_blend{};
  QSpinBox* satin_opacity{};
  QSpinBox* satin_angle{};
  QSpinBox* satin_distance{};
  QSpinBox* satin_size{};
  QCheckBox* satin_invert{};
  RgbColorRowWidgets satin_rows{};
  QSpinBox* satin_red{};
  QSpinBox* satin_green{};
  QSpinBox* satin_blue{};
};
SatinPageControls build_satin_page(LayerStylePageContext& context, QVBoxLayout* satin_layout, const LayerSatin& satin);

struct ColorOverlayPageControls {
  QComboBox* color_overlay_blend{};
  QSpinBox* color_overlay_opacity{};
  QSpinBox* color_overlay_red{};
  QSpinBox* color_overlay_green{};
  QSpinBox* color_overlay_blue{};
  RgbColorRowWidgets color_overlay_rows{};
};
ColorOverlayPageControls build_color_overlay_page(LayerStylePageContext& context, QVBoxLayout* color_overlay_layout, const LayerColorOverlay& color_overlay);

struct GradientPageControls {
  QComboBox* gradient_blend{};
  QSpinBox* gradient_opacity{};
  std::unique_ptr<LayerStyleGradientControls> gradient_controls{};
};
GradientPageControls build_gradient_page(LayerStylePageContext& context, QVBoxLayout* gradient_layout, const LayerGradientFill& gradient);

struct OuterGlowPageControls {
  QComboBox* outer_glow_blend{};
  QComboBox* outer_glow_technique{};
  QSpinBox* outer_glow_opacity{};
  QSpinBox* outer_glow_size{};
  QSpinBox* outer_glow_spread{};
  QSpinBox* outer_glow_range{};
  RgbColorRowWidgets outer_glow_rows{};
  QSpinBox* outer_glow_red{};
  QSpinBox* outer_glow_green{};
  QSpinBox* outer_glow_blue{};
};
OuterGlowPageControls build_outer_glow_page(LayerStylePageContext& context, QVBoxLayout* outer_glow_layout, const LayerOuterGlow& outer_glow);

struct ShadowPageControls {
  QComboBox* shadow_blend{};
  QSpinBox* shadow_opacity{};
  QSpinBox* shadow_angle{};
  QSpinBox* shadow_distance{};
  QSpinBox* shadow_size{};
  QSpinBox* shadow_spread{};
  QCheckBox* shadow_continuous{};
  QSpinBox* shadow_fade{};
  std::function<void()> sync_continuous_controls{};
  QCheckBox* shadow_conceals{};
  QComboBox* shadow_contour{};
  QCheckBox* shadow_anti_aliased{};
  RgbColorRowWidgets shadow_rows{};
  QSpinBox* shadow_red{};
  QSpinBox* shadow_green{};
  QSpinBox* shadow_blue{};
};
ShadowPageControls build_shadow_page(LayerStylePageContext& context, QVBoxLayout* shadow_layout, const LayerDropShadow& shadow, const StyleContour& custom_shadow_contour);

}  // namespace patchy::ui::layer_style_detail
