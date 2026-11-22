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

}  // namespace

std::vector<patchy::test::TestCase> layer_style_inner_effects_soft_edge_tests() {
  return {
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
