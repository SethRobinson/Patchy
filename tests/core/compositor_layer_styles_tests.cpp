#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/gradient_presets.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_density_probe.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/grd_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/pat_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/contour_presets.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "core/style_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "synthetic_dng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::solid_rgb;
using patchy::test::solid_rgba;
using patchy::test::write_rgb8_bmp_artifact;

void compositor_renders_layer_style_drop_shadow_gradient_and_stroke() {
  patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 12, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled", solid_rgba(4, 4, 220, 20, 20, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{3, 3, 4, 4});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 2.0F;
  shadow.size = 0.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = patchy::BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient.angle_degrees = 0.0F;
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{20, 60, 240}});
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{20, 220, 80}});
  layer.layer_style().gradient_fills.push_back(fill);

  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{255, 220, 0};
  stroke.opacity = 1.0F;
  stroke.size = 1.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  layer.layer_style().strokes.push_back(stroke);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* shadow_px = flattened.pixel(8, 4);
  CHECK(shadow_px[0] < 20);
  CHECK(shadow_px[1] < 20);
  CHECK(shadow_px[2] < 20);

  const auto* left_gradient = flattened.pixel(3, 4);
  const auto* right_gradient = flattened.pixel(6, 4);
  CHECK(left_gradient[2] > left_gradient[1]);
  CHECK(right_gradient[1] > right_gradient[2]);

  const auto* stroke_px = flattened.pixel(2, 4);
  CHECK(stroke_px[0] > 240);
  CHECK(stroke_px[1] > 200);
  CHECK(stroke_px[2] < 40);
}

void compositor_renders_layer_style_bevel_emboss() {
  patchy::Document document(12, 12, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 12, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Bevel", solid_rgba(6, 6, 120, 120, 120, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{3, 3, 6, 6});

  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Normal;
  bevel.highlight_color = patchy::RgbColor{255, 255, 255};
  bevel.highlight_opacity = 1.0F;
  bevel.shadow_blend_mode = patchy::BlendMode::Normal;
  bevel.shadow_color = patchy::RgbColor{0, 0, 0};
  bevel.shadow_opacity = 1.0F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 3.0F;
  bevel.size = 2.0F;
  layer.layer_style().bevels.push_back(bevel);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* highlighted = flattened.pixel(3, 3);
  const auto* shadowed = flattened.pixel(8, 8);
  CHECK(highlighted[0] > 150);
  CHECK(shadowed[0] < 100);
}

void compositor_renders_bevel_across_thin_checkbox_edges() {
  patchy::Document document(40, 40, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(40, 40, 255, 255, 255));

  auto pixels = solid_rgba(32, 32, 255, 242, 0, 0);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (x < 5 || y < 5 || x >= pixels.width() - 5 || y >= pixels.height() - 5) {
        auto* px = pixels.pixel(x, y);
        px[0] = 255;
        px[1] = 242;
        px[2] = 0;
        px[3] = 255;
      }
    }
  }

  patchy::Layer styled_layer(document.allocate_layer_id(), "Checkbox", std::move(pixels));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{4, 4, 32, 32});

  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{255, 242, 0};
  overlay.opacity = 1.0F;
  layer.layer_style().color_overlays.push_back(overlay);

  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Screen;
  bevel.highlight_color = patchy::RgbColor{255, 255, 255};
  bevel.highlight_opacity = 0.75F;
  bevel.shadow_blend_mode = patchy::BlendMode::Multiply;
  bevel.shadow_color = patchy::RgbColor{0, 0, 0};
  bevel.shadow_opacity = 0.75F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 1.0F;
  bevel.size = 5.0F;
  bevel.direction_up = true;
  layer.layer_style().bevels.push_back(bevel);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* top_center = flattened.pixel(20, 6);
  const auto* inner_top_edge = flattened.pixel(20, 8);
  const auto* left_center = flattened.pixel(6, 20);
  const auto* inner_left_edge = flattened.pixel(8, 20);

  CHECK(inner_top_edge[1] + 20 < top_center[1]);
  CHECK(inner_left_edge[1] + 20 < left_center[1]);

  int continuous_inner_top_shadow = 0;
  for (std::int32_t x = 12; x <= 28; ++x) {
    const auto* px = flattened.pixel(x, 8);
    if (px[1] + 20 < top_center[1]) {
      ++continuous_inner_top_shadow;
    }
  }
  CHECK(continuous_inner_top_shadow >= 14);
  write_rgb8_bmp_artifact("layer_style_bevel_checkbox", flattened);
}

void compositor_bevel_styles_techniques_and_soften_are_distinct() {
  const auto equal_pixels = [](const patchy::PixelBuffer& lhs, const patchy::PixelBuffer& rhs) {
    return lhs.data().size() == rhs.data().size() &&
           std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
  };
  const auto render = [](patchy::BevelEmbossStyleKind style, patchy::BevelTechnique technique, float soften,
                         bool add_stroke, bool enable_bevel = true, bool direction_up = true,
                         bool gradient_stroke = false, bool stacked_strokes = false) {
    patchy::Document document(56, 56, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(56, 56, 100, 100, 100));
    patchy::Layer styled(document.allocate_layer_id(), "Bevel", solid_rgba(28, 28, 120, 120, 120, 255));
    auto& layer = document.add_layer(std::move(styled));
    layer.set_bounds(patchy::Rect{14, 14, 28, 28});
    if (add_stroke) {
      patchy::LayerStroke stroke;
      stroke.enabled = true;
      stroke.blend_mode = patchy::BlendMode::Normal;
      stroke.color = patchy::RgbColor{180, 150, 40};
      stroke.opacity = 1.0F;
      stroke.size = 4.0F;
      stroke.position = patchy::LayerStrokePosition::Outside;
      if (gradient_stroke) {
        stroke.uses_gradient = true;
        stroke.gradient.color_stops = {{0.0F, patchy::RgbColor{255, 255, 255}},
                                       {1.0F, patchy::RgbColor{0, 0, 0}}};
        stroke.gradient.alpha_stops = {{0.0F, 1.0F}, {0.5F, 0.25F}, {1.0F, 1.0F}};
      }
      layer.layer_style().strokes.push_back(stroke);
      if (stacked_strokes) {
        stroke.color = patchy::RgbColor{40, 150, 200};
        stroke.size = 2.0F;
        stroke.position = patchy::LayerStrokePosition::Inside;
        stroke.uses_gradient = false;
        layer.layer_style().strokes.push_back(stroke);
      }
    }
    if (enable_bevel) {
      patchy::LayerBevelEmboss bevel;
      bevel.enabled = true;
      bevel.style = style;
      bevel.technique = technique;
      bevel.soften = soften;
      bevel.highlight_blend_mode = patchy::BlendMode::Normal;
      bevel.highlight_color = patchy::RgbColor{255, 255, 255};
      bevel.highlight_opacity = 1.0F;
      bevel.shadow_blend_mode = patchy::BlendMode::Normal;
      bevel.shadow_color = patchy::RgbColor{0, 0, 0};
      bevel.shadow_opacity = 1.0F;
      bevel.angle_degrees = 120.0F;
      bevel.altitude_degrees = 30.0F;
      bevel.depth = 2.0F;
      bevel.size = 6.0F;
      bevel.direction_up = direction_up;
      layer.layer_style().bevels.push_back(bevel);
    }
    return patchy::Compositor{}.flatten_rgb8(document);
  };

  const auto smooth = render(patchy::BevelEmbossStyleKind::InnerBevel, patchy::BevelTechnique::Smooth, 0.0F, false);
  const auto hard =
      render(patchy::BevelEmbossStyleKind::InnerBevel, patchy::BevelTechnique::ChiselHard, 0.0F, false);
  const auto soft =
      render(patchy::BevelEmbossStyleKind::InnerBevel, patchy::BevelTechnique::ChiselSoft, 0.0F, false);
  const auto softened = render(patchy::BevelEmbossStyleKind::InnerBevel, patchy::BevelTechnique::Smooth, 4.0F, false);
  CHECK(!equal_pixels(smooth, hard));
  CHECK(!equal_pixels(hard, soft));
  CHECK(!equal_pixels(smooth, softened));

  const auto inner = smooth;
  const auto outer =
      render(patchy::BevelEmbossStyleKind::OuterBevel, patchy::BevelTechnique::Smooth, 0.0F, false);
  int inner_exterior_pixels = 0;
  int outer_exterior_pixels = 0;
  for (std::int32_t y = 0; y < 56; ++y) {
    for (std::int32_t x = 0; x < 56; ++x) {
      if (x >= 14 && x < 42 && y >= 14 && y < 42) {
        continue;
      }
      inner_exterior_pixels += inner.pixel(x, y)[0] != 100;
      outer_exterior_pixels += outer.pixel(x, y)[0] != 100;
    }
  }
  CHECK(inner_exterior_pixels == 0);
  CHECK(outer_exterior_pixels > 0);

  const auto emboss = render(patchy::BevelEmbossStyleKind::Emboss, patchy::BevelTechnique::Smooth, 0.0F, false);
  const auto pillow =
      render(patchy::BevelEmbossStyleKind::PillowEmboss, patchy::BevelTechnique::Smooth, 0.0F, false);
  const auto pillow_down =
      render(patchy::BevelEmbossStyleKind::PillowEmboss, patchy::BevelTechnique::Smooth, 0.0F, false, true, false);
  CHECK(!equal_pixels(emboss, pillow));
  CHECK(!equal_pixels(pillow, pillow_down));

  const auto no_bevel =
      render(patchy::BevelEmbossStyleKind::StrokeEmboss, patchy::BevelTechnique::Smooth, 0.0F, false, false);
  const auto stroke_without_source =
      render(patchy::BevelEmbossStyleKind::StrokeEmboss, patchy::BevelTechnique::Smooth, 0.0F, false);
  CHECK(equal_pixels(no_bevel, stroke_without_source));
  const auto stroke_only =
      render(patchy::BevelEmbossStyleKind::StrokeEmboss, patchy::BevelTechnique::Smooth, 0.0F, true, false);
  const auto stroke_emboss =
      render(patchy::BevelEmbossStyleKind::StrokeEmboss, patchy::BevelTechnique::Smooth, 0.0F, true);
  CHECK(!equal_pixels(stroke_only, stroke_emboss));
  const auto gradient_stroke_emboss = render(patchy::BevelEmbossStyleKind::StrokeEmboss,
                                             patchy::BevelTechnique::Smooth, 0.0F, true, true, true, true);
  const auto stacked_stroke_emboss = render(patchy::BevelEmbossStyleKind::StrokeEmboss,
                                            patchy::BevelTechnique::Smooth, 0.0F, true, true, true, false, true);
  CHECK(!equal_pixels(stroke_emboss, gradient_stroke_emboss));
  CHECK(!equal_pixels(stroke_emboss, stacked_stroke_emboss));

  patchy::LayerStyle exterior_style;
  patchy::LayerBevelEmboss exterior_bevel;
  exterior_bevel.enabled = true;
  exterior_bevel.style = patchy::BevelEmbossStyleKind::OuterBevel;
  exterior_bevel.size = 6.0F;
  exterior_bevel.soften = 4.0F;
  exterior_style.bevels.push_back(exterior_bevel);
  CHECK(patchy::layer_style_effect_padding(exterior_style) >= 12);
}

void psd_bevel_styles_techniques_and_soften_round_trip() {
  constexpr std::array styles{
      patchy::BevelEmbossStyleKind::InnerBevel, patchy::BevelEmbossStyleKind::OuterBevel,
      patchy::BevelEmbossStyleKind::Emboss, patchy::BevelEmbossStyleKind::PillowEmboss,
      patchy::BevelEmbossStyleKind::StrokeEmboss};
  constexpr std::array techniques{patchy::BevelTechnique::Smooth, patchy::BevelTechnique::ChiselHard,
                                  patchy::BevelTechnique::ChiselSoft};
  for (const auto style : styles) {
    for (const auto technique : techniques) {
      patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
      patchy::Layer layer(document.allocate_layer_id(), "Bevel", solid_rgba(4, 4, 120, 120, 120, 255));
      layer.set_bounds(patchy::Rect{2, 2, 4, 4});
      patchy::LayerBevelEmboss bevel;
      bevel.enabled = true;
      bevel.style = style;
      bevel.technique = technique;
      bevel.soften = 7.0F;
      layer.layer_style().bevels.push_back(bevel);
      document.add_layer(std::move(layer));
      const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
      CHECK(reread.layers().size() == 1U);
      CHECK(reread.layers().front().layer_style().bevels.size() == 1U);
      const auto& imported = reread.layers().front().layer_style().bevels.front();
      CHECK(imported.style == style);
      CHECK(imported.technique == technique);
      CHECK(imported.soften == 7.0F);
    }
  }
}

void compositor_renders_layer_style_outer_glow() {
  CHECK(patchy::LayerOuterGlow{}.blend_mode == patchy::BlendMode::Normal);

  patchy::Document document(14, 14, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(14, 14, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Glow", solid_rgba(4, 4, 20, 20, 220, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{5, 5, 4, 4});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 4.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* glow_px = flattened.pixel(4, 6);
  const auto* layer_px = flattened.pixel(6, 6);
  CHECK(glow_px[0] > 240);
  CHECK(glow_px[1] < 120);
  CHECK(glow_px[2] < 120);
  CHECK(layer_px[2] > 200);
}

void compositor_outer_glow_spread_stays_within_size() {
  patchy::Document document(64, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(64, 32, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Glow", solid_rgba(4, 4, 20, 20, 220, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{30, 14, 4, 4});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 6.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* inside_glow_px = flattened.pixel(24, 15);
  const auto* outside_size_px = flattened.pixel(22, 15);
  CHECK(inside_glow_px[1] < 40);
  CHECK(outside_size_px[1] > 240);
  CHECK(outside_size_px[2] > 240);
}

void compositor_outer_glow_spread_uses_circular_distance_field() {
  patchy::Document document(28, 28, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(28, 28, 255, 255, 255));

  patchy::Layer styled_layer(document.allocate_layer_id(), "Glow", solid_rgba(4, 4, 20, 20, 220, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{12, 12, 4, 4});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 6.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* inside_radius = flattened.pixel(6, 13);
  const auto* outside_diagonal = flattened.pixel(7, 7);
  CHECK(inside_radius[0] > 240);
  CHECK(inside_radius[1] < 40);
  CHECK(inside_radius[2] < 40);
  CHECK(outside_diagonal[0] > 240);
  CHECK(outside_diagonal[1] > 240);
  CHECK(outside_diagonal[2] > 240);
}

void compositor_drop_shadow_preserves_source_alpha() {
  patchy::Document document(5, 3, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(5, 3, 255, 255, 255));

  patchy::Layer layer(document.allocate_layer_id(), "Half Alpha", solid_rgba(1, 1, 220, 20, 20, 128));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{1, 1, 1, 1});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 1.0F;
  shadow.size = 0.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* shadow_px = flattened.pixel(2, 1);
  CHECK(shadow_px[0] > 110);
  CHECK(shadow_px[0] < 150);
  CHECK(shadow_px[1] > 110);
  CHECK(shadow_px[1] < 150);
  CHECK(shadow_px[2] > 110);
  CHECK(shadow_px[2] < 150);
}

void compositor_drop_shadow_preserves_connected_antialias_alpha() {
  patchy::Document document(12, 8, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(12, 8, 255, 255, 255));

  auto pixels = solid_rgba(4, 3, 220, 20, 20, 0);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < 3; ++x) {
      pixels.pixel(x, y)[3] = 255;
    }
    pixels.pixel(3, y)[3] = 64;
  }

  patchy::Layer layer(document.allocate_layer_id(), "Connected Antialias", std::move(pixels));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{1, 2, 4, 3});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 3.0F;
  shadow.size = 0.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* antialias_shadow = flattened.pixel(7, 3);
  CHECK(antialias_shadow[0] > 175);
  CHECK(antialias_shadow[0] < 210);
  CHECK(antialias_shadow[1] > 175);
  CHECK(antialias_shadow[1] < 210);
  CHECK(antialias_shadow[2] > 175);
  CHECK(antialias_shadow[2] < 210);
}

void compositor_drop_shadow_soft_mask_has_smooth_falloff() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(180, 120, 255, 255, 255));

  auto pixels = solid_rgba(90, 70, 235, 35, 24, 0);
  const auto stamp_disc = [&pixels](float center_x, float center_y) {
    constexpr float radius = 2.6F;
    constexpr float edge_width = 0.9F;
    const auto min_x = std::max(0, static_cast<int>(std::floor(center_x - radius - edge_width)));
    const auto min_y = std::max(0, static_cast<int>(std::floor(center_y - radius - edge_width)));
    const auto max_x = std::min(pixels.width() - 1, static_cast<int>(std::ceil(center_x + radius + edge_width)));
    const auto max_y = std::min(pixels.height() - 1, static_cast<int>(std::ceil(center_y + radius + edge_width)));
    for (std::int32_t y = min_y; y <= max_y; ++y) {
      for (std::int32_t x = min_x; x <= max_x; ++x) {
        const auto dx = static_cast<float>(x) + 0.5F - center_x;
        const auto dy = static_cast<float>(y) + 0.5F - center_y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        const auto coverage = patchy::clamp_unit((radius + edge_width - distance) / (2.0F * edge_width));
        if (coverage <= 0.0F) {
          continue;
        }
        auto* px = pixels.pixel(x, y);
        px[3] = std::max(px[3], static_cast<std::uint8_t>(std::lround(coverage * 255.0F)));
      }
    }
  };

  for (int step = 0; step < 96; ++step) {
    const auto t = static_cast<float>(step) / 95.0F;
    const auto center_x = 45.0F + std::sin(t * 5.1F) * 20.0F;
    const auto center_y = 8.0F + t * 54.0F;
    stamp_disc(center_x, center_y);
  }

  patchy::Layer layer(document.allocate_layer_id(), "Soft Shadow Curve", std::move(pixels));
  auto& source = document.add_layer(std::move(layer));
  source.set_bounds(patchy::Rect{55, 25, 90, 70});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Normal;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 180.0F;
  shadow.distance = 0.0F;
  shadow.spread = 23.0F;
  shadow.size = 40.0F;
  source.layer_style().drop_shadows.push_back(shadow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  std::vector<int> intermediate_levels;
  int max_adjacent_gray_delta = 0;
  for (std::int32_t y = 5; y < flattened.height() - 5; ++y) {
    bool previous_gray = false;
    int previous_value = 255;
    for (std::int32_t x = 5; x < flattened.width() - 5; ++x) {
      const auto* px = flattened.pixel(x, y);
      const bool is_gray = std::abs(static_cast<int>(px[0]) - static_cast<int>(px[1])) <= 1 &&
                           std::abs(static_cast<int>(px[1]) - static_cast<int>(px[2])) <= 1;
      if (!is_gray) {
        previous_gray = false;
        previous_value = 255;
        continue;
      }

      const auto value = static_cast<int>(px[0]);
      if (value > 25 && value < 250) {
        intermediate_levels.push_back(value);
      }
      if (previous_gray && (value < 252 || previous_value < 252)) {
        max_adjacent_gray_delta = std::max(max_adjacent_gray_delta, std::abs(value - previous_value));
      }
      previous_gray = true;
      previous_value = value;
    }
  }
  std::sort(intermediate_levels.begin(), intermediate_levels.end());
  intermediate_levels.erase(std::unique(intermediate_levels.begin(), intermediate_levels.end()),
                            intermediate_levels.end());
  CHECK(intermediate_levels.size() >= 32U);
  CHECK(max_adjacent_gray_delta <= 35);
  write_rgb8_bmp_artifact("drop_shadow_soft_mask_smooth_falloff", flattened);
}

void compositor_outer_glow_preserves_source_alpha() {
  auto make_document = [](std::uint8_t alpha) {
    patchy::Document document(11, 11, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(11, 11, 0, 0, 0));
    patchy::Layer layer(document.allocate_layer_id(), "Glow Source", solid_rgba(1, 1, 20, 20, 20, alpha));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(patchy::Rect{5, 5, 1, 1});

    patchy::LayerOuterGlow glow;
    glow.enabled = true;
    glow.blend_mode = patchy::BlendMode::Normal;
    glow.color = patchy::RgbColor{255, 255, 255};
    glow.opacity = 1.0F;
    glow.size = 2.0F;
    source.layer_style().outer_glows.push_back(glow);
    return document;
  };

  const auto half_alpha = patchy::Compositor{}.flatten_rgb8(make_document(128));
  const auto opaque = patchy::Compositor{}.flatten_rgb8(make_document(255));
  const auto half_value = half_alpha.pixel(6, 5)[0];
  const auto opaque_value = opaque.pixel(6, 5)[0];
  CHECK(half_value > 5);
  CHECK(opaque_value > half_value + 6);
}

void compositor_outer_glow_antialias_strength_does_not_create_streaks() {
  patchy::Document document(56, 30, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(56, 30, 0, 0, 0));

  auto pixels = solid_rgba(14, 10, 255, 255, 255, 0);
  for (std::int32_t y = 1; y < 9; ++y) {
    pixels.pixel(1, y)[3] = 64;
  }
  for (std::int32_t index = 0; index < 6; ++index) {
    pixels.pixel(2 + index, 2 + index)[3] = 64;
  }
  for (std::int32_t y = 4; y < 7; ++y) {
    for (std::int32_t x = 8; x < 11; ++x) {
      pixels.pixel(x, y)[3] = 255;
    }
  }

  patchy::Layer styled_layer(document.allocate_layer_id(), "Antialias Source", std::move(pixels));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{24, 10, 14, 10});

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 255, 255};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 8.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* projected_edge_alpha = flattened.pixel(18, 14);
  CHECK(projected_edge_alpha[0] > 220);
  CHECK(projected_edge_alpha[1] > 220);
  CHECK(projected_edge_alpha[2] > 220);
}

void compositor_large_text_style_spread_keeps_rounded_silhouette() {
  patchy::Document document(240, 150, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(240, 150, 243, 237, 230));

  auto pixels = solid_rgba(180, 70, 255, 255, 255, 0);
  const auto fill_text_run = [&pixels](std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    for (std::int32_t py = y; py < y + height; ++py) {
      for (std::int32_t px = x; px < x + width; ++px) {
        auto* pixel = pixels.pixel(px, py);
        pixel[0] = 255;
        pixel[1] = 255;
        pixel[2] = 255;
        pixel[3] = 255;
      }
    }
  };
  fill_text_run(18, 18, 52, 7);
  fill_text_run(84, 18, 58, 7);
  fill_text_run(48, 36, 72, 7);
  fill_text_run(75, 54, 36, 7);

  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Text", std::move(pixels));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{40, 35, 180, 70});

  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = patchy::BlendMode::Multiply;
  shadow.color = patchy::RgbColor{0, 0, 0};
  shadow.opacity = 1.0F;
  shadow.angle_degrees = 90.0F;
  shadow.distance = 14.0F;
  shadow.spread = 74.0F;
  shadow.size = 20.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{184, 81, 74};
  glow.opacity = 1.0F;
  glow.spread = 100.0F;
  glow.size = 20.0F;
  layer.layer_style().outer_glows.push_back(glow);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* top_glow = flattened.pixel(80, 35);
  const auto* top_left_corner = flattened.pixel(45, 35);
  const auto* top_right_corner = flattened.pixel(198, 36);
  const auto* shadow_px = flattened.pixel(125, 106);

  CHECK(top_glow[0] >= 170);
  CHECK(top_glow[0] <= 200);
  CHECK(top_glow[1] < 120);
  CHECK(top_glow[2] < 120);
  CHECK(top_left_corner[0] > 235);
  CHECK(top_left_corner[1] > 229);
  CHECK(top_left_corner[2] > 222);
  CHECK(top_right_corner[0] > 235);
  CHECK(top_right_corner[1] > 229);
  CHECK(top_right_corner[2] > 222);
  CHECK(shadow_px[0] < 190);
  CHECK(shadow_px[1] < 185);
  CHECK(shadow_px[2] < 180);
}

void layer_style_spread_dilation_keeps_circular_radius() {
  constexpr int width = 7;
  constexpr int height = 7;
  std::vector<float> mask(static_cast<std::size_t>(width * height), 0.0F);
  mask[static_cast<std::size_t>(3 * width + 3)] = 1.0F;

  const auto dilated = patchy::render_detail::dilate_mask(mask, width, height, 2);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto dx = x - 3;
      const auto dy = y - 3;
      const auto inside_circle = dx * dx + dy * dy <= 4;
      const auto value = dilated[static_cast<std::size_t>(y * width + x)];
      CHECK(inside_circle ? value == 1.0F : value == 0.0F);
    }
  }
}

void visible_alpha_bounds_track_sparse_rgba_pixels() {
  auto pixels = solid_rgba(8, 6, 0, 0, 0, 0);
  pixels.pixel(2, 1)[3] = 1;
  pixels.pixel(5, 4)[3] = 128;

  const auto local_bounds = patchy::visible_alpha_local_bounds(pixels);
  CHECK(local_bounds.has_value());
  CHECK(local_bounds->x == 2);
  CHECK(local_bounds->y == 1);
  CHECK(local_bounds->width == 4);
  CHECK(local_bounds->height == 4);

  patchy::Layer layer(1, "Sparse", pixels);
  const auto document_bounds = patchy::layer_visible_alpha_bounds(layer, patchy::Rect{12, 20, 8, 6});
  CHECK(document_bounds.has_value());
  CHECK(document_bounds->x == 14);
  CHECK(document_bounds->y == 21);
  CHECK(document_bounds->width == 4);
  CHECK(document_bounds->height == 4);

  const auto moved_bounds = patchy::layer_visible_alpha_bounds(layer, patchy::Rect{30, 40, 8, 6});
  CHECK(moved_bounds.has_value());
  CHECK(moved_bounds->x == 32);
  CHECK(moved_bounds->y == 41);

  auto& mutable_pixels = layer.pixels();
  mutable_pixels.pixel(2, 1)[3] = 0;
  mutable_pixels.pixel(7, 5)[3] = 255;
  const auto edited_bounds = patchy::layer_visible_alpha_bounds(layer, patchy::Rect{12, 20, 8, 6});
  CHECK(edited_bounds.has_value());
  CHECK(edited_bounds->x == 17);
  CHECK(edited_bounds->y == 24);
  CHECK(edited_bounds->width == 3);
  CHECK(edited_bounds->height == 2);

  auto override_pixels = solid_rgba(8, 6, 0, 0, 0, 0);
  override_pixels.pixel(1, 2)[3] = 255;
  const auto override_bounds =
      patchy::layer_visible_alpha_bounds(layer, override_pixels, patchy::Rect{12, 20, 8, 6});
  CHECK(override_bounds.has_value());
  CHECK(override_bounds->x == 13);
  CHECK(override_bounds->y == 22);
  CHECK(override_bounds->width == 1);
  CHECK(override_bounds->height == 1);

  CHECK(!patchy::visible_alpha_local_bounds(solid_rgba(3, 2, 0, 0, 0, 0)).has_value());
  const auto opaque_rgb_bounds = patchy::visible_alpha_local_bounds(solid_rgb(4, 3, 10, 20, 30));
  CHECK(opaque_rgb_bounds.has_value());
  CHECK(opaque_rgb_bounds->x == 0);
  CHECK(opaque_rgb_bounds->y == 0);
  CHECK(opaque_rgb_bounds->width == 4);
  CHECK(opaque_rgb_bounds->height == 3);
}

void layer_style_preview_marks_large_or_broad_effects_expensive() {
  patchy::Layer plain_layer(1, "Plain", solid_rgba(10, 10, 20, 20, 20, 255));
  plain_layer.set_bounds(patchy::Rect{100, 100, 10, 10});
  CHECK(!patchy::layer_style_preview_is_expensive(plain_layer, patchy::Rect::from_size(1000, 1000)));

  patchy::Layer large_glow_layer(2, "Large Glow", solid_rgba(10, 10, 20, 20, 20, 255));
  large_glow_layer.set_bounds(patchy::Rect{100, 100, 10, 10});
  patchy::LayerOuterGlow large_glow;
  large_glow.enabled = true;
  large_glow.opacity = 1.0F;
  large_glow.size = 221.0F;
  large_glow.spread = 14.0F;
  large_glow_layer.layer_style().outer_glows.push_back(large_glow);
  CHECK(patchy::layer_style_preview_is_expensive(large_glow_layer, patchy::Rect::from_size(1000, 1000)));

  patchy::Layer broad_layer(3, "Broad", solid_rgba(50, 50, 20, 20, 20, 255));
  broad_layer.set_bounds(patchy::Rect{10, 10, 50, 50});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.opacity = 1.0F;
  stroke.size = 1.0F;
  broad_layer.layer_style().strokes.push_back(stroke);
  CHECK(patchy::layer_style_preview_is_expensive(broad_layer, patchy::Rect::from_size(100, 100)));
}

void compositor_renders_sparse_outer_glow_from_visible_alpha_bounds() {
  const auto make_document = [](bool cropped_source) {
    patchy::Document document(160, 120, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(160, 120, 255, 255, 255));

    patchy::PixelBuffer pixels = cropped_source ? solid_rgba(8, 8, 20, 20, 220, 255)
                                                : solid_rgba(90, 50, 0, 0, 0, 0);
    if (!cropped_source) {
      for (std::int32_t y = 15; y < 23; ++y) {
        for (std::int32_t x = 35; x < 43; ++x) {
          auto* px = pixels.pixel(x, y);
          px[0] = 20;
          px[1] = 20;
          px[2] = 220;
          px[3] = 255;
        }
      }
    }

    patchy::Layer layer(document.allocate_layer_id(), "Glow", std::move(pixels));
    layer.set_bounds(cropped_source ? patchy::Rect{65, 35, 8, 8} : patchy::Rect{30, 20, 90, 50});
    patchy::LayerOuterGlow glow;
    glow.enabled = true;
    glow.blend_mode = patchy::BlendMode::Normal;
    glow.color = patchy::RgbColor{255, 0, 0};
    glow.opacity = 0.85F;
    glow.spread = 25.0F;
    glow.size = 18.0F;
    layer.layer_style().outer_glows.push_back(glow);
    document.add_layer(std::move(layer));
    return document;
  };

  const auto sparse = patchy::Compositor{}.flatten_rgb8(make_document(false));
  const auto cropped = patchy::Compositor{}.flatten_rgb8(make_document(true));
  for (std::int32_t y = 0; y < sparse.height(); ++y) {
    for (std::int32_t x = 0; x < sparse.width(); ++x) {
      const auto* sparse_px = sparse.pixel(x, y);
      const auto* cropped_px = cropped.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        CHECK(std::abs(static_cast<int>(sparse_px[channel]) - static_cast<int>(cropped_px[channel])) <= 1);
      }
    }
  }
}

void compositor_renders_sparse_drop_shadow_from_visible_alpha_bounds() {
  const auto make_document = [](bool cropped_source) {
    patchy::Document document(160, 120, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(160, 120, 255, 255, 255));

    patchy::PixelBuffer pixels = cropped_source ? solid_rgba(8, 8, 30, 80, 220, 255)
                                                : solid_rgba(90, 50, 0, 0, 0, 0);
    if (!cropped_source) {
      for (std::int32_t y = 15; y < 23; ++y) {
        for (std::int32_t x = 35; x < 43; ++x) {
          auto* px = pixels.pixel(x, y);
          px[0] = 30;
          px[1] = 80;
          px[2] = 220;
          px[3] = 255;
        }
      }
    }

    patchy::Layer layer(document.allocate_layer_id(), "Shadow", std::move(pixels));
    layer.set_bounds(cropped_source ? patchy::Rect{65, 35, 8, 8} : patchy::Rect{30, 20, 90, 50});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{0, 0, 0};
    shadow.opacity = 0.85F;
    shadow.angle_degrees = 135.0F;
    shadow.distance = 7.0F;
    shadow.size = 18.0F;
    shadow.spread = 25.0F;
    layer.layer_style().drop_shadows.push_back(shadow);
    document.add_layer(std::move(layer));
    return document;
  };

  const auto sparse = patchy::Compositor{}.flatten_rgb8(make_document(false));
  const auto cropped = patchy::Compositor{}.flatten_rgb8(make_document(true));
  for (std::int32_t y = 0; y < sparse.height(); ++y) {
    for (std::int32_t x = 0; x < sparse.width(); ++x) {
      const auto* sparse_px = sparse.pixel(x, y);
      const auto* cropped_px = cropped.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        CHECK(std::abs(static_cast<int>(sparse_px[channel]) - static_cast<int>(cropped_px[channel])) <= 1);
      }
    }
  }
}

void compositor_renders_layer_style_color_overlay() {
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(4, 4, 255, 255, 255));

  auto& layer = document.add_pixel_layer("Overridden", solid_rgba(4, 4, 20, 40, 220, 255));
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{240, 60, 20};
  overlay.opacity = 1.0F;
  layer.layer_style().color_overlays.push_back(overlay);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(1, 1);
  CHECK(px[0] == 240);
  CHECK(px[1] == 60);
  CHECK(px[2] == 20);
}

}  // namespace

std::vector<patchy::test::TestCase> compositor_layer_styles_tests() {
  return {
      {"compositor_renders_layer_style_drop_shadow_gradient_and_stroke",
       compositor_renders_layer_style_drop_shadow_gradient_and_stroke},
      {"compositor_renders_layer_style_bevel_emboss", compositor_renders_layer_style_bevel_emboss},
      {"compositor_renders_bevel_across_thin_checkbox_edges",
       compositor_renders_bevel_across_thin_checkbox_edges},
      {"compositor_bevel_styles_techniques_and_soften_are_distinct",
       compositor_bevel_styles_techniques_and_soften_are_distinct},
      {"psd_bevel_styles_techniques_and_soften_round_trip",
       psd_bevel_styles_techniques_and_soften_round_trip},
      {"compositor_renders_layer_style_outer_glow", compositor_renders_layer_style_outer_glow},
      {"compositor_outer_glow_spread_stays_within_size",
       compositor_outer_glow_spread_stays_within_size},
      {"compositor_outer_glow_spread_uses_circular_distance_field",
       compositor_outer_glow_spread_uses_circular_distance_field},
      {"compositor_drop_shadow_preserves_source_alpha",
       compositor_drop_shadow_preserves_source_alpha},
      {"compositor_drop_shadow_preserves_connected_antialias_alpha",
       compositor_drop_shadow_preserves_connected_antialias_alpha},
      {"compositor_drop_shadow_soft_mask_has_smooth_falloff",
       compositor_drop_shadow_soft_mask_has_smooth_falloff},
      {"compositor_outer_glow_preserves_source_alpha",
       compositor_outer_glow_preserves_source_alpha},
      {"compositor_outer_glow_antialias_strength_does_not_create_streaks",
       compositor_outer_glow_antialias_strength_does_not_create_streaks},
      {"compositor_large_text_style_spread_keeps_rounded_silhouette",
       compositor_large_text_style_spread_keeps_rounded_silhouette},
      {"layer_style_spread_dilation_keeps_circular_radius",
       layer_style_spread_dilation_keeps_circular_radius},
      {"visible_alpha_bounds_track_sparse_rgba_pixels", visible_alpha_bounds_track_sparse_rgba_pixels},
      {"layer_style_preview_marks_large_or_broad_effects_expensive",
       layer_style_preview_marks_large_or_broad_effects_expensive},
      {"compositor_renders_sparse_outer_glow_from_visible_alpha_bounds",
       compositor_renders_sparse_outer_glow_from_visible_alpha_bounds},
      {"compositor_renders_sparse_drop_shadow_from_visible_alpha_bounds",
       compositor_renders_sparse_drop_shadow_from_visible_alpha_bounds},
      {"compositor_renders_layer_style_color_overlay", compositor_renders_layer_style_color_overlay},
  };
}
