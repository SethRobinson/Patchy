#include <string_view>
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_render_utils.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
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

// Patchy's continuous (long) shadow: the matte is swept from the layer out to
// the offset, so a hard shadow forms one unbroken band, Fade thins it toward
// the far end, and a diagonal offset follows its digital line.
// Photoshop's 30000 px Distance: every shadow path must stay bounded by the
// canvas and the layer instead of allocating by the offset. Inner shadows
// past the layer's extent are fully shadowed, long shadows sweep across the
// whole canvas, and styled groups (isolated and pass-through) render their
// children unchanged when the shadow lands off-canvas.
void compositor_shadow_distance_30000_stays_bounded() {
  constexpr float kFar = 30000.0F;
  const auto red = [](const patchy::PixelBuffer& pixels, int x, int y) {
    return static_cast<int>(pixels.pixel(x, y)[0]);
  };

  // Inner shadow: a 10x10 red layer; past 10 + size + 2 px every interior
  // sample reads outside the layer, so the whole layer is shadowed black.
  const auto inner = [](float distance) {
    patchy::Document document(48, 16, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(48, 16, 255, 255, 255));
    patchy::Layer layer(document.allocate_layer_id(), "Bar", solid_rgba(10, 10, 220, 20, 20, 255));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(patchy::Rect{4, 3, 10, 10});
    patchy::LayerInnerShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{0, 0, 0};
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 180.0F;
    shadow.distance = distance;
    shadow.size = 2.0F;
    source.layer_style().inner_shadows.push_back(shadow);
    return patchy::Compositor{}.flatten_rgb8(document);
  };
  for (const auto distance : {20.0F, kFar}) {
    const auto shadowed = inner(distance);
    for (int y = 3; y < 13; ++y) {
      for (int x = 4; x < 14; ++x) {
        CHECK(red(shadowed, x, y) == 0);
      }
    }
    CHECK(red(shadowed, 2, 8) == 255);
  }
  // A short offset still shades only the leading edge.
  const auto short_offset = inner(3.0F);
  CHECK(red(short_offset, 5, 8) < 60);
  CHECK(red(short_offset, 12, 8) == 220);

  // Long (continuous) drop shadow at 30000 px toward +x: everything right of
  // the bar on its rows is swept, the rest of the canvas is untouched.
  {
    patchy::Document document(48, 16, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(48, 16, 255, 255, 255));
    patchy::Layer layer(document.allocate_layer_id(), "Bar", solid_rgba(2, 4, 220, 20, 20, 255));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(patchy::Rect{4, 4, 2, 4});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{0, 0, 0};
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 180.0F;
    shadow.distance = kFar;
    shadow.size = 0.0F;
    shadow.continuous = true;
    source.layer_style().drop_shadows.push_back(shadow);
    const auto swept = patchy::Compositor{}.flatten_rgb8(document);
    for (int x = 6; x < 48; ++x) {
      CHECK(red(swept, x, 5) == 0);
    }
    CHECK(red(swept, 3, 5) == 255);
    CHECK(red(swept, 20, 3) == 255);
  }

  // Styled groups, pass-through and isolated: a 30000 px shadow lands
  // off-canvas and the child renders exactly as it does without the style.
  for (const auto mode : {patchy::BlendMode::PassThrough, patchy::BlendMode::Normal}) {
    const auto render = [mode](bool styled) {
      patchy::Document document(24, 12, patchy::PixelFormat::rgb8());
      document.add_pixel_layer("Base", solid_rgb(24, 12, 255, 255, 255));
      patchy::Layer group(document.allocate_layer_id(), "Group", patchy::LayerKind::Group);
      group.set_blend_mode(mode);
      patchy::Layer child(document.allocate_layer_id(), "Child", solid_rgba(6, 4, 30, 140, 200, 255));
      child.set_bounds(patchy::Rect{8, 4, 6, 4});
      group.add_child(std::move(child));
      if (styled) {
        patchy::LayerDropShadow shadow;
        shadow.enabled = true;
        shadow.opacity = 1.0F;
        shadow.angle_degrees = 90.0F;
        shadow.distance = kFar;
        shadow.size = 4.0F;
        group.layer_style().drop_shadows.push_back(shadow);
      }
      document.add_layer(std::move(group));
      return patchy::Compositor{}.flatten_rgb8(document);
    };
    const auto plain = render(false);
    const auto styled = render(true);
    CHECK(std::equal(plain.data().begin(), plain.data().end(), styled.data().begin(), styled.data().end()));
  }
}

void compositor_continuous_drop_shadow_sweeps_to_offset_and_fades() {
  const auto render = [](bool continuous, float fade, float angle, float distance, patchy::Rect bounds) {
    patchy::Document document(48, 16, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(48, 16, 255, 255, 255));
    patchy::Layer layer(document.allocate_layer_id(), "Bar",
                        solid_rgba(bounds.width, bounds.height, 220, 20, 20, 255));
    auto& source = document.add_layer(std::move(layer));
    source.set_bounds(bounds);
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{0, 0, 0};
    shadow.opacity = 1.0F;
    shadow.angle_degrees = angle;
    shadow.distance = distance;
    shadow.size = 0.0F;
    shadow.spread = 0.0F;
    shadow.continuous = continuous;
    shadow.fade = fade;
    source.layer_style().drop_shadows.push_back(shadow);
    return patchy::Compositor{}.flatten_rgb8(document);
  };
  const auto red = [](const patchy::PixelBuffer& pixels, int x, int y) { return static_cast<int>(pixels.pixel(x, y)[0]); };

  // Angle 180 casts toward +x: a 2x4 bar at x 4..5 swept 20 px fills x 6..25.
  const auto bar = patchy::Rect{4, 4, 2, 4};
  const auto plain = render(false, 0.0F, 180.0F, 20.0F, bar);
  CHECK(red(plain, 15, 5) == 255);
  CHECK(red(plain, 24, 5) < 20);
  CHECK(red(plain, 25, 5) < 20);
  const auto swept = render(true, 0.0F, 180.0F, 20.0F, bar);
  for (int x = 6; x <= 25; ++x) {
    CHECK(red(swept, x, 5) < 20);
    CHECK(red(swept, x, 7) < 20);
  }
  CHECK(red(swept, 26, 5) == 255);
  CHECK(red(swept, 3, 5) == 255);
  CHECK(red(swept, 15, 3) == 255);
  CHECK(red(swept, 15, 8) == 255);
  // Fade 100: near the layer the sweep is still almost solid, by the far end
  // it has nearly vanished, and it never overshoots the plain reach.
  const auto faded = render(true, 100.0F, 180.0F, 20.0F, bar);
  CHECK(red(faded, 7, 5) < 60);
  CHECK(red(faded, 15, 5) > red(faded, 7, 5));
  CHECK(red(faded, 24, 5) > 200);
  CHECK(red(faded, 26, 5) == 255);
  // Angle 135 casts toward +x +y: a 3x3 square at (2, 2) swept by (10, 10)
  // darkens the diagonal band and nothing beside it.
  const auto diagonal = render(true, 0.0F, 135.0F, 14.0F, patchy::Rect{2, 2, 3, 3});
  CHECK(red(diagonal, 8, 8) < 20);
  CHECK(red(diagonal, 13, 13) < 20);
  CHECK(red(diagonal, 8, 3) == 255);
  CHECK(red(diagonal, 3, 8) == 255);
  CHECK(red(diagonal, 15, 15) == 255);
  // A zero offset degenerates to the plain path: the shadow hides under the
  // opaque layer, so the backdrop beside it stays untouched.
  const auto zero = render(true, 0.0F, 180.0F, 0.0F, bar);
  CHECK(red(zero, 6, 5) == 255);
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
  // Grayscale-dilation spread (the COM-calibrated Softer model): the alpha-64
  // strip projects its own strength times the default Range-50 doubling
  // (~0.5), not the component's full strength, and the band stays streak-free
  // along the strip. The solid block still projects at full strength.
  const auto* projected_edge_alpha = flattened.pixel(18, 14);
  CHECK(projected_edge_alpha[0] > 100);
  CHECK(projected_edge_alpha[0] < 170);
  const auto* projected_along_strip = flattened.pixel(18, 16);
  CHECK(std::abs(static_cast<int>(projected_edge_alpha[0]) - static_cast<int>(projected_along_strip[0])) <= 2);
  const auto* projected_block_alpha = flattened.pixel(26, 15);
  CHECK(projected_block_alpha[0] > 220);
  CHECK(projected_block_alpha[1] > 220);
  CHECK(projected_block_alpha[2] > 220);
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

void compositor_interior_overlay_covers_fill_under_partial_layer_opacity() {
  // Photoshop resolves interior overlays INTO the layer's own color and applies
  // layer Opacity once to that result, so a 100%/Normal overlay hides the
  // layer's own pixels outright at any opacity. Compositing the overlay as its
  // own destination pass scaled by layer.opacity() instead left (1 - opacity)
  // of the fill showing through: Game_Screen.psd's crimson panels under a 100%
  // stucco Pattern Overlay at opacity 196/255 bled red/purple.
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(4, 4, 255, 255, 255));

  auto& layer = document.add_pixel_layer("Panel", solid_rgba(4, 4, 255, 67, 92, 255));
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{0, 0, 255};
  overlay.opacity = 1.0F;
  layer.layer_style().color_overlays.push_back(overlay);
  layer.set_opacity(0.5F);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(1, 1);
  // Half the overlay over white, with no trace of the crimson fill. The old
  // double-scaled pass rendered (128, 81, 215) here.
  CHECK(std::abs(static_cast<int>(px[0]) - 128) <= 1);
  CHECK(std::abs(static_cast<int>(px[1]) - 128) <= 1);
  CHECK(std::abs(static_cast<int>(px[2]) - 255) <= 1);

  // At full opacity the overlay replaces the fill exactly, as it always has.
  layer.set_opacity(1.0F);
  const auto opaque = patchy::Compositor{}.flatten_rgb8(document);
  const auto* opaque_px = opaque.pixel(1, 1);
  CHECK(opaque_px[0] == 0 && opaque_px[1] == 0 && opaque_px[2] == 255);
}

void compositor_interior_overlay_knocks_out_semi_transparent_fill() {
  // The overlay is clipped to the layer's alpha and replaces the content within
  // it, so a half-alpha fill under a 100%/Normal overlay renders the overlay at
  // half coverage over the backdrop - not a wash of overlay over fill over
  // backdrop, which is what the separate destination pass produced.
  patchy::Document document(4, 4, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(4, 4, 255, 255, 255));

  auto& layer = document.add_pixel_layer("Panel", solid_rgba(4, 4, 255, 67, 92, 128));
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{0, 0, 255};
  overlay.opacity = 1.0F;
  layer.layer_style().color_overlays.push_back(overlay);

  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto* px = flattened.pixel(1, 1);
  CHECK(std::abs(static_cast<int>(px[0]) - 127) <= 1);
  CHECK(std::abs(static_cast<int>(px[1]) - 127) <= 1);
  CHECK(std::abs(static_cast<int>(px[2]) - 255) <= 1);
}

// Share of a probe's painted pixels allowed to differ from Photoshop by more than 24.
constexpr double kShapeEffectProbeTolerance = 0.02;
// Share of a stroke-alpha probe's square allowed to differ from Photoshop by more than 8.
constexpr double kStrokeAlphaProbeTolerance = 0.06;

patchy::Layer make_stroked_shape_layer(patchy::Document& document, bool fill_enabled) {
  patchy::Layer shape(document.allocate_layer_id(), "Shape", patchy::PixelBuffer());
  patchy::VectorShapeContent content;
  patchy::PathSubpath rect;
  for (const auto& [x, y] : {std::pair{24.0, 24.0}, {72.0, 24.0}, {72.0, 72.0}, {24.0, 72.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    rect.anchors.push_back(anchor);
  }
  content.path.subpaths.push_back(rect);
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{30, 60, 220};
  content.stroke.enabled = true;
  content.stroke.width = 10.0;
  content.stroke.alignment = patchy::VectorStrokeAlignment::Center;
  content.stroke.fill_enabled = fill_enabled;
  content.stroke.content.kind = patchy::VectorFillKind::Solid;
  content.stroke.content.color = patchy::RgbColor{20, 200, 60};
  shape.set_vector_shape(content);
  shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(96, 96),
                                     &document.metadata().patterns);
  return shape;
}

void compositor_interior_overlay_stays_under_vector_stroke() {
  // PS 2026 probes (fx-sofi-center/outside/nofill, docs/vector-tools.md):
  // interior overlays cover a stroked shape layer's FILL only, with the
  // vector stroke composited above; a stroke-only shape's overlay covers the
  // stroke itself.
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{255, 0, 0};
  overlay.opacity = 1.0F;
  {
    patchy::Document document(96, 96, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(96, 96, 255, 255, 255));
    auto& layer = document.add_layer(make_stroked_shape_layer(document, true));
    layer.layer_style().color_overlays.push_back(overlay);
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    // Fill center: the overlay covers the fill.
    CHECK(flattened.pixel(48, 48)[0] == 255);
    CHECK(flattened.pixel(48, 48)[1] == 0);
    // Stroke band (centered width 10 on the y=24 edge spans 19..29): the
    // green vector stroke stays above the overlay, inner and outer halves.
    CHECK(flattened.pixel(48, 21)[1] == 200);
    CHECK(flattened.pixel(48, 26)[1] == 200);
    CHECK(flattened.pixel(48, 21)[0] == 20);
    // Outside stays the backdrop.
    CHECK(flattened.pixel(8, 8)[0] == 255);
    CHECK(flattened.pixel(8, 8)[2] == 255);
  }
  {
    // Stroke-only shape (fill disabled): the overlay covers the stroke (the
    // legacy combined path, matching fx-sofi-nofill).
    patchy::Document document(96, 96, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(96, 96, 255, 255, 255));
    auto& layer = document.add_layer(make_stroked_shape_layer(document, false));
    layer.layer_style().color_overlays.push_back(overlay);
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    CHECK(flattened.pixel(48, 21)[0] == 255);
    CHECK(flattened.pixel(48, 21)[1] == 0);
    // The disabled fill leaves the interior showing the backdrop.
    CHECK(flattened.pixel(48, 48)[0] == 255);
    CHECK(flattened.pixel(48, 48)[1] == 255);
  }
}

// A 48 px square (24..72) whose gradient fill runs left to right and is fully transparent
// over its right half: the shape covers pixels the fill does not paint.
patchy::Layer make_half_transparent_gradient_shape(patchy::Document& document, bool solid_instead = false) {
  patchy::Layer shape(document.allocate_layer_id(), "Shape", patchy::PixelBuffer());
  patchy::VectorShapeContent content;
  patchy::PathSubpath rect;
  for (const auto& [x, y] : {std::pair{24.0, 24.0}, {72.0, 24.0}, {72.0, 72.0}, {24.0, 72.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    rect.anchors.push_back(anchor);
  }
  content.path.subpaths.push_back(rect);
  if (solid_instead) {
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{255, 0, 0};
  } else {
    content.fill.kind = patchy::VectorFillKind::Gradient;
    content.fill.gradient.angle_degrees = 0.0F;
    content.fill.gradient.color_stops = {{0.0F, patchy::RgbColor{255, 0, 0}}, {1.0F, patchy::RgbColor{255, 0, 0}}};
    content.fill.gradient.alpha_stops = {{0.0F, 1.0F}, {0.5F, 0.0F}, {1.0F, 0.0F}};
  }
  shape.set_vector_shape(content);
  shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(96, 96), &document.metadata().patterns);
  return shape;
}

void compositor_effects_follow_shape_coverage_under_transparent_fill() {
  // Photoshop 2026 probes on psd-tools' stroke-effects.psd (October 2026,
  // docs/layer-effects-render.md "Effect silhouette of shape layers"): on a shape layer
  // every effect follows the SHAPE, at full strength, where its gradient fill is
  // transparent. Patchy keyed them off the painted alpha, so the squares of that file
  // came out with triangular strokes.
  const patchy::RgbColor blue{0, 0, 255};
  const auto is_blue = [](const std::uint8_t* px) { return px[0] == 0 && px[1] == 0 && px[2] == 255; };
  const auto is_white = [](const std::uint8_t* px) { return px[0] == 255 && px[1] == 255 && px[2] == 255; };
  const auto render = [&](auto&& style_layer, bool solid = false) {
    patchy::Document document(96, 96, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Base", solid_rgb(96, 96, 255, 255, 255));
    auto& layer = document.add_layer(make_half_transparent_gradient_shape(document, solid));
    style_layer(layer);
    return patchy::Compositor{}.flatten_rgb8(document);
  };
  {
    // The bake: a silhouette plane exists only when coverage and painted alpha differ.
    patchy::Document document(96, 96, patchy::PixelFormat::rgb8());
    const auto gradient = make_half_transparent_gradient_shape(document);
    const auto* shape = std::as_const(gradient).vector_shape();
    CHECK(shape != nullptr && !shape->effect_matte_cache.empty());
    if (shape != nullptr && !shape->effect_matte_cache.empty()) {
      const auto bounds = gradient.bounds();
      CHECK(shape->effect_matte_cache.width() == std::as_const(gradient).pixels().width());
      CHECK(std::as_const(gradient).pixels().pixel(66 - bounds.x, 48 - bounds.y)[3] == 0);
      CHECK(std::as_const(shape->effect_matte_cache).pixel(66 - bounds.x, 48 - bounds.y)[3] == 255);
      CHECK(std::as_const(shape->effect_matte_cache).pixel(30 - bounds.x, 48 - bounds.y)[3] == 255);
    }
    const auto solid = make_half_transparent_gradient_shape(document, true);
    CHECK(std::as_const(solid).vector_shape()->effect_matte_cache.empty());
  }
  {
    // No effects: the transparent half shows the backdrop, as before.
    const auto flattened = render([](patchy::Layer&) {});
    CHECK(is_white(flattened.pixel(66, 48)));
    CHECK(flattened.pixel(26, 48)[0] == 255 && flattened.pixel(26, 48)[1] < 24);
  }
  for (const auto position : {patchy::LayerStrokePosition::Outside, patchy::LayerStrokePosition::Inside}) {
    const auto flattened = render([&](patchy::Layer& layer) {
      patchy::LayerStroke stroke;
      stroke.enabled = true;
      stroke.color = blue;
      stroke.size = 4.0F;
      stroke.position = position;
      layer.layer_style().strokes.push_back(stroke);
    });
    // The band runs along the whole square, including the edge the fill never reaches.
    const int x = position == patchy::LayerStrokePosition::Outside ? 74 : 70;
    CHECK(is_blue(flattened.pixel(x, 48)));
    CHECK(is_blue(flattened.pixel(x, 30)));
    CHECK(is_blue(flattened.pixel(60, position == patchy::LayerStrokePosition::Outside ? 74 : 70)));
    // And nothing else lands inside the transparent half.
    CHECK(is_white(flattened.pixel(60, 48)));
  }
  {
    // A 100% Normal Color Overlay makes the whole square opaque.
    const auto flattened = render([&](patchy::Layer& layer) {
      patchy::LayerColorOverlay overlay;
      overlay.enabled = true;
      overlay.blend_mode = patchy::BlendMode::Normal;
      overlay.color = blue;
      overlay.opacity = 1.0F;
      layer.layer_style().color_overlays.push_back(overlay);
    });
    CHECK(is_blue(flattened.pixel(66, 48)));
    CHECK(is_blue(flattened.pixel(30, 48)));
    CHECK(is_white(flattened.pixel(76, 48)));
  }
  for (const bool conceals : {true, false}) {
    // A hard drop shadow thrown to the right is the square's shadow; "Layer Knocks Out
    // Drop Shadow" hides it under the whole square, transparent half included.
    const auto flattened = render([&](patchy::Layer& layer) {
      patchy::LayerDropShadow shadow;
      shadow.enabled = true;
      shadow.blend_mode = patchy::BlendMode::Normal;
      shadow.color = blue;
      shadow.opacity = 1.0F;
      shadow.angle_degrees = 180.0F;
      shadow.distance = 5.0F;
      shadow.size = 0.0F;
      shadow.layer_conceals = conceals;
      layer.layer_style().drop_shadows.push_back(shadow);
    });
    CHECK(is_blue(flattened.pixel(74, 48)));
    CHECK(is_blue(flattened.pixel(74, 70)));
    CHECK(conceals ? is_white(flattened.pixel(66, 48)) : is_blue(flattened.pixel(66, 48)));
  }
  {
    // A solid fill is untouched by all of this: the stroke is where it always was.
    const auto flattened = render(
        [&](patchy::Layer& layer) {
          patchy::LayerStroke stroke;
          stroke.enabled = true;
          stroke.color = blue;
          stroke.size = 4.0F;
          layer.layer_style().strokes.push_back(stroke);
        },
        true);
    CHECK(is_blue(flattened.pixel(74, 48)));
    CHECK(flattened.pixel(66, 48)[0] == 255 && flattened.pixel(66, 48)[1] == 0);
  }
}

// The Photoshop probes behind the rule above, when present: one shape layer of psd-tools'
// stroke-effects.psd saved by Photoshop 2026 with one effect each (scratch script
// fxprobe/probe.jsx, kept in local-test-fixtures/shape-fx-probes). Each file's own
// composite is Photoshop's render; Patchy's render of the layers has to agree with it.
void shape_effect_silhouette_matches_photoshop_probes_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "shape-fx-probes";
  patchy::test::skip_unless_exists(root);
  int compared = 0;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (entry.path().extension() != ".psd") {
      continue;
    }
    patchy::psd::ReadOptions flat_options;
    flat_options.prefer_flat_composite = true;
    // Patchy's render matted on white paper: the probes have no background layer.
    const auto over_white = [](const patchy::Document& document) {
      std::vector<std::uint8_t> alpha;
      auto flattened = patchy::Compositor{}.flatten_rgb8(document, &alpha);
      for (std::int32_t y = 0; y < flattened.height(); ++y) {
        for (std::int32_t x = 0; x < flattened.width(); ++x) {
          auto* px = flattened.pixel(x, y);
          const int a = alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(flattened.width()) +
                              static_cast<std::size_t>(x)];
          for (int channel = 0; channel < 3; ++channel) {
            px[channel] = static_cast<std::uint8_t>((px[channel] * a + 255 * (255 - a) + 127) / 255);
          }
        }
      }
      return flattened;
    };
    // (Photoshop stores the composite of a document without a background already
    // blended with white, so that side is taken as it is.)
    std::vector<std::uint8_t> expected_alpha;
    auto expected = patchy::Compositor{}.flatten_rgb8(patchy::psd::DocumentIo::read_file(entry.path(), flat_options),
                                                      &expected_alpha);
    for (std::int32_t y = 0; y < expected.height(); ++y) {
      for (std::int32_t x = 0; x < expected.width(); ++x) {
        if (expected_alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(expected.width()) +
                           static_cast<std::size_t>(x)] == 0) {
          auto* px = expected.pixel(x, y);
          px[0] = px[1] = px[2] = 255;
        }
      }
    }
    const auto actual = over_white(patchy::psd::DocumentIo::read_file(entry.path()));
    CHECK(expected.width() == actual.width() && expected.height() == actual.height());
    if (expected.width() != actual.width() || expected.height() != actual.height()) {
      continue;
    }
    // The area either render paints, and how much of it is visibly different.
    std::int64_t painted = 0;
    std::int64_t wrong = 0;
    for (std::int32_t y = 0; y < expected.height(); ++y) {
      for (std::int32_t x = 0; x < expected.width(); ++x) {
        const auto* a = expected.pixel(x, y);
        const auto* b = actual.pixel(x, y);
        const auto is_paper = [](const std::uint8_t* px) { return px[0] > 250 && px[1] > 250 && px[2] > 250; };
        if (is_paper(a) && is_paper(b)) {
          continue;
        }
        ++painted;
        const int delta = std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
        if (delta > 24) {
          ++wrong;
        }
      }
    }
    const double share = painted == 0 ? 1.0 : static_cast<double>(wrong) / static_cast<double>(painted);
    std::cout << "  " << entry.path().filename().string() << ": " << wrong << " of " << painted << " painted pixels off ("
              << share * 100.0 << "%)\n";
    CHECK(painted > 0);
    CHECK(share <= kShapeEffectProbeTolerance);
    ++compared;
  }
  CHECK(compared > 0);
}

// Photoshop 2026 probes of the Stroke effect on semi-transparent content (scratch script
// fxprobe/stroke_alpha_probe.jsx, kept in local-test-fixtures/stroke-alpha-probes): a red
// square whose alpha ramps 1 -> 0, or sits flat at 50%, over a light blue background,
// one stroke variant per file (position, stroke opacity, layer opacity, Fill Opacity,
// Overprint, Multiply). The composite in each file is Photoshop's render.
void stroke_on_semi_transparent_content_matches_photoshop_probes_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "stroke-alpha-probes";
  patchy::test::skip_unless_exists(root);
  int compared = 0;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (entry.path().extension() != ".psd") {
      continue;
    }
    patchy::psd::ReadOptions flat_options;
    flat_options.prefer_flat_composite = true;
    const auto expected =
        patchy::Compositor{}.flatten_rgb8(patchy::psd::DocumentIo::read_file(entry.path(), flat_options));
    const auto actual = patchy::Compositor{}.flatten_rgb8(patchy::psd::DocumentIo::read_file(entry.path()));
    CHECK(expected.width() == actual.width() && expected.height() == actual.height());
    if (expected.width() != actual.width() || expected.height() != actual.height()) {
      continue;
    }
    // The square and its band: x 10..54, y 10..54. Count pixels off by more than 8.
    std::int64_t wrong = 0;
    int worst = 0;
    for (std::int32_t y = 10; y < 54; ++y) {
      for (std::int32_t x = 10; x < 54; ++x) {
        const auto* a = expected.pixel(x, y);
        const auto* b = actual.pixel(x, y);
        const int delta = std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
        worst = std::max(worst, delta);
        if (delta > 8) {
          ++wrong;
        }
      }
    }
    const double share = static_cast<double>(wrong) / (44.0 * 44.0);
    std::cout << "  " << entry.path().filename().string() << ": " << wrong << " pixels off by more than 8 ("
              << share * 100.0 << "%), worst " << worst << "\n";
    // The AA circle keeps a wider margin: its Center stroke's ring was 10% off
    // before this model and still is (the subpixel band placement, not the fold).
    const bool aa_probe = entry.path().filename().string().rfind("aa_", 0) == 0;
    CHECK(share <= (aa_probe ? 0.12 : kStrokeAlphaProbeTolerance));
    ++compared;
  }
  CHECK(compared > 0);
}

// Soft-edged strokes (GitHub issue 261): Photoshop 2026 authored and rendered
// local-test-fixtures/stroke-soft-probes/probes.psd (make_probes.jsx): 40 px squares
// filled through a selection feathered 0, 1, 2, 4 and 8 px, one per column, with
// Outside 2, 5 and 10 px, Inside 5 px and Center 6 px solid strokes, one per row.
// probes-flat.bmp is its merge over white. They pin the band model in
// stroke_alpha_mask: the outer band measures from the first pixel with any alpha and
// its last ring reads that pixel's alpha, the inner band from the first pixel below
// full alpha with its last ring reading the missing alpha, and every partial pixel
// carries the two paints. Before this model the band sat about a pixel inside
// Photoshop's on every feathered square (the reporter's "1 px smaller" stroke).
void stroke_soft_edge_probes_match_photoshop_if_available() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "stroke-soft-probes";
  patchy::test::skip_unless_exists(dir / "probes.psd");
  patchy::test::skip_unless_exists(dir / "probes-flat.bmp");
  const auto document = patchy::psd::DocumentIo::read_file(dir / "probes.psd");
  const auto reference = patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(dir / "probes-flat.bmp"));
  std::vector<std::uint8_t> coverage;
  const auto flat = patchy::Compositor{}.flatten_rgb8(document, &coverage);
  CHECK(reference.width() == flat.width() && reference.height() == flat.height());
  constexpr int kCell = 96;
  const char* feathers[] = {"f0", "f1", "f2", "f4", "f8"};
  const char* strokes[] = {"out2", "out5", "in5", "ctr6", "out10"};
  double worst_cell_mean = 0.0;
  std::uint64_t total_off = 0;
  std::uint64_t total_pixels = 0;
  for (int row = 0; row < 5; ++row) {
    for (int column = 0; column < 5; ++column) {
      double total = 0.0;
      int worst = 0;
      std::uint64_t off = 0;
      for (std::int32_t y = row * kCell; y < (row + 1) * kCell; ++y) {
        for (std::int32_t x = column * kCell; x < (column + 1) * kCell; ++x) {
          const auto* a = reference.pixel(x, y);
          const auto* b = flat.pixel(x, y);
          const auto alpha = static_cast<double>(coverage[static_cast<std::size_t>(y) * flat.width() + x]) / 255.0;
          int delta = 0;
          for (int channel = 0; channel < 3; ++channel) {
            const int over_white = static_cast<int>(std::lround(b[channel] * alpha + 255.0 * (1.0 - alpha)));
            delta = std::max(delta, std::abs(a[channel] - over_white));
          }
          total += delta;
          worst = std::max(worst, delta);
          if (delta > 8) {
            ++off;
          }
        }
      }
      const double mean = total / (static_cast<double>(kCell) * kCell);
      std::cout << "  " << feathers[column] << "_" << strokes[row] << ": mean " << mean << ", worst " << worst
                << ", " << off << " pixels off by more than 8\n";
      worst_cell_mean = std::max(worst_cell_mean, mean);
      total_off += off;
      total_pixels += static_cast<std::uint64_t>(kCell) * kCell;
    }
  }
  const double share = static_cast<double>(total_off) / static_cast<double>(total_pixels);
  std::cout << "  soft stroke probes: worst cell mean " << worst_cell_mean << ", " << share * 100.0
            << "% of pixels off by more than 8\n";
  CHECK(worst_cell_mean <= 1.5);
  CHECK(share <= 0.01);
}

// Several Stroke instances on one layer (GitHub issue 265): Photoshop paints the first
// listed instance on top and, with Overprint off, its band knocks out the strokes listed
// after it as it knocks out the content, so a Multiply stroke listed first multiplies the
// BACKDROP inside its band, never the wider Normal stroke listed second. Patchy painted
// the instances in list order, so the wider second stroke covered the first entirely.
void compositor_multiple_strokes_stack_first_on_top_with_knockout() {
  using namespace patchy;
  Document document(40, 40, PixelFormat::rgb8());
  document.add_pixel_layer("Backdrop", solid_rgb(40, 40, 200, 100, 50));
  Layer styled(document.allocate_layer_id(), "Styled", solid_rgba(12, 12, 20, 220, 20, 255));
  auto& layer = document.add_layer(std::move(styled));
  layer.set_bounds(Rect{14, 14, 12, 12});
  LayerStroke first;
  first.enabled = true;
  first.size = 3.0F;
  first.position = LayerStrokePosition::Outside;
  first.blend_mode = BlendMode::Multiply;
  first.color = RgbColor{255, 255, 0};
  LayerStroke second;
  second.enabled = true;
  second.size = 8.0F;
  second.position = LayerStrokePosition::Outside;
  second.color = RgbColor{0, 0, 255};
  layer.layer_style().strokes = {first, second};
  const auto flat = Compositor{}.flatten_rgb8(document);
  // Inside the first stroke's 3 px band: yellow multiplied with the backdrop.
  const auto* inner = flat.pixel(12, 20);
  CHECK(inner[0] == 200 && inner[1] == 100 && inner[2] == 0);
  // Past it, within the second stroke's band: plain blue.
  const auto* outer = flat.pixel(8, 20);
  CHECK(outer[0] == 0 && outer[1] == 0 && outer[2] == 255);
  // Swapping the list order puts the blue stroke on top and covers the yellow band.
  layer.layer_style().strokes = {second, first};
  const auto swapped = Compositor{}.flatten_rgb8(document);
  const auto* covered = swapped.pixel(12, 20);
  CHECK(covered[0] == 0 && covered[1] == 0 && covered[2] == 255);
}

// ag-psd's strokes/src.psd (GitHub issue 265): a layer with two Stroke instances in an
// 'lmfx' block, a 7 px Multiply stroke listed first and an 18 px Normal stroke second,
// over a scribbled backdrop. ps-flat.bmp is Photoshop's flatten.
void ag_psd_strokes_two_instances_match_photoshop_if_available() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "ag-psd" / "strokes";
  patchy::test::skip_unless_exists(dir / "src.psd");
  patchy::test::skip_unless_exists(dir / "ps-flat.bmp");
  const auto document = patchy::psd::DocumentIo::read_file(dir / "src.psd");
  const patchy::Layer* layer = nullptr;
  for (const auto& candidate : document.layers()) {
    if (candidate.name() == "Layer 1") {
      layer = &candidate;
    }
  }
  CHECK(layer != nullptr);
  const auto& strokes = layer->layer_style().strokes;
  CHECK(strokes.size() == 2);
  CHECK(strokes[0].blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(strokes[0].size - 7.0F) < 0.01F);
  CHECK(strokes[1].blend_mode == patchy::BlendMode::Normal);
  CHECK(std::abs(strokes[1].size - 18.0F) < 0.01F);
  const auto reference = patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(dir / "ps-flat.bmp"));
  const auto flat = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(reference.width() == flat.width() && reference.height() == flat.height());
  write_rgb8_bmp_artifact("ag_psd_strokes_patchy", flat);
  write_rgb8_bmp_artifact("ag_psd_strokes_photoshop", reference);
  double total = 0.0;
  std::uint64_t off = 0;
  std::uint64_t count = 0;
  for (std::int32_t y = 110; y < 275; ++y) {
    for (std::int32_t x = 30; x < 372; ++x) {
      const auto* a = reference.pixel(x, y);
      const auto* b = flat.pixel(x, y);
      const int delta = std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
      total += delta;
      if (delta > 12) {
        ++off;
      }
      ++count;
    }
  }
  const double mean = total / static_cast<double>(count);
  const double share = static_cast<double>(off) / static_cast<double>(count);
  std::cout << "  strokes fixture vs Photoshop: mean " << mean << ", " << off << " pixels off by more than 12 ("
            << share * 100.0 << "%)\n";
  // Before the stacking fix the yellow band was missing: mean 6.8, 14% of the pixels off.
  CHECK(mean <= 1.5);
  CHECK(share <= 0.02);
}

// The same fixture at the shape's anti-aliased edge (GitHub issue 265 follow-up): where
// Photoshop shows only the first stroke (and the content), no pixel may carry the second,
// blue stroke. The edge zone is every semi-transparent content pixel plus the ring of up
// to 4 px outside the painted pixels, well inside the 7 px first band. The style renders
// on the layer itself and, unchanged, on a pass-through group and an isolated group
// holding the bare layer: a group's silhouette is the same matte.
void ag_psd_strokes_shape_edge_shows_only_the_first_stroke_if_available() {
  using namespace patchy;
  const auto dir = test::source_root_path() / "local-test-fixtures" / "ag-psd" / "strokes";
  test::skip_unless_exists(dir / "src.psd");
  test::skip_unless_exists(dir / "ps-flat.bmp");
  const auto source = psd::DocumentIo::read_file(dir / "src.psd");
  const Layer* background = nullptr;
  const Layer* shape = nullptr;
  for (const auto& candidate : source.layers()) {
    if (candidate.name() == "bg") {
      background = &candidate;
    } else if (candidate.name() == "Layer 1") {
      shape = &candidate;
    }
  }
  CHECK(background != nullptr && shape != nullptr);
  const auto reference = Compositor{}.flatten_rgb8(bmp::DocumentIo::read_file(dir / "ps-flat.bmp"));
  const auto width = source.width();
  const auto height = source.height();

  // Content alpha over the canvas, then the edge zone.
  const auto bounds = shape->bounds();
  const auto& pixels = std::as_const(*shape).pixels();
  std::vector<std::uint8_t> alpha(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0U);
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      const auto cx = bounds.x + x;
      const auto cy = bounds.y + y;
      if (cx >= 0 && cy >= 0 && cx < width && cy < height) {
        alpha[static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx)] =
            pixels.pixel(x, y)[3];
      }
    }
  }
  const auto alpha_at = [&](std::int32_t x, std::int32_t y) -> int {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return 0;
    }
    return alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
  };
  std::vector<std::pair<std::int32_t, std::int32_t>> zone;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto a = alpha_at(x, y);
      if (a == 255) {
        continue;
      }
      bool in_zone = a > 0;
      for (std::int32_t dy = -4; dy <= 4 && !in_zone; ++dy) {
        for (std::int32_t dx = -4; dx <= 4 && !in_zone; ++dx) {
          in_zone = dx * dx + dy * dy <= 16 && alpha_at(x + dx, y + dy) > 0;
        }
      }
      if (in_zone) {
        zone.emplace_back(x, y);
      }
    }
  }
  CHECK(zone.size() > 3000U);

  const auto check_edge = [&](const Document& document, std::string_view label) {
    const auto flat = Compositor{}.flatten_rgb8(document);
    CHECK(flat.width() == reference.width() && flat.height() == reference.height());
    write_rgb8_bmp_artifact(std::string("ag_psd_strokes_edge_") + std::string(label), flat);
    int worst = 0;
    std::uint64_t blue = 0;
    for (const auto& [x, y] : zone) {
      const auto* expected = reference.pixel(x, y);
      const auto* actual = flat.pixel(x, y);
      worst = std::max({worst, std::abs(expected[0] - actual[0]), std::abs(expected[1] - actual[1]),
                        std::abs(expected[2] - actual[2])});
      // The second stroke (131, 189, 234) is the only paint here bluer than red.
      if (actual[2] > actual[0] + 24 && expected[2] <= expected[0] + 24) {
        ++blue;
      }
    }
    std::cout << "  " << label << " edge vs Photoshop: worst " << worst << ", " << blue << " blue pixels\n";
    // Before the group fix a styled group left the backdrop on the soft edge: worst 153.
    return blue == 0U && worst <= 4;
  };

  // Every path renders (and prints) before the verdict.
  std::vector<bool> passed{check_edge(source, "layer")};
  for (const auto mode : {BlendMode::PassThrough, BlendMode::Normal}) {
    Document document(width, height, PixelFormat::rgb8());
    Layer backdrop = *background;
    document.add_layer(std::move(backdrop));
    Layer group(document.allocate_layer_id(), "Group", LayerKind::Group);
    group.set_blend_mode(mode);
    group.layer_style() = shape->layer_style();
    Layer child = *shape;
    child.layer_style() = LayerStyle{};
    group.add_child(std::move(child));
    document.add_layer(std::move(group));
    passed.push_back(check_edge(document, mode == BlendMode::PassThrough ? "pass_through_group" : "isolated_group"));
  }
  // ps-group.psd is Photoshop's own save of that pass-through group (the style copied
  // onto the group, hidden on the layer); its flatten, ps-group-flat.bmp, is
  // byte-identical to ps-flat.bmp.
  if (std::filesystem::exists(dir / "ps-group.psd") && std::filesystem::exists(dir / "ps-group-flat.bmp")) {
    const auto group_reference = Compositor{}.flatten_rgb8(bmp::DocumentIo::read_file(dir / "ps-group-flat.bmp"));
    CHECK(std::equal(group_reference.data().begin(), group_reference.data().end(), reference.data().begin(),
                     reference.data().end()));
    passed.push_back(check_edge(psd::DocumentIo::read_file(dir / "ps-group.psd"), "photoshop_group"));
  }
  for (const auto ok : passed) {
    CHECK(ok);
  }
}

}  // namespace


void compositor_burn_dodge_effects_preserve_transparent_coverage() {
  using namespace patchy;
  for (const auto mode : {BlendMode::LinearBurn, BlendMode::ColorBurn, BlendMode::ColorDodge}) {
    for (const auto backdrop_alpha : {0.0F, 0.5F, 1.0F}) {
      render_detail::IsolatedClipGroupTarget target(Rect::from_size(1, 1));
      target.composite_color(0, 0, {120, 140, 160}, backdrop_alpha, BlendMode::Normal);
      render_detail::composite_effect_color(target, 0, 0, {30, 60, 90}, 0.25F, mode,
                                            DissolveField::DropShadow);
      const auto sample = target.sample_color(0, 0);
      CHECK(std::abs(sample.alpha - (0.25F + backdrop_alpha * 0.75F)) < 0.00001F);
      if (backdrop_alpha == 0.0F) {
        CHECK(sample.color.red == 30 && sample.color.green == 60 && sample.color.blue == 90);
      }
    }
  }
}


patchy::PathSubpath masked_effect_square(double left, double top, double right, double bottom) {
  patchy::PathSubpath square;
  for (const auto& [x, y] : {std::pair{left, top}, {right, top}, {right, bottom}, {left, bottom}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    square.anchors.push_back(anchor);
  }
  return square;
}

// #447 follow-up: a layer filled edge to edge with a mask cutting out a square
// draws its effects from the masked square (Photoshop's default, the "Hides
// Effects" options off): the drop shadow lands below the square, the inner
// shadow darkens inside its top edge. Vector mask, raster mask, and a vector
// mask on a shape layer.
void compositor_layer_effects_follow_masked_shape() {
  constexpr std::int32_t kSize = 120;
  const auto add_effects = [](patchy::Layer& layer) {
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.blend_mode = patchy::BlendMode::Normal;
    shadow.color = patchy::RgbColor{0, 0, 0};
    shadow.opacity = 1.0F;
    shadow.angle_degrees = 90.0F;
    shadow.distance = 8.0F;
    shadow.size = 0.0F;
    layer.layer_style().drop_shadows.push_back(shadow);
    patchy::LayerInnerShadow inner;
    inner.enabled = true;
    inner.blend_mode = patchy::BlendMode::Normal;
    inner.color = patchy::RgbColor{0, 0, 255};
    inner.opacity = 1.0F;
    inner.angle_degrees = 90.0F;
    inner.distance = 6.0F;
    inner.size = 0.0F;
    layer.layer_style().inner_shadows.push_back(inner);
  };
  const auto check_render = [](const patchy::Document& document, const std::string& label) {
    const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
    const auto* below = flattened.pixel(50, 74);    // under the square: the drop shadow
    const auto* inside_top = flattened.pixel(50, 32);  // inside the top edge: the inner shadow
    const auto* center = flattened.pixel(50, 55);   // the red square itself
    const auto* outside = flattened.pixel(95, 95);  // masked away, no effect
    const bool ok = below[0] < 30 && below[1] < 30 && below[2] < 30 && inside_top[2] > 200 &&
                    inside_top[0] < 60 && center[0] > 200 && center[2] < 60 && outside[0] == 220 &&
                    outside[1] == 220 && outside[2] == 220;
    if (!ok) {
      std::cerr << label << ": below " << int(below[0]) << "," << int(below[1]) << "," << int(below[2])
                << " inside_top " << int(inside_top[0]) << "," << int(inside_top[1]) << "," << int(inside_top[2])
                << " center " << int(center[0]) << "," << int(center[1]) << "," << int(center[2]) << " outside "
                << int(outside[0]) << "," << int(outside[1]) << "," << int(outside[2]) << '\n';
    }
    CHECK(ok);
  };
  // Patchy's PSD round trip keeps the mask and the effects.
  const auto check = [&check_render](const patchy::Document& document, const std::string& label) {
    check_render(document, label);
    const auto reopened = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
    check_render(reopened, label + " reopened");
  };
  {
    patchy::Document document(kSize, kSize, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(kSize, kSize, 220, 220, 220));
    auto& layer = document.add_layer(patchy::Layer(document.allocate_layer_id(), "Paint",
                                                   solid_rgba(kSize, kSize, 230, 40, 40, 255)));
    patchy::LayerVectorMask mask;
    mask.path.subpaths.push_back(masked_effect_square(30.0, 30.0, 70.0, 70.0));
    layer.set_vector_mask(std::move(mask));
    patchy::update_vector_mask_raster(layer, patchy::Rect::from_size(kSize, kSize));
    add_effects(layer);
    check(document, "vector mask");
  }
  {
    patchy::Document document(kSize, kSize, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(kSize, kSize, 220, 220, 220));
    auto& layer = document.add_layer(patchy::Layer(document.allocate_layer_id(), "Paint",
                                                   solid_rgba(kSize, kSize, 230, 40, 40, 255)));
    patchy::LayerMask mask;
    mask.bounds = patchy::Rect::from_size(kSize, kSize);
    mask.pixels = patchy::PixelBuffer(kSize, kSize, patchy::PixelFormat::gray8());
    for (std::int32_t y = 30; y < 70; ++y) {
      for (std::int32_t x = 30; x < 70; ++x) {
        *mask.pixels.pixel(x, y) = 255;
      }
    }
    mask.default_color = 0;
    layer.mask() = std::move(mask);
    add_effects(layer);
    check(document, "layer mask");
  }
  {
    patchy::Document document(kSize, kSize, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_rgb(kSize, kSize, 220, 220, 220));
    patchy::Layer shape(document.allocate_layer_id(), "Shape", patchy::PixelBuffer());
    patchy::VectorShapeContent content;
    content.path.subpaths.push_back(masked_effect_square(0.0, 0.0, kSize, kSize));
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{230, 40, 40};
    shape.set_vector_shape(content);
    shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(kSize, kSize), &document.metadata().patterns);
    patchy::LayerVectorMask mask;
    mask.path.subpaths.push_back(masked_effect_square(30.0, 30.0, 70.0, 70.0));
    shape.set_vector_mask(std::move(mask));
    patchy::update_vector_mask_raster(shape, patchy::Rect::from_size(kSize, kSize));
    auto& layer = document.add_layer(std::move(shape));
    add_effects(layer);
    // In session only: PSD keeps one vector path per layer, the shape's, and the UI
    // refuses a vector mask on a shape layer (scripts can still set one).
    check_render(document, "shape layer vector mask");
  }
}


// The per-move region update of a dragged vector mask path matches a full
// re-rasterization pixel for pixel, plain and inverted, when an anchor moves
// inward (coverage shrinks) and outward (the cache bounds grow).
void vector_mask_region_update_matches_full_raster() {
  constexpr std::int32_t kSize = 160;
  const auto canvas = patchy::Rect::from_size(kSize, kSize);
  for (const bool inverted : {false, true}) {
    for (const double moved_x : {60.0, 150.0}) {
      patchy::Layer region_layer(1, "Region", solid_rgba(kSize, kSize, 200, 0, 0, 255));
      patchy::LayerVectorMask mask;
      mask.inverted = inverted;
      auto square = masked_effect_square(30.0, 30.0, 100.0, 100.0);
      square.anchors[1].out_x = 110.0;  // a curved edge
      square.anchors[1].out_y = 60.0;
      mask.path.subpaths.push_back(square);
      region_layer.set_vector_mask(mask);
      patchy::update_vector_mask_raster(region_layer, canvas);
      const auto old_path = mask.path;

      auto moved = *std::as_const(region_layer).vector_mask();
      moved.path.subpaths[0].anchors[2].anchor_x = moved_x;
      moved.path.subpaths[0].anchors[2].in_x = moved_x;
      moved.path.subpaths[0].anchors[2].out_x = moved_x;
      moved.path.subpaths[0].anchors[2].anchor_y = 140.0;
      region_layer.set_vector_mask(moved);
      auto full_layer = region_layer;
      // The old and new control hulls, as the canvas passes them.
      double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
      for (const patchy::VectorPath* path : std::array<const patchy::VectorPath*, 2>{&old_path, &moved.path}) {
        for (const auto& anchor : path->subpaths[0].anchors) {
          for (const auto& [x, y] : std::array<std::pair<double, double>, 3>{
                   {{anchor.anchor_x, anchor.anchor_y}, {anchor.in_x, anchor.in_y}, {anchor.out_x, anchor.out_y}}}) {
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
          }
        }
      }
      const patchy::Rect dirty{static_cast<std::int32_t>(std::floor(left)) - 2,
                               static_cast<std::int32_t>(std::floor(top)) - 2,
                               static_cast<std::int32_t>(std::ceil(right - left)) + 5,
                               static_cast<std::int32_t>(std::ceil(bottom - top)) + 5};
      patchy::update_vector_mask_raster_region(region_layer, canvas, std::span<const patchy::Rect>(&dirty, 1));
      patchy::update_vector_mask_raster(full_layer, canvas);
      int differences = 0;
      for (std::int32_t y = 0; y < kSize; ++y) {
        for (std::int32_t x = 0; x < kSize; ++x) {
          if (patchy::vector_mask_alpha_at(region_layer, x, y) != patchy::vector_mask_alpha_at(full_layer, x, y)) {
            ++differences;
          }
        }
      }
      if (differences != 0) {
        std::cerr << "inverted " << inverted << " moved_x " << moved_x << ": " << differences << " pixels differ\n";
      }
      CHECK(differences == 0);
    }
  }
}

std::vector<patchy::test::TestCase> compositor_layer_styles_tests() {
  return {
      {"compositor_layer_effects_follow_masked_shape", compositor_layer_effects_follow_masked_shape},
      {"vector_mask_region_update_matches_full_raster", vector_mask_region_update_matches_full_raster},
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
      {"compositor_continuous_drop_shadow_sweeps_to_offset_and_fades",
       compositor_continuous_drop_shadow_sweeps_to_offset_and_fades},
      {"compositor_shadow_distance_30000_stays_bounded", compositor_shadow_distance_30000_stays_bounded},
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
      {"compositor_interior_overlay_covers_fill_under_partial_layer_opacity",
       compositor_interior_overlay_covers_fill_under_partial_layer_opacity},
      {"compositor_interior_overlay_knocks_out_semi_transparent_fill",
       compositor_interior_overlay_knocks_out_semi_transparent_fill},
      {"compositor_interior_overlay_stays_under_vector_stroke",
       compositor_interior_overlay_stays_under_vector_stroke},
      {"compositor_effects_follow_shape_coverage_under_transparent_fill",
       compositor_effects_follow_shape_coverage_under_transparent_fill},
      {"shape_effect_silhouette_matches_photoshop_probes_if_available",
       shape_effect_silhouette_matches_photoshop_probes_if_available},
      {"stroke_on_semi_transparent_content_matches_photoshop_probes_if_available",
       stroke_on_semi_transparent_content_matches_photoshop_probes_if_available},
      {"stroke_soft_edge_probes_match_photoshop_if_available",
       stroke_soft_edge_probes_match_photoshop_if_available},
      {"compositor_multiple_strokes_stack_first_on_top_with_knockout",
       compositor_multiple_strokes_stack_first_on_top_with_knockout},
      {"ag_psd_strokes_two_instances_match_photoshop_if_available",
       ag_psd_strokes_two_instances_match_photoshop_if_available},
      {"ag_psd_strokes_shape_edge_shows_only_the_first_stroke_if_available",
       ag_psd_strokes_shape_edge_shows_only_the_first_stroke_if_available},
      {"compositor_burn_dodge_effects_preserve_transparent_coverage", compositor_burn_dodge_effects_preserve_transparent_coverage},
  };
}
