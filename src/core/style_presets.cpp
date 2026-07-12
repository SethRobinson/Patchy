#include "core/style_presets.hpp"

#include "core/contour_presets.hpp"
#include "core/pattern_presets.hpp"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace patchy {

namespace {

// Canonical folder names (translated at library seed time, like the pattern
// defaults folder).
constexpr const char* kTextFolder = "Text";
constexpr const char* kBasicsFolder = "Basics";
constexpr const char* kMaterialsFolder = "Materials";

// Built-in pattern preset ids (core/pattern_presets.cpp); fixed forever.
constexpr const char* kBrushedMetalPatternId = "c4a11e00-0008-4b1d-9c3e-7a7c9e55b008";
constexpr const char* kBumpsPatternId = "c4a11e00-0009-4b1d-9c3e-7a7c9e55b009";

// Bundled photo-texture pattern ids (PhotoPatternPreset table); fixed forever.
constexpr const char* kFineWoodGrainPatternId = "f0705a00-0001-4c8b-9e3d-2a5b6c77e001";
constexpr const char* kDarkWalnutPatternId = "f0705a00-0002-4c8b-9e3d-2a5b6c77e002";
constexpr const char* kOakVeneerPatternId = "f0705a00-0003-4c8b-9e3d-2a5b6c77e003";
constexpr const char* kWeatheredWoodPatternId = "f0705a00-0004-4c8b-9e3d-2a5b6c77e004";
constexpr const char* kOldPlanksPatternId = "f0705a00-0005-4c8b-9e3d-2a5b6c77e005";
constexpr const char* kTreeBarkPatternId = "f0705a00-0007-4c8b-9e3d-2a5b6c77e007";
constexpr const char* kWeatheredMarblePatternId = "f0705a00-0008-4c8b-9e3d-2a5b6c77e008";
constexpr const char* kSlateSlabsPatternId = "f0705a00-0009-4c8b-9e3d-2a5b6c77e009";
constexpr const char* kGraniteBlocksPatternId = "f0705a00-000a-4c8b-9e3d-2a5b6c77e00a";
constexpr const char* kCoarseRustPatternId = "f0705a00-000c-4c8b-9e3d-2a5b6c77e00c";
constexpr const char* kSteelPlatePatternId = "f0705a00-000d-4c8b-9e3d-2a5b6c77e00d";
constexpr const char* kBrownLeatherPatternId = "f0705a00-000e-4c8b-9e3d-2a5b6c77e00e";
constexpr const char* kSnowPatternId = "f0705a00-0012-4c8b-9e3d-2a5b6c77e012";
constexpr const char* kCrackedEarthPatternId = "f0705a00-0013-4c8b-9e3d-2a5b6c77e013";

constexpr RgbColor rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return RgbColor{red, green, blue};
}

GradientColorStop color_stop(float location, RgbColor color, float midpoint = 0.5F) {
  GradientColorStop stop;
  stop.location = location;
  stop.color = color;
  stop.midpoint = midpoint;
  return stop;
}

LayerStyleGradient vertical_gradient(std::vector<GradientColorStop> stops) {
  LayerStyleGradient gradient;
  gradient.type = LayerStyleGradientType::Linear;
  gradient.angle_degrees = 90.0F;
  gradient.scale = 1.0F;
  gradient.color_stops = std::move(stops);
  gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F}, GradientAlphaStop{1.0F, 1.0F, 0.5F}};
  return gradient;
}

LayerDropShadow drop_shadow(RgbColor color, float opacity, float angle, float distance, float size,
                            float spread = 0.0F) {
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = BlendMode::Multiply;
  shadow.color = color;
  shadow.opacity = opacity;
  shadow.angle_degrees = angle;
  shadow.distance = distance;
  shadow.spread = spread;
  shadow.size = size;
  return shadow;
}

LayerInnerShadow inner_shadow(RgbColor color, float opacity, float angle, float distance,
                              float size, float choke = 0.0F) {
  LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = BlendMode::Multiply;
  shadow.color = color;
  shadow.opacity = opacity;
  shadow.angle_degrees = angle;
  shadow.distance = distance;
  shadow.choke = choke;
  shadow.size = size;
  return shadow;
}

LayerOuterGlow outer_glow(RgbColor color, float opacity, float spread, float size) {
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Screen;
  glow.color = color;
  glow.opacity = opacity;
  glow.spread = spread;
  glow.size = size;
  return glow;
}

LayerInnerGlow inner_glow(RgbColor color, float opacity, float size, float choke = 0.0F) {
  LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Screen;
  glow.color = color;
  glow.opacity = opacity;
  glow.choke = choke;
  glow.size = size;
  glow.source = LayerInnerGlowSource::Edge;
  return glow;
}

LayerColorOverlay color_overlay(RgbColor color, float opacity = 1.0F) {
  LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = BlendMode::Normal;
  overlay.color = color;
  overlay.opacity = opacity;
  return overlay;
}

LayerGradientFill gradient_overlay(LayerStyleGradient gradient, float opacity = 1.0F) {
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = BlendMode::Normal;
  fill.opacity = opacity;
  fill.gradient = std::move(gradient);
  return fill;
}

LayerPatternOverlay pattern_overlay(const char* pattern_id, const char* pattern_name,
                                    float scale = 1.0F) {
  LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = BlendMode::Normal;
  overlay.opacity = 1.0F;
  overlay.scale = scale;
  overlay.pattern_id = pattern_id;
  overlay.pattern_name = pattern_name;
  return overlay;
}

LayerStroke stroke(RgbColor color, float size, LayerStrokePosition position, float opacity = 1.0F) {
  LayerStroke result;
  result.enabled = true;
  result.blend_mode = BlendMode::Normal;
  result.color = color;
  result.opacity = opacity;
  result.size = size;
  result.position = position;
  return result;
}

LayerStroke gradient_stroke(LayerStyleGradient gradient, float size, LayerStrokePosition position) {
  LayerStroke result = stroke(rgb(0, 0, 0), size, position);
  result.uses_gradient = true;
  result.gradient = std::move(gradient);
  return result;
}

LayerBevelEmboss bevel(float size, float depth, RgbColor highlight, float highlight_opacity,
                       RgbColor shadow, float shadow_opacity) {
  LayerBevelEmboss result;
  result.enabled = true;
  result.highlight_blend_mode = BlendMode::Screen;
  result.highlight_color = highlight;
  result.highlight_opacity = highlight_opacity;
  result.shadow_blend_mode = BlendMode::Multiply;
  result.shadow_color = shadow;
  result.shadow_opacity = shadow_opacity;
  result.angle_degrees = 120.0F;
  result.altitude_degrees = 30.0F;
  result.depth = depth;
  result.size = size;
  result.direction_up = true;
  return result;
}

LayerSatin satin(RgbColor color, float opacity, float distance, float size,
                 BlendMode mode = BlendMode::Screen) {
  LayerSatin result;
  result.enabled = true;
  result.blend_mode = mode;
  result.color = color;
  result.opacity = opacity;
  result.angle_degrees = 19.0F;
  result.distance = distance;
  result.size = size;
  result.invert = true;
  return result;
}

StyleContour builtin_contour(const char* preset_id) {
  const auto* preset = find_builtin_contour_preset(std::string_view(preset_id));
  return preset != nullptr ? preset->contour : StyleContour{};
}

// --- Text folder -----------------------------------------------------------

LayerStyle style_adventure() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(122, 59, 16)),
      color_stop(0.45F, rgb(201, 138, 43)),
      color_stop(0.78F, rgb(244, 212, 120)),
      color_stop(1.0F, rgb(255, 242, 184)),
  })));
  style.strokes.push_back(stroke(rgb(58, 31, 10), 3.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.9F, 120.0F, 7.0F, 0.0F));
  style.bevels.push_back(bevel(3.0F, 1.5F, rgb(255, 255, 255), 0.6F, rgb(64, 38, 10), 0.6F));
  return style;
}

LayerStyle style_hack_the_gibson() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(0, 255, 65)));
  style.outer_glows.push_back(outer_glow(rgb(0, 255, 65), 0.85F, 8.0F, 16.0F));
  style.inner_glows.push_back(inner_glow(rgb(200, 255, 200), 0.6F, 4.0F));
  return style;
}

LayerStyle style_a_galaxy_far_away() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(255, 232, 31)));
  style.strokes.push_back(stroke(rgb(0, 0, 0), 3.0F, LayerStrokePosition::Outside));
  return style;
}

LayerStyle style_neon_nights() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(255, 72, 196)));
  style.outer_glows.push_back(outer_glow(rgb(255, 64, 190), 0.9F, 10.0F, 24.0F));
  style.inner_glows.push_back(inner_glow(rgb(255, 214, 240), 0.75F, 6.0F));
  return style;
}

LayerStyle style_arcade_cabinet() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(255, 255, 255)));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 229, 255), 1.0F, 135.0F, 4.0F, 0.0F));
  style.drop_shadows.back().blend_mode = BlendMode::Normal;
  style.drop_shadows.push_back(drop_shadow(rgb(255, 43, 214), 1.0F, 135.0F, 8.0F, 0.0F));
  style.drop_shadows.back().blend_mode = BlendMode::Normal;
  return style;
}

LayerStyle style_chrome_bumper() {
  LayerStyle style;
  auto gradient = vertical_gradient({
      color_stop(0.0F, rgb(124, 90, 46)),
      color_stop(0.42F, rgb(216, 201, 163)),
      color_stop(0.52F, rgb(31, 42, 51), 0.12F),
      color_stop(0.78F, rgb(143, 180, 204)),
      color_stop(1.0F, rgb(223, 234, 242)),
  });
  style.gradient_fills.push_back(gradient_overlay(std::move(gradient)));
  auto chrome_bevel = bevel(5.0F, 2.0F, rgb(255, 255, 255), 0.85F, rgb(0, 0, 0), 0.7F);
  chrome_bevel.gloss_contour = builtin_contour("contour.ring");
  chrome_bevel.gloss_anti_aliased = true;
  style.bevels.push_back(std::move(chrome_bevel));
  style.strokes.push_back(stroke(rgb(27, 34, 43), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.6F, 120.0F, 5.0F, 8.0F));
  return style;
}

LayerStyle style_liquid_gold() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(138, 90, 14)),
      color_stop(0.5F, rgb(232, 185, 60)),
      color_stop(0.8F, rgb(255, 224, 138)),
      color_stop(1.0F, rgb(255, 246, 201)),
  })));
  style.bevels.push_back(bevel(8.0F, 2.2F, rgb(255, 255, 255), 0.8F, rgb(74, 47, 5), 0.75F));
  style.inner_glows.push_back(inner_glow(rgb(255, 220, 120), 0.45F, 6.0F));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.55F, 120.0F, 5.0F, 7.0F));
  return style;
}

LayerStyle style_ice_cold() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(74, 166, 216)),
      color_stop(0.55F, rgb(178, 226, 248)),
      color_stop(1.0F, rgb(238, 250, 255)),
  })));
  style.strokes.push_back(stroke(rgb(159, 217, 242), 2.0F, LayerStrokePosition::Outside));
  style.outer_glows.push_back(outer_glow(rgb(120, 200, 255), 0.6F, 0.0F, 12.0F));
  style.inner_glows.push_back(inner_glow(rgb(255, 255, 255), 0.7F, 5.0F));
  style.bevels.push_back(bevel(4.0F, 1.4F, rgb(255, 255, 255), 0.75F, rgb(38, 90, 128), 0.5F));
  return style;
}

LayerStyle style_molten_core() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(255, 223, 94)),
      color_stop(0.4F, rgb(255, 138, 0)),
      color_stop(0.75F, rgb(224, 48, 0)),
      color_stop(1.0F, rgb(138, 13, 0)),
  })));
  style.outer_glows.push_back(outer_glow(rgb(255, 120, 0), 0.8F, 6.0F, 18.0F));
  style.strokes.push_back(stroke(rgb(90, 16, 4), 2.0F, LayerStrokePosition::Outside));
  style.inner_glows.push_back(inner_glow(rgb(255, 230, 120), 0.6F, 5.0F));
  return style;
}

LayerStyle style_toxic_ooze() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(31, 122, 8)),
      color_stop(0.6F, rgb(94, 196, 17)),
      color_stop(1.0F, rgb(200, 255, 60)),
  })));
  style.strokes.push_back(stroke(rgb(20, 60, 8), 3.0F, LayerStrokePosition::Outside));
  style.outer_glows.push_back(outer_glow(rgb(110, 255, 40), 0.7F, 0.0F, 14.0F));
  style.satins.push_back(satin(rgb(230, 255, 150), 0.5F, 8.0F, 10.0F));
  return style;
}

LayerStyle style_midnight_horror() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(143, 15, 15)));
  style.inner_shadows.push_back(inner_shadow(rgb(0, 0, 0), 0.85F, 120.0F, 6.0F, 8.0F));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.9F, 120.0F, 6.0F, 0.0F));
  return style;
}

LayerStyle style_wanted_poster() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(120, 66, 18)));
  style.strokes.push_back(stroke(rgb(58, 30, 8), 3.0F, LayerStrokePosition::Outside));
  auto flat_shadow = drop_shadow(rgb(196, 164, 110), 1.0F, 120.0F, 5.0F, 0.0F);
  flat_shadow.blend_mode = BlendMode::Normal;
  style.drop_shadows.push_back(std::move(flat_shadow));
  return style;
}

LayerStyle style_comic_pow() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(255, 205, 0)));
  style.strokes.push_back(stroke(rgb(226, 6, 44), 3.0F, LayerStrokePosition::Inside));
  style.strokes.push_back(stroke(rgb(255, 255, 255), 6.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.9F, 128.0F, 7.0F, 0.0F));
  return style;
}

LayerStyle style_bubble_pop() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(255, 90, 168)),
      color_stop(0.6F, rgb(255, 138, 194)),
      color_stop(1.0F, rgb(255, 209, 232)),
  })));
  style.bevels.push_back(bevel(7.0F, 1.8F, rgb(255, 255, 255), 0.8F, rgb(150, 30, 90), 0.55F));
  style.satins.push_back(satin(rgb(255, 255, 255), 0.35F, 6.0F, 12.0F));
  style.strokes.push_back(stroke(rgb(255, 255, 255), 3.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(233, 60, 140), 0.4F, 120.0F, 4.0F, 6.0F));
  return style;
}

LayerStyle style_saturday_cartoon() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(255, 150, 26)));
  style.strokes.push_back(stroke(rgb(255, 255, 255), 5.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.5F, 120.0F, 6.0F, 6.0F));
  return style;
}

LayerStyle style_space_cadet() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(13, 32, 54)),
      color_stop(0.48F, rgb(59, 116, 168)),
      color_stop(0.52F, rgb(11, 26, 44), 0.1F),
      color_stop(1.0F, rgb(95, 182, 232)),
  })));
  style.outer_glows.push_back(outer_glow(rgb(0, 229, 255), 0.65F, 4.0F, 10.0F));
  style.strokes.push_back(stroke(rgb(8, 20, 34), 2.0F, LayerStrokePosition::Outside));
  return style;
}

LayerStyle style_royal_decree() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(58, 16, 80)),
      color_stop(0.5F, rgb(122, 47, 160)),
      color_stop(1.0F, rgb(184, 120, 216)),
  })));
  style.strokes.push_back(gradient_stroke(vertical_gradient({
                                              color_stop(0.0F, rgb(138, 90, 14)),
                                              color_stop(0.5F, rgb(242, 207, 92)),
                                              color_stop(1.0F, rgb(138, 90, 14)),
                                          }),
                                          3.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.6F, 120.0F, 5.0F, 8.0F));
  return style;
}

LayerStyle style_stamped_steel() {
  LayerStyle style;
  style.pattern_overlays.push_back(pattern_overlay(kBrushedMetalPatternId, "Brushed Metal"));
  auto steel_bevel = bevel(5.0F, 1.6F, rgb(255, 255, 255), 0.75F, rgb(20, 24, 30), 0.65F);
  steel_bevel.texture.enabled = true;
  steel_bevel.texture.pattern_id = kBumpsPatternId;
  steel_bevel.texture.pattern_name = "Bumps";
  steel_bevel.texture.scale = 1.0F;
  steel_bevel.texture.depth = 0.5F;
  style.bevels.push_back(std::move(steel_bevel));
  style.strokes.push_back(stroke(rgb(30, 34, 40), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.6F, 120.0F, 4.0F, 5.0F));
  return style;
}

LayerStyle style_honey_drip() {
  LayerStyle style;
  style.gradient_fills.push_back(gradient_overlay(vertical_gradient({
      color_stop(0.0F, rgb(122, 74, 8)),
      color_stop(0.5F, rgb(216, 145, 42)),
      color_stop(1.0F, rgb(255, 217, 128)),
  })));
  style.satins.push_back(satin(rgb(255, 240, 180), 0.5F, 10.0F, 12.0F));
  style.inner_glows.push_back(inner_glow(rgb(255, 200, 90), 0.5F, 5.0F));
  style.bevels.push_back(bevel(3.0F, 1.2F, rgb(255, 255, 255), 0.6F, rgb(110, 62, 8), 0.5F));
  style.drop_shadows.push_back(drop_shadow(rgb(120, 70, 10), 0.5F, 120.0F, 4.0F, 6.0F));
  return style;
}

LayerStyle style_blueprint() {
  LayerStyle style;
  style.color_overlays.push_back(color_overlay(rgb(255, 255, 255)));
  style.strokes.push_back(stroke(rgb(255, 255, 255), 2.0F, LayerStrokePosition::Outside, 0.85F));
  style.outer_glows.push_back(outer_glow(rgb(200, 230, 255), 0.55F, 0.0F, 9.0F));
  return style;
}

// --- Basics folder ---------------------------------------------------------

LayerStyle style_soft_shadow() {
  LayerStyle style;
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.55F, 120.0F, 6.0F, 10.0F));
  return style;
}

LayerStyle style_sticker_outline() {
  LayerStyle style;
  style.strokes.push_back(stroke(rgb(255, 255, 255), 6.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.45F, 120.0F, 4.0F, 6.0F));
  return style;
}

LayerStyle style_simple_emboss() {
  LayerStyle style;
  style.bevels.push_back(bevel(5.0F, 1.0F, rgb(255, 255, 255), 0.75F, rgb(0, 0, 0), 0.75F));
  return style;
}

LayerStyle style_warm_glow() {
  LayerStyle style;
  style.outer_glows.push_back(outer_glow(rgb(255, 214, 130), 0.8F, 4.0F, 16.0F));
  return style;
}

LayerStyle style_neon_edge() {
  LayerStyle style;
  style.strokes.push_back(stroke(rgb(64, 255, 190), 2.0F, LayerStrokePosition::Outside));
  style.outer_glows.push_back(outer_glow(rgb(64, 255, 190), 0.8F, 0.0F, 12.0F));
  return style;
}

LayerStyle style_letterpress() {
  LayerStyle style;
  style.inner_shadows.push_back(inner_shadow(rgb(0, 0, 0), 0.6F, 120.0F, 2.0F, 3.0F));
  auto edge = drop_shadow(rgb(255, 255, 255), 0.8F, 120.0F, 1.0F, 0.0F);
  edge.blend_mode = BlendMode::Normal;
  style.drop_shadows.push_back(std::move(edge));
  return style;
}

// --- Materials folder (bundled photo textures) ------------------------------

LayerPatternOverlay photo_overlay(const char* pattern_id, const char* pattern_name,
                                  float scale = 1.0F) {
  return pattern_overlay(pattern_id, pattern_name, scale);
}

LayerStyle style_carved_oak() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kOakVeneerPatternId, "Oak Veneer", 0.5F));
  auto carved = bevel(5.0F, 1.8F, rgb(255, 244, 220), 0.7F, rgb(56, 34, 14), 0.7F);
  carved.texture.enabled = true;
  carved.texture.pattern_id = kTreeBarkPatternId;
  carved.texture.pattern_name = "Tree Bark";
  carved.texture.scale = 0.35F;
  carved.texture.depth = 0.35F;
  style.bevels.push_back(std::move(carved));
  style.strokes.push_back(stroke(rgb(74, 46, 20), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(30, 18, 8), 0.55F, 120.0F, 5.0F, 6.0F));
  return style;
}

LayerStyle style_walnut_gloss() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kDarkWalnutPatternId, "Dark Walnut", 0.5F));
  style.bevels.push_back(bevel(4.0F, 1.5F, rgb(255, 240, 214), 0.75F, rgb(24, 12, 4), 0.7F));
  style.satins.push_back(satin(rgb(255, 236, 200), 0.3F, 8.0F, 12.0F));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.5F, 120.0F, 4.0F, 6.0F));
  return style;
}

LayerStyle style_weathered_sign() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kOldPlanksPatternId, "Old Planks", 0.5F));
  style.bevels.push_back(bevel(3.0F, 1.2F, rgb(240, 228, 205), 0.6F, rgb(32, 22, 12), 0.65F));
  style.strokes.push_back(stroke(rgb(40, 26, 12), 3.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.75F, 120.0F, 5.0F, 0.0F));
  return style;
}

LayerStyle style_driftwood() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kWeatheredWoodPatternId, "Weathered Wood", 0.5F));
  style.strokes.push_back(stroke(rgb(90, 88, 82), 2.0F, LayerStrokePosition::Outside));
  style.inner_glows.push_back(inner_glow(rgb(230, 225, 210), 0.4F, 5.0F));
  style.drop_shadows.push_back(drop_shadow(rgb(20, 22, 24), 0.45F, 120.0F, 4.0F, 7.0F));
  return style;
}

LayerStyle style_timber_grain() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kFineWoodGrainPatternId, "Fine Wood Grain", 0.5F));
  style.bevels.push_back(bevel(4.0F, 1.5F, rgb(250, 232, 200), 0.65F, rgb(26, 16, 6), 0.7F));
  style.strokes.push_back(stroke(rgb(48, 30, 12), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.55F, 120.0F, 5.0F, 5.0F));
  return style;
}

LayerStyle style_marble_monument() {
  LayerStyle style;
  style.pattern_overlays.push_back(
      photo_overlay(kWeatheredMarblePatternId, "Weathered Marble", 0.5F));
  style.bevels.push_back(bevel(6.0F, 1.6F, rgb(255, 255, 255), 0.7F, rgb(40, 40, 44), 0.65F));
  style.inner_shadows.push_back(inner_shadow(rgb(30, 30, 34), 0.5F, 120.0F, 3.0F, 5.0F));
  style.strokes.push_back(stroke(rgb(70, 70, 74), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.5F, 120.0F, 5.0F, 8.0F));
  return style;
}

LayerStyle style_slate_etched() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kSlateSlabsPatternId, "Slate Slabs", 0.5F));
  style.inner_shadows.push_back(inner_shadow(rgb(0, 0, 0), 0.7F, 120.0F, 2.0F, 3.0F));
  auto edge = drop_shadow(rgb(196, 200, 206), 0.7F, 120.0F, 1.0F, 0.0F);
  edge.blend_mode = BlendMode::Normal;
  style.drop_shadows.push_back(std::move(edge));
  return style;
}

LayerStyle style_granite_bold() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kGraniteBlocksPatternId, "Granite Blocks", 0.5F));
  style.bevels.push_back(bevel(7.0F, 2.0F, rgb(235, 235, 235), 0.7F, rgb(18, 18, 20), 0.75F));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.7F, 120.0F, 6.0F, 0.0F));
  return style;
}

LayerStyle style_rust_bucket() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kCoarseRustPatternId, "Coarse Rust", 0.5F));
  style.bevels.push_back(bevel(4.0F, 1.4F, rgb(255, 214, 170), 0.6F, rgb(30, 12, 6), 0.7F));
  style.strokes.push_back(stroke(rgb(46, 20, 10), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(20, 8, 4), 0.6F, 120.0F, 5.0F, 5.0F));
  return style;
}

LayerStyle style_riveted_steel() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kSteelPlatePatternId, "Steel Plate", 0.5F));
  auto plated = bevel(5.0F, 1.6F, rgb(240, 246, 252), 0.75F, rgb(12, 16, 22), 0.7F);
  plated.texture.enabled = true;
  plated.texture.pattern_id = kBumpsPatternId;
  plated.texture.pattern_name = "Bumps";
  plated.texture.scale = 1.0F;
  plated.texture.depth = 0.4F;
  style.bevels.push_back(std::move(plated));
  style.strokes.push_back(stroke(rgb(24, 28, 34), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(0, 0, 0), 0.6F, 120.0F, 4.0F, 5.0F));
  return style;
}

LayerStyle style_leather_stamp() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kBrownLeatherPatternId, "Brown Leather", 0.5F));
  style.inner_shadows.push_back(inner_shadow(rgb(30, 14, 6), 0.75F, 120.0F, 3.0F, 5.0F));
  style.strokes.push_back(stroke(rgb(52, 28, 14), 2.0F, LayerStrokePosition::Inside));
  style.drop_shadows.push_back(drop_shadow(rgb(24, 12, 6), 0.35F, 120.0F, 3.0F, 4.0F));
  return style;
}

LayerStyle style_frost_drift() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kSnowPatternId, "Snow", 0.5F));
  style.outer_glows.push_back(outer_glow(rgb(170, 215, 255), 0.6F, 0.0F, 12.0F));
  style.inner_glows.push_back(inner_glow(rgb(255, 255, 255), 0.6F, 5.0F));
  style.bevels.push_back(bevel(3.0F, 1.2F, rgb(255, 255, 255), 0.7F, rgb(120, 160, 200), 0.4F));
  return style;
}

LayerStyle style_cracked_desert() {
  LayerStyle style;
  style.pattern_overlays.push_back(photo_overlay(kCrackedEarthPatternId, "Cracked Earth", 0.5F));
  style.bevels.push_back(bevel(3.0F, 1.3F, rgb(255, 240, 210), 0.6F, rgb(60, 36, 16), 0.65F));
  style.strokes.push_back(stroke(rgb(96, 64, 34), 2.0F, LayerStrokePosition::Outside));
  style.drop_shadows.push_back(drop_shadow(rgb(60, 30, 10), 0.6F, 120.0F, 5.0F, 6.0F));
  return style;
}

using StyleBuilder = LayerStyle (*)();

struct StylePresetEntry {
  StylePreset preset;
  StyleBuilder build;
};

// Append-only; ids and canonical names persist in user libraries and .asl
// exports. New presets go on the end with fresh ids and the defaults version
// that introduced them.
constexpr std::array<StylePresetEntry, 39> kBuiltinStylePresets{{
    {{"57a1e500-0001-4c6d-8f2a-9b3d4e55c001", "Adventure", kTextFolder, 1}, style_adventure},
    {{"57a1e500-0002-4c6d-8f2a-9b3d4e55c002", "Hack the Gibson", kTextFolder, 1}, style_hack_the_gibson},
    {{"57a1e500-0003-4c6d-8f2a-9b3d4e55c003", "A Galaxy Far Away", kTextFolder, 1}, style_a_galaxy_far_away},
    {{"57a1e500-0004-4c6d-8f2a-9b3d4e55c004", "Neon Nights", kTextFolder, 1}, style_neon_nights},
    {{"57a1e500-0005-4c6d-8f2a-9b3d4e55c005", "Arcade Cabinet", kTextFolder, 1}, style_arcade_cabinet},
    {{"57a1e500-0006-4c6d-8f2a-9b3d4e55c006", "Chrome Bumper", kTextFolder, 1}, style_chrome_bumper},
    {{"57a1e500-0007-4c6d-8f2a-9b3d4e55c007", "Liquid Gold", kTextFolder, 1}, style_liquid_gold},
    {{"57a1e500-0008-4c6d-8f2a-9b3d4e55c008", "Ice Cold", kTextFolder, 1}, style_ice_cold},
    {{"57a1e500-0009-4c6d-8f2a-9b3d4e55c009", "Molten Core", kTextFolder, 1}, style_molten_core},
    {{"57a1e500-000a-4c6d-8f2a-9b3d4e55c00a", "Toxic Ooze", kTextFolder, 1}, style_toxic_ooze},
    {{"57a1e500-000b-4c6d-8f2a-9b3d4e55c00b", "Midnight Horror", kTextFolder, 1}, style_midnight_horror},
    {{"57a1e500-000c-4c6d-8f2a-9b3d4e55c00c", "Wanted Poster", kTextFolder, 1}, style_wanted_poster},
    {{"57a1e500-000d-4c6d-8f2a-9b3d4e55c00d", "Comic Pow", kTextFolder, 1}, style_comic_pow},
    {{"57a1e500-000e-4c6d-8f2a-9b3d4e55c00e", "Bubble Pop", kTextFolder, 1}, style_bubble_pop},
    {{"57a1e500-000f-4c6d-8f2a-9b3d4e55c00f", "Saturday Cartoon", kTextFolder, 1}, style_saturday_cartoon},
    {{"57a1e500-0010-4c6d-8f2a-9b3d4e55c010", "Space Cadet", kTextFolder, 1}, style_space_cadet},
    {{"57a1e500-0011-4c6d-8f2a-9b3d4e55c011", "Royal Decree", kTextFolder, 1}, style_royal_decree},
    {{"57a1e500-0012-4c6d-8f2a-9b3d4e55c012", "Stamped Steel", kTextFolder, 1}, style_stamped_steel},
    {{"57a1e500-0013-4c6d-8f2a-9b3d4e55c013", "Honey Drip", kTextFolder, 1}, style_honey_drip},
    {{"57a1e500-0014-4c6d-8f2a-9b3d4e55c014", "Blueprint", kTextFolder, 1}, style_blueprint},
    {{"57a1e500-0015-4c6d-8f2a-9b3d4e55c015", "Soft Shadow", kBasicsFolder, 1}, style_soft_shadow},
    {{"57a1e500-0016-4c6d-8f2a-9b3d4e55c016", "Sticker Outline", kBasicsFolder, 1}, style_sticker_outline},
    {{"57a1e500-0017-4c6d-8f2a-9b3d4e55c017", "Simple Emboss", kBasicsFolder, 1}, style_simple_emboss},
    {{"57a1e500-0018-4c6d-8f2a-9b3d4e55c018", "Warm Glow", kBasicsFolder, 1}, style_warm_glow},
    {{"57a1e500-0019-4c6d-8f2a-9b3d4e55c019", "Neon Edge", kBasicsFolder, 1}, style_neon_edge},
    {{"57a1e500-001a-4c6d-8f2a-9b3d4e55c01a", "Letterpress", kBasicsFolder, 1}, style_letterpress},
    {{"57a1e500-001b-4c6d-8f2a-9b3d4e55c01b", "Carved Oak", kMaterialsFolder, 2}, style_carved_oak},
    {{"57a1e500-001c-4c6d-8f2a-9b3d4e55c01c", "Walnut Gloss", kMaterialsFolder, 2}, style_walnut_gloss},
    {{"57a1e500-001d-4c6d-8f2a-9b3d4e55c01d", "Weathered Sign", kMaterialsFolder, 2}, style_weathered_sign},
    {{"57a1e500-001e-4c6d-8f2a-9b3d4e55c01e", "Driftwood", kMaterialsFolder, 2}, style_driftwood},
    {{"57a1e500-001f-4c6d-8f2a-9b3d4e55c01f", "Timber Grain", kMaterialsFolder, 2}, style_timber_grain},
    {{"57a1e500-0020-4c6d-8f2a-9b3d4e55c020", "Marble Monument", kMaterialsFolder, 2}, style_marble_monument},
    {{"57a1e500-0021-4c6d-8f2a-9b3d4e55c021", "Slate Etched", kMaterialsFolder, 2}, style_slate_etched},
    {{"57a1e500-0022-4c6d-8f2a-9b3d4e55c022", "Granite Bold", kMaterialsFolder, 2}, style_granite_bold},
    {{"57a1e500-0023-4c6d-8f2a-9b3d4e55c023", "Rust Bucket", kMaterialsFolder, 2}, style_rust_bucket},
    {{"57a1e500-0024-4c6d-8f2a-9b3d4e55c024", "Riveted Steel", kMaterialsFolder, 2}, style_riveted_steel},
    {{"57a1e500-0025-4c6d-8f2a-9b3d4e55c025", "Leather Stamp", kMaterialsFolder, 2}, style_leather_stamp},
    {{"57a1e500-0026-4c6d-8f2a-9b3d4e55c026", "Frost Drift", kMaterialsFolder, 2}, style_frost_drift},
    {{"57a1e500-0027-4c6d-8f2a-9b3d4e55c027", "Cracked Desert", kMaterialsFolder, 2}, style_cracked_desert},
}};

// The presets table exposed without the builder pointers.
std::array<StylePreset, kBuiltinStylePresets.size()> build_preset_table() {
  std::array<StylePreset, kBuiltinStylePresets.size()> table{};
  for (std::size_t index = 0; index < kBuiltinStylePresets.size(); ++index) {
    table[index] = kBuiltinStylePresets[index].preset;
  }
  return table;
}

}  // namespace

std::span<const StylePreset> builtin_style_presets() noexcept {
  static const auto presets = build_preset_table();
  return presets;
}

const StylePreset* find_builtin_style_preset(std::string_view id) noexcept {
  const auto presets = builtin_style_presets();
  for (const auto& preset : presets) {
    if (id == preset.id) {
      return &preset;
    }
  }
  return nullptr;
}

LayerStyle builtin_style_preset_style(std::string_view id) {
  for (const auto& entry : kBuiltinStylePresets) {
    if (id == entry.preset.id) {
      return entry.build();
    }
  }
  return {};
}

}  // namespace patchy
