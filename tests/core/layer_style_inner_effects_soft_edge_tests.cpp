// Inner Glow and Inner Shadow on feathered and anti-aliased edges: the effect belongs to the
// layer's own color, which meets the backdrop once by the layer's coverage. The rule and the
// numbers are Photoshop 2027's (docs/layer-effects-render-shadows-glows.md, "Interior effects
// on soft edges").
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "formats/bmp_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "render/layer_style_mask_ops.hpp"
#include "test_harness.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::find_layer_named;
using patchy::test::solid_rgb;

constexpr std::int32_t kSide = 64;
constexpr std::int32_t kMargin = 8;
constexpr std::int32_t kCanvas = kSide + 2 * kMargin;

// A square whose alpha climbs from 0 at its left edge to 255 at its right, with two soft
// rows at the top and bottom: every coverage from clear to opaque meets every glow strength.
patchy::PixelBuffer soft_square(patchy::RgbColor color) {
  patchy::PixelBuffer pixels(kSide, kSide, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < kSide; ++y) {
    const auto edge = std::min(y, kSide - 1 - y);
    const auto row_alpha = edge == 0 ? 96 : edge == 1 ? 200 : 255;
    for (std::int32_t x = 0; x < kSide; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = color.red;
      pixel[1] = color.green;
      pixel[2] = color.blue;
      pixel[3] = static_cast<std::uint8_t>(std::min(255, x * 255 / (kSide - 24)) * row_alpha / 255);
    }
  }
  return pixels;
}

struct Backdrop {
  bool present{true};
  patchy::RgbColor color{255, 255, 255};
};

patchy::Document styled_document(const patchy::LayerStyle& style, patchy::RgbColor layer_color, Backdrop backdrop,
                                 float opacity = 1.0F) {
  patchy::Document document(kCanvas, kCanvas, patchy::PixelFormat::rgb8());
  if (backdrop.present) {
    document.add_pixel_layer("Background",
                             solid_rgb(kCanvas, kCanvas, backdrop.color.red, backdrop.color.green,
                                       backdrop.color.blue));
  }
  auto& layer = document.add_layer(patchy::Layer(document.allocate_layer_id(), "Soft", soft_square(layer_color)));
  layer.set_bounds(patchy::Rect{kMargin, kMargin, kSide, kSide});
  layer.set_opacity(opacity);
  layer.layer_style() = style;
  return document;
}

patchy::LayerInnerGlow glow_effect(patchy::RgbColor color, patchy::BlendMode mode = patchy::BlendMode::Normal,
                                   float opacity = 1.0F) {
  patchy::LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = mode;
  glow.color = color;
  glow.opacity = opacity;
  glow.size = 12.0F;
  glow.range = 100.0F;
  return glow;
}

patchy::LayerInnerShadow shadow_effect(patchy::RgbColor color, patchy::BlendMode mode = patchy::BlendMode::Normal,
                                       float opacity = 1.0F, float distance = 0.0F) {
  patchy::LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = mode;
  shadow.color = color;
  shadow.opacity = opacity;
  shadow.size = 10.0F;
  shadow.distance = distance;
  shadow.angle_degrees = 120.0F;
  return shadow;
}

patchy::LayerStyleGradient constant_gradient(patchy::RgbColor color) {
  patchy::LayerStyleGradient gradient;
  gradient.color_stops = {{0.0F, color}, {1.0F, color}};
  gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  gradient.smoothness = 0;
  return gradient;
}

struct Variant {
  const char* name;
  patchy::LayerStyle style;
};

std::vector<Variant> soft_edge_variants() {
  std::vector<Variant> variants;
  const auto add = [&variants](const char* name, auto&& configure) {
    Variant variant{name, {}};
    configure(variant.style);
    variants.push_back(std::move(variant));
  };
  add("solid glow", [](patchy::LayerStyle& style) { style.inner_glows.push_back(glow_effect({255, 255, 255})); });
  add("glow at 50%", [](patchy::LayerStyle& style) {
    style.inner_glows.push_back(glow_effect({255, 255, 255}, patchy::BlendMode::Normal, 0.5F));
  });
  add("gradient glow", [](patchy::LayerStyle& style) {
    auto glow = glow_effect({255, 255, 255});
    glow.gradient = constant_gradient({250, 30, 60});
    style.inner_glows.push_back(glow);
  });
  add("center glow", [](patchy::LayerStyle& style) {
    auto glow = glow_effect({30, 200, 240});
    glow.source = patchy::LayerInnerGlowSource::Center;
    style.inner_glows.push_back(glow);
  });
  add("screen glow", [](patchy::LayerStyle& style) {
    style.inner_glows.push_back(glow_effect({60, 160, 220}, patchy::BlendMode::Screen, 0.75F));
  });
  add("color dodge glow", [](patchy::LayerStyle& style) {
    style.inner_glows.push_back(glow_effect({200, 190, 120}, patchy::BlendMode::ColorDodge, 0.75F));
  });
  add("solid shadow", [](patchy::LayerStyle& style) { style.inner_shadows.push_back(shadow_effect({20, 40, 250})); });
  add("multiply shadow with distance", [](patchy::LayerStyle& style) {
    style.inner_shadows.push_back(shadow_effect({0, 0, 0}, patchy::BlendMode::Multiply, 0.75F, 5.0F));
  });
  add("linear burn shadow", [](patchy::LayerStyle& style) {
    style.inner_shadows.push_back(shadow_effect({60, 160, 220}, patchy::BlendMode::LinearBurn, 0.6F));
  });
  add("glow and shadow", [](patchy::LayerStyle& style) {
    style.inner_glows.push_back(glow_effect({255, 255, 255}));
    style.inner_shadows.push_back(shadow_effect({0, 0, 0}, patchy::BlendMode::Normal, 0.8F, 4.0F));
  });
  add("overlay, glow and shadow", [](patchy::LayerStyle& style) {
    patchy::LayerColorOverlay overlay;
    overlay.enabled = true;
    overlay.color = patchy::RgbColor{240, 200, 20};
    overlay.opacity = 0.5F;
    style.color_overlays.push_back(overlay);
    style.inner_glows.push_back(glow_effect({255, 255, 255}, patchy::BlendMode::Screen));
    style.inner_shadows.push_back(shadow_effect({0, 0, 0}, patchy::BlendMode::Multiply, 0.75F, 3.0F));
  });
  return variants;
}

int byte_error(double expected, std::uint8_t actual) {
  return static_cast<int>(std::ceil(std::abs(expected - static_cast<double>(actual)) - 0.5));
}

// The layer meets the backdrop once, by its own coverage, whatever its interior effects do:
// the page keeps exactly 1 - s, a transparent backdrop keeps the layer's alpha, and the
// color over a page is the color the layer has on its own, mixed by s. A separate effect
// pass over the finished layer breaks all three on a soft edge: it leaves (1 - s)(1 - s f)
// of the page and widens the alpha.
void compositor_inner_effects_composite_with_the_layer_coverage_once() {
  const patchy::RgbColor layer_color{200, 120, 40};
  const auto alpha = soft_square(layer_color);
  for (const auto& variant : soft_edge_variants()) {
    std::vector<std::uint8_t> clear_alpha;
    const auto clear =
        patchy::Compositor{}.flatten_rgb8(styled_document(variant.style, layer_color, {false, {}}), &clear_alpha);
    const auto white = patchy::Compositor{}.flatten_rgb8(styled_document(variant.style, layer_color, {true, {255, 255, 255}}));
    const auto black = patchy::Compositor{}.flatten_rgb8(styled_document(variant.style, layer_color, {true, {0, 0, 0}}));
    const auto blue = patchy::Compositor{}.flatten_rgb8(styled_document(variant.style, layer_color, {true, {20, 60, 230}}));
    int worst_page = 0;
    int worst_alpha = 0;
    int worst_color = 0;
    bool styled = false;
    for (std::int32_t y = 0; y < kSide; ++y) {
      for (std::int32_t x = 0; x < kSide; ++x) {
        const auto s = static_cast<double>(alpha.pixel(x, y)[3]) / 255.0;
        const auto px = x + kMargin;
        const auto py = y + kMargin;
        const auto covered = static_cast<int>(clear_alpha[static_cast<std::size_t>(py * kCanvas + px)]);
        worst_alpha = std::max(worst_alpha, std::abs(covered - static_cast<int>(alpha.pixel(x, y)[3])));
        const std::array<double, 3> page{20.0, 60.0, 230.0};
        for (int channel = 0; channel < 3; ++channel) {
          // White minus black is the share of the page that shows through.
          const auto through = static_cast<double>(white.pixel(px, py)[channel]) -
                               static_cast<double>(black.pixel(px, py)[channel]);
          worst_page = std::max(worst_page, static_cast<int>(std::ceil(std::abs(through - 255.0 * (1.0 - s)) - 1.0)));
          if (covered > 0) {
            const auto own = static_cast<double>(clear.pixel(px, py)[channel]);
            worst_color = std::max(worst_color,
                                   byte_error(page[static_cast<std::size_t>(channel)] * (1.0 - s) + own * s,
                                              blue.pixel(px, py)[channel]));
            styled = styled || clear.pixel(px, py)[channel] !=
                                   std::array<std::uint8_t, 3>{layer_color.red, layer_color.green,
                                                               layer_color.blue}[static_cast<std::size_t>(channel)];
          }
        }
      }
    }
    std::cout << variant.name << ": page share error " << worst_page << ", alpha error " << worst_alpha
              << ", color error " << worst_color << "\n";
    CHECK(styled);
    CHECK(worst_page <= 0);
    CHECK(worst_alpha <= 0);
    CHECK(worst_color <= 1);
  }
}

// An opaque gradient glow covers the layer's own color on every pixel, soft ones included:
// a half-covered pixel shows half page and half glow, and nothing of the layer (Photoshop
// 2027). At an effect Opacity of 50% the layer's color and the glow share the covered part.
void compositor_opaque_gradient_inner_glow_replaces_the_layer_color_on_soft_pixels() {
  const patchy::RgbColor layer_color{0, 255, 0};
  const patchy::RgbColor glow_color{255, 0, 0};
  const auto alpha = soft_square(layer_color);
  for (const auto opacity : {1.0F, 0.5F}) {
    patchy::LayerStyle style;
    auto glow = glow_effect(glow_color, patchy::BlendMode::Normal, opacity);
    glow.gradient = constant_gradient(glow_color);
    style.inner_glows.push_back(glow);
    const auto flat = patchy::Compositor{}.flatten_rgb8(styled_document(style, layer_color, {true, {0, 0, 255}}));
    int worst = 0;
    for (std::int32_t y = 0; y < kSide; ++y) {
      for (std::int32_t x = 0; x < kSide; ++x) {
        const auto s = static_cast<double>(alpha.pixel(x, y)[3]) / 255.0;
        const auto* pixel = flat.pixel(x + kMargin, y + kMargin);
        // Red is the glow's share, green the layer's and blue the page's.
        worst = std::max(worst, byte_error(255.0 * s * opacity, pixel[0]));
        worst = std::max(worst, byte_error(255.0 * s * (1.0 - opacity), pixel[1]));
        worst = std::max(worst, byte_error(255.0 * (1.0 - s), pixel[2]));
      }
    }
    std::cout << "opaque gradient glow at " << opacity << ": worst " << worst << "\n";
    CHECK(worst <= 1);
  }
}

// Layer Opacity scales the styled layer as a whole: at 50% each pixel is halfway between
// the page and the full-opacity render, interior effects included.
void compositor_inner_effects_ride_the_layer_opacity() {
  const patchy::RgbColor layer_color{200, 120, 40};
  for (const auto& variant : soft_edge_variants()) {
    const Backdrop page{true, {20, 60, 230}};
    const auto full = patchy::Compositor{}.flatten_rgb8(styled_document(variant.style, layer_color, page));
    const auto half = patchy::Compositor{}.flatten_rgb8(styled_document(variant.style, layer_color, page, 0.5F));
    const std::array<double, 3> page_color{20.0, 60.0, 230.0};
    int worst = 0;
    for (std::int32_t y = kMargin; y < kMargin + kSide; ++y) {
      for (std::int32_t x = kMargin; x < kMargin + kSide; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          const auto expected =
              0.5 * (page_color[static_cast<std::size_t>(channel)] + static_cast<double>(full.pixel(x, y)[channel]));
          worst = std::max(worst, byte_error(expected, half.pixel(x, y)[channel]));
        }
      }
    }
    std::cout << variant.name << " at layer opacity 50%: worst " << worst << "\n";
    CHECK(worst <= 1);
  }
}

// Size 0 has no blur: the Edge field is the inverse of the layer's coverage. An opaque pixel
// gets no glow, a soft pixel of coverage s gets 1 - s of it, and a gradient is sampled at s
// (Photoshop 2027, a feathered square: 203 where the page shows 140 of 255 through a black
// square with a white glow). Softer and Precise agree there.
void compositor_inner_glow_size_zero_edge_keeps_inverse_coverage_on_soft_pixels() {
  const patchy::RgbColor black{0, 0, 0};
  const auto alpha = soft_square(black);
  for (const auto technique : {patchy::LayerGlowTechnique::Softer, patchy::LayerGlowTechnique::Precise}) {
    patchy::LayerStyle style;
    auto glow = glow_effect({255, 255, 255});
    glow.size = 0.0F;
    glow.technique = technique;
    style.inner_glows.push_back(glow);
    const auto solid = patchy::Compositor{}.flatten_rgb8(styled_document(style, black, {true, {255, 255, 255}}));
    auto& gradient = style.inner_glows.front().gradient.emplace();
    gradient.color_stops = {{0.0F, {255, 0, 0}}, {1.0F, {0, 0, 255}}};
    gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    gradient.smoothness = 0;
    const auto colored = patchy::Compositor{}.flatten_rgb8(styled_document(style, black, {true, {0, 255, 0}}));
    int worst = 0;
    for (std::int32_t y = 0; y < kSide; ++y) {
      for (std::int32_t x = 0; x < kSide; ++x) {
        const auto s = static_cast<double>(alpha.pixel(x, y)[3]) / 255.0;
        // Solid white on black over white: the page's share plus the glow's.
        worst = std::max(worst, byte_error(255.0 * ((1.0 - s) + s * (1.0 - s)), solid.pixel(x + kMargin, y + kMargin)[0]));
        // Opaque red to blue over green: the stop position is s.
        const auto* pixel = colored.pixel(x + kMargin, y + kMargin);
        worst = std::max(worst, byte_error(255.0 * s * (1.0 - s), pixel[0]));
        worst = std::max(worst, byte_error(255.0 * (1.0 - s), pixel[1]));
        worst = std::max(worst, byte_error(255.0 * s * s, pixel[2]));
      }
    }
    std::cout << "size 0 edge glow, technique " << static_cast<int>(technique) << ": worst " << worst << "\n";
    CHECK(worst <= 1);
    // Photoshop's own pixel: coverage 115 of 255 lets 140 of the page through.
    const auto s = 115.0 / 255.0;
    CHECK(std::abs(255.0 * ((1.0 - s) + s * (1.0 - s)) - 203.0) < 1.0);
  }
}

// "Blend Interior Effects as Group" on a Multiply layer: an Inner Glow is part of the group,
// so the layer's mode carries it too; an Inner Shadow lands on the blended result whether
// the option is on or off (docs/ps-compat.md, "Layer blend mode vs its own effects").
void compositor_inner_glow_joins_the_interior_group_and_inner_shadow_does_not() {
  const patchy::RgbColor layer_color{200, 120, 40};
  const patchy::RgbColor effect_color{60, 160, 220};
  const std::array<double, 3> page{90.0, 140.0, 200.0};
  const std::array<double, 3> effect{60.0, 160.0, 220.0};
  const auto alpha = soft_square(layer_color);
  const auto render = [&](const patchy::LayerStyle& style, bool grouped) {
    auto document = styled_document(style, layer_color, {true, {90, 140, 200}});
    auto& layer = document.layers().back();
    layer.set_blend_mode(patchy::BlendMode::Multiply);
    layer.layer_style().blend_interior_elements = grouped;
    return patchy::Compositor{}.flatten_rgb8(document);
  };
  // An opaque gradient glow replaces the layer's color everywhere.
  patchy::LayerStyle glow_style;
  auto glow = glow_effect(effect_color);
  glow.gradient = constant_gradient(effect_color);
  glow_style.inner_glows.push_back(glow);
  const auto glow_plain = render(glow_style, false);
  const auto glow_grouped = render(glow_style, true);
  int worst_glow = 0;
  for (std::int32_t y = 0; y < kSide; ++y) {
    for (std::int32_t x = 0; x < kSide; ++x) {
      const auto s = static_cast<double>(alpha.pixel(x, y)[3]) / 255.0;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        // Off: the glow covers the multiplied layer. On: the glow is multiplied with the page.
        const auto plain = page[channel] * (1.0 - s) + effect[channel] * s;
        const auto grouped = page[channel] * (1.0 - s) + effect[channel] * page[channel] / 255.0 * s;
        worst_glow = std::max(worst_glow, byte_error(plain, glow_plain.pixel(x + kMargin, y + kMargin)[channel]));
        worst_glow =
            std::max(worst_glow, byte_error(grouped, glow_grouped.pixel(x + kMargin, y + kMargin)[channel]));
      }
    }
  }
  std::cout << "inner glow in and out of the interior group: worst " << worst_glow << "\n";
  CHECK(worst_glow <= 1);

  patchy::LayerStyle shadow_style;
  shadow_style.inner_shadows.push_back(shadow_effect(effect_color));
  const auto shadow_plain = render(shadow_style, false);
  const auto shadow_grouped = render(shadow_style, true);
  CHECK(patchy::test::rgb_diff_metrics(shadow_plain, shadow_grouped).max_channel_delta == 0);
  // The shadow is there and not multiplied. On the opaque right edge its strength f reads
  // off a Normal layer's red (200 towards 60); the Multiply layer then shows the shadow's
  // own color over the multiplied layer color by that f.
  const auto normal = patchy::Compositor{}.flatten_rgb8(styled_document(shadow_style, layer_color, {true, {90, 140, 200}}));
  const auto px = kMargin + kSide - 1;
  const auto py = kMargin + kSide / 2;
  const auto f = (200.0 - static_cast<double>(normal.pixel(px, py)[0])) / 140.0;
  CHECK(f > 0.3 && f < 0.7);
  const std::array<double, 3> layer{200.0, 120.0, 40.0};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const auto multiplied = layer[channel] * page[channel] / 255.0;
    CHECK(std::abs(multiplied * (1.0 - f) + effect[channel] * f -
                   static_cast<double>(shadow_plain.pixel(px, py)[channel])) <= 2.5);
  }
}

// A 16-bit document takes the same fold: its flatten agrees with the 8-bit one.
void deep_compositor_inner_effects_fold_like_8_bit() {
  const patchy::RgbColor layer_color{200, 120, 40};
  for (const auto& variant : soft_edge_variants()) {
    const auto document = styled_document(variant.style, layer_color, {true, {20, 60, 230}});
    const auto flat8 = patchy::Compositor{}.flatten_rgb8(document);
    auto deep_document = document;
    patchy::convert_document_depth(deep_document, patchy::BitDepth::UInt16);
    const auto narrowed = patchy::convert_pixel_buffer_depth(patchy::Compositor{}.flatten_rgba_deep(deep_document),
                                                             patchy::BitDepth::UInt8, patchy::SampleKind::Color);
    int worst = 0;
    for (std::int32_t y = 0; y < kCanvas; ++y) {
      for (std::int32_t x = 0; x < kCanvas; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          worst = std::max(worst, std::abs(static_cast<int>(flat8.pixel(x, y)[channel]) -
                                           static_cast<int>(narrowed.pixel(x, y)[channel])));
        }
      }
    }
    std::cout << variant.name << " 16-bit against 8-bit: worst " << worst << "\n";
    CHECK(worst <= 2);
  }
}

// Photoshop 2027's captures of layers with interior effects on soft edges
// (local-test-fixtures/layer-style-soft-edges, made by probe.jsx there): 100 px squares in
// rows of 14, each its own layer, saved over a white, a blue and a mid-tone page and over
// nothing. Patchy renders the layered file on each page and stays close to Photoshop's own
// flatten, soft edges included.
struct ProbeCell {
  const char* name;
  int limit;  // largest difference from Photoshop on any page, in levels of 255
};

struct ProbePage {
  const char* name;
  bool present;
  patchy::RgbColor color;
};

void compare_soft_edge_probe(const std::string& stem, const std::vector<ProbePage>& pages,
                             const std::vector<ProbeCell>& cells) {
  const auto dir = patchy::test::local_fixtures_root() / "layer-style-soft-edges" / "probes";
  const auto psd = dir / (stem + ".psd");
  patchy::test::skip_unless_exists(psd);
  for (const auto& page : pages) {
    patchy::test::skip_unless_exists(dir / (stem + "-" + page.name + ".bmp"));
  }
  patchy::test::skip_unless_exists(dir / (stem + "-clear-alpha.bmp"));
  constexpr std::int32_t kCell = 100;
  constexpr std::int32_t kGap = 20;
  constexpr std::int32_t kColumns = 14;
  constexpr std::int32_t kReach = 10;  // a feather of 4 fades out within 10 px
  auto document = patchy::psd::DocumentIo::read_file(psd);
  CHECK(document.layers().front().name() == "Background");
  for (const auto& cell : cells) {
    CHECK(find_layer_named(document.layers(), cell.name) != nullptr);
  }
  std::vector<int> worst(cells.size(), 0);
  const auto alpha_reference =
      patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(dir / (stem + "-clear-alpha.bmp")));
  for (const auto& page : pages) {
    const auto reference = patchy::Compositor{}.flatten_rgb8(
        patchy::bmp::DocumentIo::read_file(dir / (stem + "-" + page.name + ".bmp")));
    auto& background = document.layers().front();
    background.set_visible(page.present);
    if (page.present) {
      background.set_pixels(solid_rgb(document.width(), document.height(), page.color.red, page.color.green,
                                      page.color.blue));
    }
    std::vector<std::uint8_t> alpha;
    const auto actual = patchy::Compositor{}.flatten_rgb8(document, &alpha);
    CHECK(actual.width() == reference.width() && actual.height() == reference.height());
    patchy::test::write_rgb8_bmp_artifact("layer-style-" + stem + "-" + page.name, actual);
    for (std::size_t index = 0; index < cells.size(); ++index) {
      const auto left = kGap + static_cast<std::int32_t>(index % kColumns) * (kCell + kGap);
      const auto top = kGap + static_cast<std::int32_t>(index / kColumns) * (kCell + kGap);
      int cell_worst = 0;
      for (std::int32_t y = top - kReach; y < top + kCell + kReach; ++y) {
        for (std::int32_t x = left - kReach; x < left + kCell + kReach; ++x) {
          if (!page.present) {
            // Over nothing: the alpha Photoshop kept, and the color wherever enough of the
            // layer is there for its bytes to mean something.
            const auto covered = static_cast<int>(alpha[static_cast<std::size_t>(y * actual.width() + x)]);
            const auto expected = static_cast<int>(alpha_reference.pixel(x, y)[0]);
            cell_worst = std::max(cell_worst, std::abs(covered - expected));
            if (expected < 64) {
              continue;
            }
          }
          for (int channel = 0; channel < 3; ++channel) {
            cell_worst = std::max(cell_worst, std::abs(static_cast<int>(actual.pixel(x, y)[channel]) -
                                                       static_cast<int>(reference.pixel(x, y)[channel])));
          }
        }
      }
      worst[index] = std::max(worst[index], cell_worst);
    }
  }
  for (std::size_t index = 0; index < cells.size(); ++index) {
    std::cout << stem << " " << cells[index].name << ": max error " << worst[index] << "\n";
    if (worst[index] > cells[index].limit) {
      std::cerr << cells[index].name << " differs from Photoshop by " << worst[index] << ", limit "
                << cells[index].limit << "\n";
    }
    CHECK(worst[index] <= cells[index].limit);
  }
}

void photoshop_interior_effects_soft_edge_probe_matches_if_available() {
  const std::vector<ProbePage> pages{
      {"white", true, {255, 255, 255}}, {"blue", true, {0, 0, 255}}, {"mid", true, {90, 140, 200}},
      {"clear", false, {0, 0, 0}}};
  // Feathered by 4 px unless noted. ig: Inner Glow, is: Inner Shadow, Normal 100% unless
  // named. Measured November 2026: 2 or less everywhere the base pass folds the effects.
  // Before the fold the first cell was 42 levels off and the Opacity 50% cells 31.
  compare_soft_edge_probe(
      "soft-edges", pages,
      {
          {"ig-n100", 2},
          {"ig-n50", 2},
          // A gradient whose two stops are the same red: Photoshop draws it white. Patchy
          // draws the stop color (grad-rb and grad-near below are ordinary gradients).
          {"ig-grad", 255},
          {"is-n100", 2},
          {"is-n50", 2},
          {"is-d8", 2},      // Distance 8, Size 10
          {"ig-is", 3},      // a glow under a shadow
          {"ov-ig", 2},      // a 50% Color Overlay under a glow
          {"ig-screen", 2},
          {"ig-mult", 2},
          {"ig-dodge75", 2},  // Color Dodge at 75%
          {"ig-overlay", 2},
          {"is-mult75", 2},   // the default Inner Shadow: Multiply 75%, Distance 5 at 120 degrees
          {"is-lburn60", 2},  // Linear Burn at 60%
          {"lmult-ig", 3},    // a Multiply layer
          {"lmult-ig-infx", 3},
          {"lmult-is", 3},
          {"lmult-is-infx", 3},
          // Fill Opacity 50% keeps the destination passes: the effect is composited over the
          // half-filled layer by the layer's coverage (21 on a page, 50 in the color over
          // nothing). Fill Opacity 0% is exact there.
          {"fill50-ig", 52},
          {"fill0-ig", 2},
          {"fill50-is", 52},
          {"fill0-is", 2},
          {"op50-ig", 2},  // layer Opacity 50%
          {"op50-is", 2},
          {"mask-ig", 2},  // a hard square under a feathered layer mask
          {"mask-is", 2},
          {"flat50-ig", 2},  // a hard square painted at 50%
          {"flat50-is", 2},
      });
  // The follow-up capture: ordinary gradients on a feathered square, the default Inner
  // Shadow at 90 and 120 degrees, and Size 0 shadows on hard squares, whose band widths are
  // the rounded distance vector (a half pixel rounds away from zero).
  compare_soft_edge_probe(
      "soft-edges-2", {pages[0], pages[1], pages[3]},
      {
          {"grad-rb", 2},        {"grad-const", 255}, {"grad-near", 2},   {"is-mult75-a90", 2},
          {"is-mult75-a120", 2}, {"off-d5-a120", 0},  {"off-d5-a60", 0},  {"off-d5-a240", 0},
          {"off-d5-a300", 0},    {"off-d5-a30", 0},   {"off-d5-a150", 0}, {"off-d5-a210", 0},
          {"off-d5-a330", 0},    {"off-d3-a120", 0},  {"off-d3-a60", 0},  {"off-d7-a120", 0},
          {"off-d7-a60", 0},     {"off-d1-a120", 0},  {"off-d1-a60", 0},  {"off-d3-a30", 0},
          {"off-d3-a210", 0},    {"off-d7-a30", 0},   {"off-d7-a210", 0},
      });
}

// ---- Soft mattes under Choke and the Precise technique -------------------------------------
// Photoshop 2027 measures both from the pixels that are not fully opaque: such a pixel sits
// its own coverage away from the edge, and an opaque pixel takes the smallest "distance to a
// soft pixel plus that pixel's coverage" (docs/layer-effects-render-shadows-glows.md, "Soft
// mattes under Choke and Precise"). The numbers below are Photoshop's own, read from the
// captures in local-test-fixtures/layer-style-interior-soft-matte: the effect's share of a
// green layer with a red effect, over nothing.

struct MatteEffect {
  const char* name;
  bool shadow;
  bool precise;
  float size;
  float choke;
  float range;
};

constexpr std::array<MatteEffect, 5> kMatteEffects{{
    {"Precise glow, Size 5, Range 50", false, true, 5.0F, 0.0F, 50.0F},
    {"Precise glow, Size 10, Choke 50, Range 100", false, true, 10.0F, 50.0F, 100.0F},
    {"Softer glow, Size 10, Choke 50", false, false, 10.0F, 50.0F, 100.0F},
    {"Softer glow, Size 5, Choke 40", false, false, 5.0F, 40.0F, 100.0F},
    {"Inner Shadow, Size 10, Choke 50", true, false, 10.0F, 50.0F, 100.0F},
}};

patchy::LayerStyle matte_effect_style(const MatteEffect& effect) {
  patchy::LayerStyle style;
  if (effect.shadow) {
    auto shadow = shadow_effect({255, 0, 0});
    shadow.size = effect.size;
    shadow.choke = effect.choke;
    style.inner_shadows.push_back(shadow);
  } else {
    auto glow = glow_effect({255, 0, 0});
    glow.size = effect.size;
    glow.choke = effect.choke;
    glow.range = effect.range;
    glow.technique = effect.precise ? patchy::LayerGlowTechnique::Precise : patchy::LayerGlowTechnique::Softer;
    style.inner_glows.push_back(glow);
  }
  return style;
}

constexpr std::int32_t kMatteMargin = 4;

// A green layer with the given alpha plane and effect, over nothing: the red channel of the
// flatten is the effect's share of each pixel.
patchy::PixelBuffer render_matte_effect(const std::vector<std::uint8_t>& alpha, std::int32_t width,
                                        std::int32_t height, const MatteEffect& effect) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 0;
      pixel[1] = 255;
      pixel[2] = 0;
      pixel[3] = alpha[static_cast<std::size_t>(y * width + x)];
    }
  }
  patchy::Document document(width + 2 * kMatteMargin, height + 2 * kMatteMargin, patchy::PixelFormat::rgb8());
  auto& layer = document.add_layer(patchy::Layer(document.allocate_layer_id(), "Matte", std::move(pixels)));
  layer.set_bounds(patchy::Rect{kMatteMargin, kMatteMargin, width, height});
  layer.layer_style() = matte_effect_style(effect);
  return patchy::Compositor{}.flatten_rgb8(document);
}

// The capture's ramps: 60 opaque columns, then alpha falling by `step` a column to nothing.
// The strip is tall enough for its middle row to see only the two ends.
void check_ramp_rows(int step, const std::array<std::vector<int>, 5>& expected) {
  constexpr std::int32_t kOpaque = 60;
  constexpr std::int32_t kHeight = 41;
  std::vector<std::uint8_t> row(static_cast<std::size_t>(kOpaque), 255);
  for (int value = 255 - step; value > 0; value -= step) {
    row.push_back(static_cast<std::uint8_t>(value));
  }
  const auto width = static_cast<std::int32_t>(row.size());
  std::vector<std::uint8_t> alpha;
  for (std::int32_t y = 0; y < kHeight; ++y) {
    alpha.insert(alpha.end(), row.begin(), row.end());
  }
  for (std::size_t index = 0; index < kMatteEffects.size(); ++index) {
    CHECK(expected[index].size() == row.size());
    const auto flat = render_matte_effect(alpha, width, kHeight, kMatteEffects[index]);
    int worst = 0;
    for (std::int32_t x = 0; x < width; ++x) {
      const auto actual = static_cast<int>(flat.pixel(x + kMatteMargin, kMatteMargin + kHeight / 2)[0]);
      worst = std::max(worst, std::abs(actual - expected[index][static_cast<std::size_t>(x)]));
    }
    std::cout << kMatteEffects[index].name << ", alpha falling by " << step << ": worst " << worst << "\n";
    CHECK(worst <= 2);
  }
}

std::vector<int> ramp_row(std::initializer_list<int> head, int zeros, std::initializer_list<int> tail) {
  std::vector<int> row(head);
  row.insert(row.end(), static_cast<std::size_t>(zeros), 0);
  row.insert(row.end(), tail);
  return row;
}

// The glow starts where the layer becomes opaque: the last opaque column beside a 204 column
// sits 1.8 px from the edge and one beside a 238 column 1.93. A Choke counts its radius from
// there, so the whole falling ramp is at full strength. Before this rule Patchy measured a
// Precise glow from the half-coverage column and choked the gray matte value by value: these
// rows were up to 217 levels off (Precise) and 235 (Choke).
void compositor_interior_effects_measure_a_soft_edge_from_its_opaque_boundary() {
  check_ramp_rows(51, {
                          ramp_row({212, 169, 127, 85, 42}, 50, {8, 51, 93, 135, 178, 220, 229, 237, 246}),
                          ramp_row({213, 191, 170, 149, 128, 106, 85, 64, 43, 21}, 40,
                                   {4, 26, 47, 68, 89, 111, 132, 153, 174, 196, 217, 238, 255, 255}),
                          ramp_row({255, 245, 224, 194, 153, 102, 61, 31, 10}, 42,
                                   {2, 14, 37, 69, 112, 161, 200, 228, 247, 255, 255, 255, 255}),
                          ramp_row({227, 170, 85, 28}, 52, {6, 40, 102, 181, 232, 255, 255, 255}),
                          ramp_row({255, 245, 224, 194, 153, 102, 61, 31, 10}, 42,
                                   {2, 14, 37, 69, 112, 161, 200, 228, 247, 255, 255, 255, 255}),
                      });
  check_ramp_rows(17, {
                          ramp_row({212, 169, 127, 85, 42}, 50,
                                   {3, 45, 87, 130, 172, 214, 217, 220, 223, 226, 229, 231, 234, 237, 240, 243, 246,
                                    248, 251}),
                          ramp_row({213, 191, 170, 149, 128, 106, 85, 64, 43, 21}, 40,
                                   {2,   23,  44,  65,  87,  108, 129, 150, 172, 193, 214, 235,
                                    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}),
                          ramp_row({255, 245, 224, 194, 153, 102, 61, 31, 10}, 42,
                                   {1,   12,  33,  64,  106, 156, 196, 226, 246, 255, 255, 255,
                                    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}),
                          ramp_row({227, 170, 85, 28}, 52,
                                   {2, 32, 91, 174, 229, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}),
                          ramp_row({255, 245, 224, 194, 153, 102, 61, 31, 10}, 42,
                                   {1,   12,  33,  64,  106, 156, 196, 226, 246, 255, 255, 255,
                                    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}),
                      });
}

// A layer that never reaches full opacity is all edge. A Choke of a pixel or more covers it
// with the effect completely, and a Precise glow gives it one value, the ramp at a distance
// equal to the coverage: 233 at 50% and 212 at 254 of 255 for Size 5 at Range 50.
void compositor_interior_effects_treat_a_translucent_layer_as_all_edge() {
  constexpr std::int32_t kFlat = 40;
  const std::array<std::array<int, 5>, 2> expected{{{233, 255, 255, 255, 255}, {212, 255, 255, 255, 255}}};
  const std::array<std::uint8_t, 2> levels{128, 254};
  for (std::size_t level = 0; level < levels.size(); ++level) {
    const std::vector<std::uint8_t> alpha(static_cast<std::size_t>(kFlat * kFlat), levels[level]);
    for (std::size_t index = 0; index < kMatteEffects.size(); ++index) {
      const auto flat = render_matte_effect(alpha, kFlat, kFlat, kMatteEffects[index]);
      int worst = 0;
      for (std::int32_t y = 0; y < kFlat; ++y) {
        for (std::int32_t x = 0; x < kFlat; ++x) {
          worst = std::max(worst, std::abs(static_cast<int>(flat.pixel(x + kMatteMargin, y + kMatteMargin)[0]) -
                                           expected[level][index]));
        }
      }
      std::cout << kMatteEffects[index].name << ", flat alpha " << static_cast<int>(levels[level]) << ": worst "
                << worst << "\n";
      CHECK(worst <= 1);
    }
  }
}

// A Precise glow with a Choke ramps from the choked shape outward, so a convex corner inside
// the choke radius is brighter than the straight edge beside it (255 at the corner pixel of a
// hard square against 213 along the edge, Size 10, Choke 50, Range 100). Range 50 saturates
// the whole zone and hides this. Photoshop's corner distances are not exactly Euclidean: 4
// levels. Measuring the plain distance to the outside gave 213 at the corner, 42 off.
void compositor_precise_inner_glow_choke_ramps_from_the_choked_shape() {
  constexpr std::int32_t kSquare = 40;
  constexpr std::int32_t kBlock = 12;
  const std::array<std::array<int, kBlock>, kBlock> expected{{
      {255, 244, 231, 223, 218, 213, 213, 213, 213, 213, 213, 213},
      {244, 226, 214, 201, 196, 191, 191, 191, 191, 191, 191, 191},
      {231, 214, 196, 184, 175, 170, 170, 170, 170, 170, 170, 170},
      {223, 201, 184, 166, 154, 149, 149, 149, 149, 149, 149, 149},
      {218, 196, 175, 154, 136, 128, 128, 128, 128, 128, 128, 128},
      {213, 191, 170, 149, 128, 106, 106, 106, 106, 106, 106, 106},
      {213, 191, 170, 149, 128, 106, 85, 85, 85, 85, 85, 85},
      {213, 191, 170, 149, 128, 106, 85, 64, 64, 64, 64, 64},
      {213, 191, 170, 149, 128, 106, 85, 64, 43, 43, 43, 43},
      {213, 191, 170, 149, 128, 106, 85, 64, 43, 21, 21, 21},
      {213, 191, 170, 149, 128, 106, 85, 64, 43, 21, 0, 0},
      {213, 191, 170, 149, 128, 106, 85, 64, 43, 21, 0, 0},
  }};
  const std::vector<std::uint8_t> alpha(static_cast<std::size_t>(kSquare * kSquare), 255);
  const auto flat = render_matte_effect(alpha, kSquare, kSquare, kMatteEffects[1]);
  int worst = 0;
  int worst_straight = 0;
  for (std::int32_t y = 0; y < kBlock; ++y) {
    for (std::int32_t x = 0; x < kBlock; ++x) {
      const auto error = std::abs(static_cast<int>(flat.pixel(x + kMatteMargin, y + kMatteMargin)[0]) -
                                  expected[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]);
      worst = std::max(worst, error);
      if (std::max(x, y) >= 5) {
        worst_straight = std::max(worst_straight, error);
      }
    }
  }
  std::cout << "Precise choke corner: worst " << worst << ", past the choke radius " << worst_straight << "\n";
  CHECK(worst <= 4);
  CHECK(worst_straight <= 1);
  CHECK(static_cast<int>(flat.pixel(kMatteMargin, kMatteMargin)[0]) >= 250);
}

// A Choke that takes the whole Size leaves a hard band: full strength through the last pixel
// inside the Size at Range 100 too (Photoshop: 255 for ten pixels, then 1, then nothing).
void compositor_precise_inner_glow_full_choke_is_a_hard_band() {
  constexpr std::int32_t kSquare = 40;
  const std::vector<std::uint8_t> alpha(static_cast<std::size_t>(kSquare * kSquare), 255);
  const MatteEffect effect{"Precise glow, Size 10, Choke 100, Range 100", false, true, 10.0F, 100.0F, 100.0F};
  const auto flat = render_matte_effect(alpha, kSquare, kSquare, effect);
  for (std::int32_t x = 0; x < kSquare / 2; ++x) {
    const auto actual = static_cast<int>(flat.pixel(x + kMatteMargin, kMatteMargin + kSquare / 2)[0]);
    CHECK(std::abs(actual - (x < 10 ? 255 : 0)) <= 1);
  }
}

// On a hard matte the choke is the disc dilation it always was: every inside pixel takes
// clamp(radius + 1 - distance to the nearest outside pixel), now at any radius (radii over 8
// used a chamfer band). The bytes for radii through 8 are the ones the July 2026 probes
// matched, so this compares exactly.
void interior_choke_on_a_hard_matte_is_the_exact_disc_dilation() {
  constexpr int kWidth = 72;
  constexpr int kHeight = 64;
  std::vector<float> matte(static_cast<std::size_t>(kWidth * kHeight), 0.0F);
  const auto inside = [](int x, int y) {
    const bool body = x >= 6 && x < 66 && y >= 5 && y < 58;
    const bool notch = x >= 40 && y < 30;          // an L shape: one concave corner
    const bool hole = x >= 14 && x < 17 && y >= 36 && y < 38;
    const bool dot = x == 28 && y == 20;
    return body && !notch && !hole && !dot;
  };
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      matte[static_cast<std::size_t>(y * kWidth + x)] = inside(x, y) ? 1.0F : 0.0F;
    }
  }
  struct Case {
    float size;
    float choke;
    int radius;
  };
  for (const auto& test : {Case{20.0F, 30.0F, 6}, Case{10.0F, 50.0F, 5}, Case{20.0F, 60.0F, 12}, Case{5.0F, 100.0F, 5},
                           Case{16.0F, 50.0F, 8}}) {
    std::vector<float> reference(matte.size(), 0.0F);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        auto value = 1.0F - matte[static_cast<std::size_t>(y * kWidth + x)];
        const int reach = test.radius + 1;
        for (int dy = -reach; dy <= reach; ++dy) {
          for (int dx = -reach; dx <= reach; ++dx) {
            const int sx = x + dx;
            const int sy = y + dy;
            if (sx < 0 || sy < 0 || sx >= kWidth || sy >= kHeight ||
                matte[static_cast<std::size_t>(sy * kWidth + sx)] > 0.0F) {
              continue;
            }
            const auto distance = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
            value = std::max(value, static_cast<float>(std::clamp(test.radius + 1.0 - distance, 0.0, 1.0)));
          }
        }
        reference[static_cast<std::size_t>(y * kWidth + x)] = value;
      }
    }
    const auto peak = std::max(2, static_cast<int>(std::lround(test.size))) - test.radius;
    if (peak >= 2) {
      patchy::render_detail::blur_satin_tent_mask_in_place(reference, kWidth, kHeight, static_cast<float>(peak));
    }
    auto actual = matte;
    patchy::render_detail::prepare_photoshop_interior_soft_mask(actual, kWidth, kHeight, test.size, test.choke);
    CHECK(actual == reference);
  }
}

// The masks are a function of the matte alone: a window of the layer with the padding the
// compositor gives it (Size + 2, or Size + 6 for a Precise glow whose Choke counts back from
// the choked shape) computes the same values as the whole layer.
void interior_soft_matte_masks_do_not_depend_on_the_window() {
  constexpr int kWidth = 120;
  constexpr int kHeight = 96;
  std::vector<float> matte(static_cast<std::size_t>(kWidth * kHeight), 0.0F);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      // A wide blob with a soft shoulder, an anti-aliased hole and a hard notch.
      const auto dx = (x - 58.0) / 46.0;
      const auto dy = (y - 46.0) / 34.0;
      auto value = std::clamp((1.0 - (dx * dx + dy * dy)) * 6.0, 0.0, 1.0);
      const auto hole = std::sqrt((x - 70.0) * (x - 70.0) + (y - 40.0) * (y - 40.0));
      value = std::min(value, std::clamp(hole - 6.3, 0.0, 1.0));
      if (x >= 30 && x < 34 && y >= 50) {
        value = 0.0;
      }
      matte[static_cast<std::size_t>(y * kWidth + x)] = static_cast<float>(std::round(value * 255.0) / 255.0);
    }
  }
  struct Case {
    const char* name;
    bool precise;
    float size;
    float choke;
    float range;
    int padding;
  };
  for (const auto& test : {Case{"Softer choke", false, 12.0F, 40.0F, 100.0F, 14},
                           Case{"Precise", true, 12.0F, 0.0F, 100.0F, 14},
                           Case{"Precise choke, Range 50", true, 12.0F, 50.0F, 50.0F, 14},
                           Case{"Precise choke, Range 100", true, 12.0F, 50.0F, 100.0F, 18}}) {
    const auto prepare = [&test](std::vector<float>& mask, int width, int height) {
      if (test.precise) {
        patchy::render_detail::prepare_inner_glow_precise_mask(mask, width, height, test.size, test.choke,
                                                               test.range, false);
      } else {
        patchy::render_detail::prepare_inner_glow_softer_mask(mask, width, height, test.size, test.choke, test.range,
                                                              false);
      }
    };
    auto whole = matte;
    prepare(whole, kWidth, kHeight);
    float worst = 0.0F;
    bool varied = false;
    for (const auto& window : {patchy::Rect{40, 30, 24, 20}, patchy::Rect{62, 28, 30, 26}, patchy::Rect{22, 44, 20, 28}}) {
      const patchy::Rect domain{window.x - test.padding, window.y - test.padding, window.width + 2 * test.padding,
                                window.height + 2 * test.padding};
      CHECK(domain.x >= 0 && domain.y >= 0 && domain.x + domain.width <= kWidth &&
            domain.y + domain.height <= kHeight);
      std::vector<float> part(static_cast<std::size_t>(domain.width * domain.height), 0.0F);
      for (int y = 0; y < domain.height; ++y) {
        for (int x = 0; x < domain.width; ++x) {
          part[static_cast<std::size_t>(y * domain.width + x)] =
              matte[static_cast<std::size_t>((y + domain.y) * kWidth + x + domain.x)];
        }
      }
      prepare(part, domain.width, domain.height);
      for (int y = 0; y < window.height; ++y) {
        for (int x = 0; x < window.width; ++x) {
          const auto expected = whole[static_cast<std::size_t>((y + window.y) * kWidth + x + window.x)];
          const auto actual =
              part[static_cast<std::size_t>((y + test.padding) * domain.width + x + test.padding)];
          worst = std::max(worst, std::abs(expected - actual));
          varied = varied || (expected > 0.05F && expected < 0.95F);
        }
      }
    }
    std::cout << test.name << ": window against whole, worst " << worst << "\n";
    CHECK(varied);
    CHECK(worst <= 1.0e-5F);
  }
}

// Photoshop 2027's captures of one layer of known alpha shapes under interior effects
// (local-test-fixtures/layer-style-interior-soft-matte, made by make_shapes.py,
// make_shapes2.py and probe.jsx there). Patchy renders each layered file over nothing and
// compares the color wherever the layer has any alpha, region by region.
struct MatteRegion {
  const char* name;
  patchy::Rect rect;
};

struct MatteProbe {
  const char* name;
  std::vector<int> limits;  // per region, in levels of 255
};

void compare_soft_matte_sheet(const char* alpha_name, const std::vector<MatteRegion>& regions,
                              const std::vector<MatteProbe>& probes) {
  const auto dir = patchy::test::local_fixtures_root() / "layer-style-interior-soft-matte" / "probes";
  patchy::test::skip_unless_exists(dir / alpha_name);
  const auto alpha_reference = patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(dir / alpha_name));
  for (const auto& probe : probes) {
    CHECK(probe.limits.size() == regions.size());
    const auto psd = dir / (std::string(probe.name) + ".psd");
    const auto bmp = dir / (std::string(probe.name) + ".bmp");
    patchy::test::skip_unless_exists(psd);
    patchy::test::skip_unless_exists(bmp);
    const auto document = patchy::psd::DocumentIo::read_file(psd);
    const auto reference = patchy::Compositor{}.flatten_rgb8(patchy::bmp::DocumentIo::read_file(bmp));
    std::vector<std::uint8_t> alpha;
    const auto actual = patchy::Compositor{}.flatten_rgb8(document, &alpha);
    CHECK(actual.width() == reference.width() && actual.height() == reference.height());
    patchy::test::write_rgb8_bmp_artifact(std::string("layer-style-interior-soft-matte-") + probe.name, actual);
    std::cout << probe.name << ":";
    for (std::size_t index = 0; index < regions.size(); ++index) {
      const auto& rect = regions[index].rect;
      int worst = 0;
      for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
        for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
          const auto expected_alpha = static_cast<int>(alpha_reference.pixel(x, y)[0]);
          worst = std::max(worst, std::abs(static_cast<int>(alpha[static_cast<std::size_t>(y * actual.width() + x)]) -
                                           expected_alpha));
          if (expected_alpha == 0) {
            continue;
          }
          for (int channel = 0; channel < 3; ++channel) {
            worst = std::max(worst, std::abs(static_cast<int>(actual.pixel(x, y)[channel]) -
                                             static_cast<int>(reference.pixel(x, y)[channel])));
          }
        }
      }
      std::cout << " " << regions[index].name << " " << worst;
      if (worst > probe.limits[index]) {
        std::cerr << probe.name << " " << regions[index].name << " differs from Photoshop by " << worst
                  << ", limit " << probe.limits[index] << "\n";
      }
      CHECK(worst <= probe.limits[index]);
    }
    std::cout << "\n";
  }
}

// The first sheet, top to bottom: straight edges (a hard square and four blurred ones),
// curved and rotated anti-aliased shapes, flat squares of alpha 64 to 254, opaque squares
// with a translucent dot or patch, blobs that never or barely reach full opacity, ramps of 1
// to 51 levels a pixel, and staircases of flat steps.
// ig: Inner Glow (soft: Softer, prec: Precise), is: Inner Shadow; cN: Choke, sN: Size (20
// unless named), rN: Range (100 unless named), dN: Distance. Measured November 2026, each
// limit one above. Before the soft-matte distance the straight, flat, ramp and stairs
// regions of every choked or Precise row were 124 to 255 levels off. What is left sits on
// curves and corners, where Photoshop's distances are not exactly Euclidean, and is largest
// where a ramp is steep (Precise at Range 50, a high Choke). Choke 100 draws a hard band
// whose rim lands a pixel apart on curves and around a dot.
void photoshop_interior_soft_matte_probe_matches_if_available() {
  compare_soft_matte_sheet("shapes-alpha.bmp",
                           {
                               {"straight", {20, 20, 750, 140}},
                               {"curved", {780, 20, 440, 140}},
                               {"flat", {20, 180, 1230, 100}},
                               {"dent", {20, 320, 950, 100}},
                               {"blob", {20, 460, 740, 140}},
                               {"ramp", {20, 620, 700, 140}},
                               {"stairs", {20, 780, 730, 140}},
                           },
                           {
      {"ig-soft-c0", {2, 2, 2, 2, 2, 2, 2}},
      {"ig-soft-c1", {2, 2, 2, 2, 2, 2, 2}},
      {"ig-soft-c5", {2, 2, 1, 2, 2, 2, 2}},
      {"ig-soft-c10", {2, 2, 1, 2, 2, 2, 2}},
      {"ig-soft-c30", {2, 4, 1, 4, 3, 2, 2}},
      {"ig-soft-c60", {2, 8, 1, 8, 8, 2, 2}},
      {"ig-soft-c100", {2, 92, 1, 139, 64, 3, 2}},
      {"ig-soft-s10-c50", {3, 5, 1, 6, 3, 2, 2}},
      {"ig-soft-s5-c40", {2, 2, 1, 2, 2, 2, 2}},
      {"ig-soft-s5-c0", {1, 2, 2, 1, 2, 2, 2}},
      {"ig-soft-c30-r50", {3, 7, 1, 6, 4, 2, 2}},
      {"ig-soft-c30-center", {2, 4, 1, 4, 3, 2, 2}},
      {"ig-prec-r100", {2, 3, 2, 5, 4, 2, 2}},
      {"ig-prec-r50", {3, 6, 2, 8, 7, 2, 2}},
      {"ig-prec-c30-r50", {5, 8, 1, 10, 9, 2, 2}},
      {"ig-prec-s5-r50", {5, 6, 2, 7, 6, 2, 2}},
      {"ig-prec-s10-c50", {4, 7, 1, 7, 7, 4, 4}},
      {"ig-prec-s2-r50", {2, 2, 2, 2, 2, 2, 2}},
      {"ig-prec-r50-center", {3, 6, 2, 7, 7, 2, 2}},
      {"is-c0", {2, 2, 2, 2, 2, 2, 2}},
      {"is-c10", {2, 2, 1, 2, 2, 2, 2}},
      {"is-c30", {2, 4, 1, 4, 3, 2, 2}},
      {"is-c60", {2, 8, 1, 8, 8, 2, 2}},
      {"is-c100", {2, 92, 1, 139, 64, 3, 2}},
      {"is-s10-c50", {3, 5, 1, 6, 3, 2, 2}},
      {"is-d8-c30", {2, 4, 1, 4, 3, 2, 2}},
      {"is-d8-c0", {2, 2, 2, 2, 2, 2, 2}},
                           });
}

// The second sheet: a hard L and plus (concave and convex corners), thin bars and small
// squares, a rotated anti-aliased L and a ring, a blurred L, a 30 degree wedge, an L whose
// notch is alpha 254 (a square by eye, an L to the effect), a 2 px checker, random alpha of
// 200 to 255, an opaque square with 30 scattered translucent pixels, and a square whose
// alpha falls to nothing over 30 px. Measured November 2026, each limit one above. Before the
// soft-matte distance the blurred L, the 254 notch, the random alpha, the scattered pixels
// and the falling edge were 76 to 255 levels off in every choked or Precise row, and a
// Precise glow with a Choke 21 to 128 at hard corners and thin bars. What is left: corners
// and tips (Photoshop's distances are not exactly Euclidean), most where the ramp is steep
// (Choke 60 at Range 75: 10 at a corner, 18 at the wedge's tip, 24 around a scattered
// pixel), and the rim of the Choke 100 hard band (70).
void photoshop_interior_soft_matte_corner_probe_matches_if_available() {
  compare_soft_matte_sheet("shapes2-alpha.bmp",
                           {
                               {"corners", {20, 20, 270, 120}},
                               {"thin", {320, 20, 200, 120}},
                               {"curved", {550, 20, 310, 170}},
                               {"soft-L", {880, 20, 160, 160}},
                               {"wedge", {20, 220, 140, 140}},
                               {"notch254", {180, 220, 120, 120}},
                               {"checker", {330, 220, 120, 120}},
                               {"noise", {480, 220, 120, 120}},
                               {"sparse", {630, 220, 120, 120}},
                               {"fall", {780, 220, 120, 120}},
                           },
                           {
      {"s2-ig-soft-c0", {2, 2, 2, 2, 2, 2, 2, 2, 2, 2}},
      {"s2-ig-soft-c30", {2, 2, 3, 2, 5, 2, 1, 1, 5, 2}},
      {"s2-ig-soft-s10-c50", {5, 1, 6, 3, 9, 3, 1, 1, 8, 2}},
      {"s2-ig-prec-r100", {5, 1, 4, 5, 4, 5, 1, 2, 5, 2}},
      {"s2-ig-prec-s5-r50", {7, 2, 7, 5, 6, 5, 2, 2, 7, 2}},
      {"s2-ig-prec-c30-r100", {6, 3, 5, 5, 5, 5, 1, 1, 8, 5}},
      {"s2-ig-prec-c60-r75", {11, 1, 11, 11, 19, 11, 1, 1, 25, 4}},
      {"s2-ig-prec-s10-c50", {7, 4, 7, 7, 7, 7, 1, 1, 17, 5}},
      {"s2-ig-prec-s10-c50-center", {7, 4, 7, 7, 7, 7, 1, 1, 17, 5}},
      {"s2-ig-prec-s10-c100", {71, 2, 71, 65, 65, 65, 1, 1, 71, 2}},
      {"s2-is-s10-c50", {5, 1, 6, 3, 9, 3, 1, 1, 8, 2}},
      {"s2-is-c30-d5", {2, 2, 3, 2, 5, 2, 1, 1, 5, 2}},
                           });
}
}  // namespace

std::vector<patchy::test::TestCase> layer_style_inner_effects_soft_edge_tests() {
  return {
      {"compositor_interior_effects_measure_a_soft_edge_from_its_opaque_boundary",
       compositor_interior_effects_measure_a_soft_edge_from_its_opaque_boundary},
      {"compositor_interior_effects_treat_a_translucent_layer_as_all_edge",
       compositor_interior_effects_treat_a_translucent_layer_as_all_edge},
      {"compositor_precise_inner_glow_choke_ramps_from_the_choked_shape",
       compositor_precise_inner_glow_choke_ramps_from_the_choked_shape},
      {"compositor_precise_inner_glow_full_choke_is_a_hard_band",
       compositor_precise_inner_glow_full_choke_is_a_hard_band},
      {"interior_choke_on_a_hard_matte_is_the_exact_disc_dilation",
       interior_choke_on_a_hard_matte_is_the_exact_disc_dilation},
      {"interior_soft_matte_masks_do_not_depend_on_the_window", interior_soft_matte_masks_do_not_depend_on_the_window},
      {"photoshop_interior_soft_matte_probe_matches_if_available",
       photoshop_interior_soft_matte_probe_matches_if_available},
      {"photoshop_interior_soft_matte_corner_probe_matches_if_available",
       photoshop_interior_soft_matte_corner_probe_matches_if_available},
      {"compositor_inner_effects_composite_with_the_layer_coverage_once",
       compositor_inner_effects_composite_with_the_layer_coverage_once},
      {"compositor_opaque_gradient_inner_glow_replaces_the_layer_color_on_soft_pixels",
       compositor_opaque_gradient_inner_glow_replaces_the_layer_color_on_soft_pixels},
      {"compositor_inner_effects_ride_the_layer_opacity", compositor_inner_effects_ride_the_layer_opacity},
      {"compositor_inner_glow_size_zero_edge_keeps_inverse_coverage_on_soft_pixels",
       compositor_inner_glow_size_zero_edge_keeps_inverse_coverage_on_soft_pixels},
      {"compositor_inner_glow_joins_the_interior_group_and_inner_shadow_does_not",
       compositor_inner_glow_joins_the_interior_group_and_inner_shadow_does_not},
      {"deep_compositor_inner_effects_fold_like_8_bit", deep_compositor_inner_effects_fold_like_8_bit},
      {"photoshop_interior_effects_soft_edge_probe_matches_if_available",
       photoshop_interior_effects_soft_edge_probe_matches_if_available},
  };
}
