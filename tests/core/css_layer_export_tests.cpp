// Copy CSS (GitHub issue 688): the text patchy::css::layers_to_css builds for the selected
// layers. Every expectation pins the exact text, so a change to the output is deliberate.

#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "formats/css_layer_export.hpp"
#include "formats/svg_document_io.hpp"
#include "psd/psd_text_runs.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using patchy::BlendMode;
using patchy::Document;
using patchy::Layer;
using patchy::LayerId;
using patchy::LayerKind;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;
using patchy::RgbColor;
using patchy::css::class_name_from_layer_name;
using patchy::css::layers_to_css;

// A pixel layer whose buffer sits at `bounds` with only `ink` (buffer coordinates) opaque.
Layer pixel_layer(Document& document, std::string name, Rect bounds, Rect ink) {
  PixelBuffer pixels(bounds.width, bounds.height, PixelFormat::rgba8());
  pixels.clear(0);
  for (std::int32_t y = ink.y; y < ink.y + ink.height; ++y) {
    for (std::int32_t x = ink.x; x < ink.x + ink.width; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 40;
      pixel[1] = 120;
      pixel[2] = 200;
      pixel[3] = 255;
    }
  }
  Layer layer(document.allocate_layer_id(), std::move(name), std::move(pixels));
  layer.set_bounds(bounds);
  return layer;
}

Layer solid_pixel_layer(Document& document, std::string name, Rect bounds) {
  return pixel_layer(document, std::move(name), bounds, Rect{0, 0, bounds.width, bounds.height});
}

Document read_svg(std::string_view text) {
  return patchy::svg::DocumentIo::read(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

// The CSS for every top-level layer of the document.
std::string css_of_all(const Document& document, std::string_view name = "test.psd") {
  std::vector<LayerId> ids;
  for (const auto& layer : document.layers()) {
    ids.push_back(layer.id());
  }
  return layers_to_css(document, ids, name).text;
}

std::string css_of(const Document& document, std::initializer_list<LayerId> ids) {
  const std::vector<LayerId> list(ids);
  return layers_to_css(document, list, "test.psd").text;
}

patchy::LayerDropShadow shadow(float angle, float distance, float size, float spread, float opacity) {
  patchy::LayerDropShadow drop;
  drop.enabled = true;
  drop.color = RgbColor{0, 0, 0};
  drop.opacity = opacity;
  drop.angle_degrees = angle;
  drop.distance = distance;
  drop.size = size;
  drop.spread = spread;
  return drop;
}

void css_class_names_are_valid_identifiers() {
  CHECK(class_name_from_layer_name("Header Bar") == "header-bar");
  CHECK(class_name_from_layer_name("  Big   Button!! ") == "big-button");
  CHECK(class_name_from_layer_name("snake_case-Name") == "snake_case-name");
  CHECK(class_name_from_layer_name("a.b/c:d") == "a-b-c-d");
  CHECK(class_name_from_layer_name("1 copy") == "layer-1-copy");
  CHECK(class_name_from_layer_name("2026") == "layer-2026");
  CHECK(class_name_from_layer_name("") == "layer");
  CHECK(class_name_from_layer_name("*** ---") == "layer");
  CHECK(class_name_from_layer_name("--lead") == "lead");
  // Non-ASCII characters are valid in a CSS identifier and stay as typed; only ASCII letters
  // are lowercased. Unicode spaces separate like an ASCII space.
  CHECK(class_name_from_layer_name("\xE5\x86\x99\xE7\x9C\x9F \xE3\x83\xAC\xE3\x82\xA4\xE3\x83\xA4\xE3\x83\xBC") ==
        "\xE5\x86\x99\xE7\x9C\x9F-\xE3\x83\xAC\xE3\x82\xA4\xE3\x83\xA4\xE3\x83\xBC");
  CHECK(class_name_from_layer_name("Caf\xC3\xA9 Men\xC3\xBC") == "caf\xC3\xA9-men\xC3\xBC");
  CHECK(class_name_from_layer_name("\xC3\x9C" "ber") == "\xC3\x9C" "ber");
  CHECK(class_name_from_layer_name("a\xC2\xA0" "b\xE3\x80\x80" "c") == "a-b-c");
  CHECK(class_name_from_layer_name("\xF0\x9F\x8E\xA8 Art") == "\xF0\x9F\x8E\xA8-art");
  // Bytes that are not UTF-8 never reach the output.
  CHECK(class_name_from_layer_name("\xFF" "abc\xC3") == "abc");
}

void css_class_names_are_deduplicated_in_panel_order() {
  Document document(100, 100, PixelFormat::rgba8());
  // Added bottom first: the panel shows "Box", "box", "Box 2", "1st" from the top.
  document.add_layer(solid_pixel_layer(document, "1st", Rect{0, 0, 4, 4}));
  document.add_layer(solid_pixel_layer(document, "Box 2", Rect{0, 0, 3, 3}));
  document.add_layer(solid_pixel_layer(document, "box", Rect{0, 0, 2, 2}));
  document.add_layer(solid_pixel_layer(document, "Box", Rect{0, 0, 1, 1}));
  const auto css = css_of_all(document);
  const auto box = css.find("\n.box {");
  const auto box2 = css.find("\n.box-2 {");
  const auto box22 = css.find("\n.box-2-2 {");
  const auto first = css.find("\n.layer-1st {");
  CHECK(box != std::string::npos && box2 != std::string::npos && box22 != std::string::npos &&
        first != std::string::npos);
  CHECK(box < box2 && box2 < box22 && box22 < first);
  // The top "Box" is 1 px wide, the "box" under it 2 px, "Box 2" 3 px.
  CHECK(css.find(".box {\n  position: absolute;\n  left: 0px;\n  top: 0px;\n  width: 1px;") != std::string::npos);
  CHECK(css.find(".box-2 {\n  position: absolute;\n  left: 0px;\n  top: 0px;\n  width: 2px;") != std::string::npos);
  CHECK(css.find(".box-2-2 {\n  position: absolute;\n  left: 0px;\n  top: 0px;\n  width: 3px;") !=
        std::string::npos);
}

void css_pixel_layer_gives_visible_bounds_opacity_and_blend() {
  Document document(200, 100, PixelFormat::rgba8());
  // The buffer is 40x30 at (10, 20); only a 20x10 block at (5, 3) inside it is opaque.
  auto layer = pixel_layer(document, "Photo", Rect{10, 20, 40, 30}, Rect{5, 3, 20, 10});
  layer.set_opacity(0.5F);
  layer.set_fill_opacity(0.5F);
  layer.set_blend_mode(BlendMode::Multiply);
  const auto id = document.add_layer(std::move(layer)).id();
  CHECK(css_of(document, {id}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".photo {\n"
        "  position: absolute;\n"
        "  left: 15px;\n"
        "  top: 23px;\n"
        "  width: 20px;\n"
        "  height: 10px;\n"
        "  z-index: 1;\n"
        "  opacity: 0.25;\n"
        "  mix-blend-mode: multiply;\n"
        "}\n");

  // Full opacity and Normal say nothing; a mode CSS lacks is left out; Linear Dodge is
  // CSS's plus-lighter (the SVG writer's map).
  auto* stored = document.find_layer(id);
  stored->set_opacity(1.0F);
  stored->set_fill_opacity(1.0F);
  stored->set_blend_mode(BlendMode::Normal);
  CHECK(css_of(document, {id}).find("opacity") == std::string::npos);
  CHECK(css_of(document, {id}).find("mix-blend-mode") == std::string::npos);
  stored->set_blend_mode(BlendMode::Subtract);
  CHECK(css_of(document, {id}).find("mix-blend-mode") == std::string::npos);
  stored->set_blend_mode(BlendMode::LinearDodge);
  CHECK(css_of(document, {id}).find("  mix-blend-mode: plus-lighter;\n") != std::string::npos);
}

void css_shape_layers_give_background_border_and_radius() {
  auto document = read_svg(
      "<svg width=\"300\" height=\"200\">"
      "<rect id=\"Plain\" x=\"10\" y=\"20\" width=\"100\" height=\"60\" fill=\"#336699\"/>"
      "<rect id=\"Card\" x=\"10\" y=\"100\" width=\"100\" height=\"60\" rx=\"8\" fill=\"#ffcc00\" "
      "stroke=\"#102030\" stroke-width=\"4\"/>"
      "<ellipse id=\"Dot\" cx=\"200\" cy=\"50\" rx=\"30\" ry=\"20\" fill=\"#ff0000\"/>"
      "</svg>");
  CHECK(document.layers().size() == 3U);
  const auto plain = document.layers()[0].id();
  const auto card = document.layers()[1].id();
  const auto dot = document.layers()[2].id();

  CHECK(css_of(document, {plain}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".plain {\n"
        "  position: absolute;\n"
        "  left: 10px;\n"
        "  top: 20px;\n"
        "  width: 100px;\n"
        "  height: 60px;\n"
        "  z-index: 1;\n"
        "  background-color: #336699;\n"
        "}\n");
  // A centered 4 px stroke: the border box is 2 px outside the outline, and so is the
  // radius of its outer edge.
  CHECK(css_of(document, {card}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".card {\n"
        "  position: absolute;\n"
        "  left: 8px;\n"
        "  top: 98px;\n"
        "  width: 104px;\n"
        "  height: 64px;\n"
        "  z-index: 2;\n"
        "  box-sizing: border-box;\n"
        "  background-color: #ffcc00;\n"
        "  border: 4px solid #102030;\n"
        "  border-radius: 10px;\n"
        "}\n");
  CHECK(css_of(document, {dot}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".dot {\n"
        "  position: absolute;\n"
        "  left: 170px;\n"
        "  top: 30px;\n"
        "  width: 60px;\n"
        "  height: 40px;\n"
        "  z-index: 3;\n"
        "  background-color: #ff0000;\n"
        "  border-radius: 50%;\n"
        "}\n");

  // An inside, dashed, half-opaque stroke with per-corner radii, under 50% fill opacity:
  // the box is the outline, the radii are the shape's own, and the fill opacity goes into
  // both colors.
  {
    auto* layer = document.find_layer(card);
    auto shape = *layer->vector_shape();
    shape.stroke.alignment = patchy::VectorStrokeAlignment::Inside;
    shape.stroke.dashes = {2.0, 2.0};
    shape.stroke.opacity = 0.5;
    shape.origination.front().corner_radii = {4.0, 8.0, 0.0, 12.5};
    layer->set_vector_shape(std::move(shape));
    layer->set_fill_opacity(0.5F);
    layer->set_opacity(0.8F);
    const auto css = css_of(document, {card});
    CHECK(css ==
          "/* Patchy: test.psd */\n"
          "\n"
          ".card {\n"
          "  position: absolute;\n"
          "  left: 10px;\n"
          "  top: 100px;\n"
          "  width: 100px;\n"
          "  height: 60px;\n"
          "  z-index: 2;\n"
          "  opacity: 0.8;\n"
          "  box-sizing: border-box;\n"
          "  background-color: rgba(255, 204, 0, 0.5);\n"
          "  border: 4px dashed rgba(16, 32, 48, 0.25);\n"
          "  border-radius: 4px 8px 0px 12.5px;\n"
          "}\n");
  }
  // Dots, and a stroke with its fill switched off.
  {
    auto* layer = document.find_layer(card);
    auto shape = *layer->vector_shape();
    patchy::apply_stroke_dash_preset(shape.stroke, patchy::StrokeDashPreset::Dotted);
    shape.stroke.fill_enabled = false;
    layer->set_vector_shape(std::move(shape));
    const auto css = css_of(document, {card});
    CHECK(css.find("  border: 4px dotted rgba(16, 32, 48, 0.25);\n") != std::string::npos);
    CHECK(css.find("background") == std::string::npos);
  }
}

void css_shape_gradients_become_css_gradients() {
  auto document = read_svg("<svg width=\"300\" height=\"200\">"
                           "<rect id=\"Sky\" x=\"10\" y=\"20\" width=\"100\" height=\"60\" fill=\"#336699\"/>"
                           "</svg>");
  const auto id = document.layers()[0].id();
  const auto set_gradient = [&](patchy::LayerStyleGradientType type, float angle, bool reverse) {
    auto* layer = document.find_layer(id);
    auto shape = *layer->vector_shape();
    shape.fill.kind = patchy::VectorFillKind::Gradient;
    shape.fill.gradient = {};
    shape.fill.gradient.type = type;
    shape.fill.gradient.angle_degrees = angle;
    shape.fill.gradient.reverse = reverse;
    shape.fill.gradient.color_stops = {patchy::GradientColorStop{0.0F, RgbColor{255, 0, 0}},
                                       patchy::GradientColorStop{0.5F, RgbColor{0, 255, 0}},
                                       patchy::GradientColorStop{1.0F, RgbColor{0, 0, 255}}};
    shape.fill.gradient.alpha_stops = {patchy::GradientAlphaStop{0.0F, 1.0F}, patchy::GradientAlphaStop{1.0F, 0.5F}};
    layer->set_vector_shape(std::move(shape));
  };
  // Patchy's 90 degrees runs bottom to top, which is CSS's 0deg. The alpha ramp joins the
  // color stops; the easing between stops is left to CSS's straight ramps.
  set_gradient(patchy::LayerStyleGradientType::Linear, 90.0F, false);
  CHECK(css_of(document, {id}).find(
            "  background-image: linear-gradient(0deg, #ff0000 0%, rgba(0, 255, 0, 0.75) 50%, "
            "rgba(0, 0, 255, 0.5) 100%);\n") != std::string::npos);
  // 0 degrees runs left to right (CSS 90deg); Reverse swaps the ends.
  set_gradient(patchy::LayerStyleGradientType::Linear, 0.0F, true);
  CHECK(css_of(document, {id}).find(
            "  background-image: linear-gradient(90deg, rgba(0, 0, 255, 0.5) 0%, rgba(0, 255, 0, 0.75) 50%, "
            "#ff0000 100%);\n") != std::string::npos);
  // A radial ramp reaches the longer side: a 50 px circle around the middle of the box.
  set_gradient(patchy::LayerStyleGradientType::Radial, 90.0F, false);
  CHECK(css_of(document, {id}).find(
            "  background-image: radial-gradient(circle 50px at 50px 30px, #ff0000 0%, "
            "rgba(0, 255, 0, 0.75) 50%, rgba(0, 0, 255, 0.5) 100%);\n") != std::string::npos);
  // The kinds CSS has no plain form for give no background at all.
  set_gradient(patchy::LayerStyleGradientType::Angle, 90.0F, false);
  CHECK(css_of(document, {id}).find("background") == std::string::npos);
  CHECK(css_of(document, {id}).find("  width: 100px;\n") != std::string::npos);
}

void css_other_outlines_give_their_drawn_box() {
  // A triangle is no CSS box: the class is the box of what the layer draws, with the fill.
  auto document = read_svg("<svg width=\"200\" height=\"200\">"
                           "<path id=\"Tri\" d=\"M 20 20 L 80 20 L 50 70 Z\" fill=\"#00aa55\"/>"
                           "</svg>");
  auto* layer = &document.layers()[0];
  patchy::update_vector_shape_raster(*layer, Rect{0, 0, document.width(), document.height()}, nullptr);
  layer->layer_style().drop_shadows.push_back(shadow(180.0F, 4.0F, 6.0F, 0.0F, 0.5F));
  CHECK(css_of(document, {layer->id()}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".tri {\n"
        "  position: absolute;\n"
        "  left: 20px;\n"
        "  top: 20px;\n"
        "  width: 60px;\n"
        "  height: 50px;\n"
        "  z-index: 1;\n"
        "  background-color: #00aa55;\n"
        "  filter: drop-shadow(4px 0px 3px rgba(0, 0, 0, 0.5));\n"
        "}\n");
}

Layer text_layer(Document& document, std::string name, std::string text, Rect bounds) {
  auto layer = solid_pixel_layer(document, std::move(name), bounds);
  layer.metadata()[patchy::kLayerMetadataText] = std::move(text);
  return layer;
}

void css_text_layer_gives_font_properties_from_the_first_run() {
  Document document(300, 200, PixelFormat::rgba8());
  auto layer = text_layer(document, "Title", "Hello world", Rect{30, 40, 120, 28});
  std::array<patchy::psd::PsdTextStyleRun, 2> runs{};
  runs[0].start = 0;
  runs[0].length = 5;
  runs[0].family = "Noto Sans";
  runs[0].size = 24.0;
  runs[0].color = RgbColor{0x11, 0x22, 0x33};
  runs[0].bold = true;
  runs[0].italic = true;
  runs[0].leading = 30.0;
  runs[0].tracking = 50.0;
  runs[1].start = 5;
  runs[1].length = 6;
  runs[1].family = "Courier New";
  runs[1].size = 12.0;
  layer.metadata()[patchy::kLayerMetadataTextRuns] = patchy::psd::serialize_patchy_text_runs(runs);
  std::array<patchy::psd::PsdTextParagraphRun, 1> paragraphs{};
  paragraphs[0].start = 0;
  paragraphs[0].length = 11;
  paragraphs[0].justification = 2;
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = patchy::psd::serialize_patchy_paragraph_runs(paragraphs);
  layer.layer_style().drop_shadows.push_back(shadow(180.0F, 4.0F, 6.0F, 0.0F, 0.5F));
  const auto id = document.add_layer(std::move(layer)).id();
  CHECK(css_of(document, {id}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".title {\n"
        "  position: absolute;\n"
        "  left: 30px;\n"
        "  top: 40px;\n"
        "  width: 120px;\n"
        "  height: 28px;\n"
        "  z-index: 1;\n"
        "  font-family: \"Noto Sans\";\n"
        "  font-size: 24px;\n"
        "  font-weight: bold;\n"
        "  font-style: italic;\n"
        "  color: #112233;\n"
        "  line-height: 30px;\n"
        "  letter-spacing: 0.05em;\n"
        "  text-align: center;\n"
        "  white-space: nowrap;\n"
        "  text-shadow: 4px 0px 6px rgba(0, 0, 0, 0.5);\n"
        "}\n");

  // A scaled text layer keeps its font size in the runs and the scale in its transform.
  document.find_layer(id)->metadata()[patchy::kLayerMetadataTextTransform] =
      patchy::serialize_layer_affine_transform({2.0, 0.0, 0.0, 2.0, 30.0, 40.0});
  const auto scaled = css_of(document, {id});
  CHECK(scaled.find("  font-size: 48px;\n") != std::string::npos);
  CHECK(scaled.find("  line-height: 60px;\n") != std::string::npos);
}

void css_text_layer_falls_back_to_its_font_keys_and_names_styles() {
  Document document(300, 200, PixelFormat::rgba8());
  // No runs: the single-value keys describe the text. Two lines of point text keep their
  // break, and fill opacity goes into the color.
  auto plain = text_layer(document, "Note", "Two\nlines", Rect{5, 6, 50, 40});
  plain.metadata()[patchy::kLayerMetadataTextFont] = "Georgia";
  plain.metadata()[patchy::kLayerMetadataTextSize] = "18";
  plain.metadata()[patchy::kLayerMetadataTextColor] = "#c04000";
  plain.set_fill_opacity(0.5F);
  const auto plain_id = document.add_layer(std::move(plain)).id();
  CHECK(css_of(document, {plain_id}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".note {\n"
        "  position: absolute;\n"
        "  left: 5px;\n"
        "  top: 6px;\n"
        "  width: 50px;\n"
        "  height: 40px;\n"
        "  z-index: 1;\n"
        "  font-family: \"Georgia\";\n"
        "  font-size: 18px;\n"
        "  font-weight: normal;\n"
        "  color: rgba(192, 64, 0, 0.5);\n"
        "  line-height: normal;\n"
        "  text-align: left;\n"
        "  white-space: pre;\n"
        "}\n");

  // A named face states its weight; caps, underline, right-justified box text, a
  // Photoshop layout's auto leading, right-to-left direction and vertical type.
  auto styled = text_layer(document, "Styled", "Styled", Rect{5, 60, 80, 30});
  std::array<patchy::psd::PsdTextStyleRun, 1> runs{};
  runs[0].start = 0;
  runs[0].length = 6;
  runs[0].family = "Lubalin \"Graph\"";
  runs[0].size = 20.0;
  runs[0].style = "Demi Oblique";
  runs[0].auto_leading = true;
  runs[0].tracking = -25.0;
  runs[0].font_caps = 2;
  runs[0].underline = true;
  runs[0].strikethrough = true;
  styled.metadata()[patchy::kLayerMetadataTextRuns] = patchy::psd::serialize_patchy_text_runs(runs);
  std::array<patchy::psd::PsdTextParagraphRun, 1> paragraphs{};
  paragraphs[0].start = 0;
  paragraphs[0].length = 6;
  paragraphs[0].justification = 4;
  paragraphs[0].auto_leading_fraction = 1.5;
  paragraphs[0].direction = "rtl";
  styled.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      patchy::psd::serialize_patchy_paragraph_runs(paragraphs);
  styled.metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
  styled.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  styled.metadata()[patchy::kLayerMetadataTextOrientation] = patchy::kTextOrientationVertical;
  const auto styled_id = document.add_layer(std::move(styled)).id();
  CHECK(css_of(document, {styled_id}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".styled {\n"
        "  position: absolute;\n"
        "  left: 5px;\n"
        "  top: 60px;\n"
        "  width: 80px;\n"
        "  height: 30px;\n"
        "  z-index: 2;\n"
        "  font-family: \"Lubalin \\\"Graph\\\"\";\n"
        "  font-size: 20px;\n"
        "  font-weight: 600;\n"
        "  font-style: italic;\n"
        "  color: #000000;\n"
        "  line-height: 1.5;\n"
        "  letter-spacing: -0.025em;\n"
        "  text-align: justify;\n"
        "  text-align-last: right;\n"
        "  direction: rtl;\n"
        "  text-transform: uppercase;\n"
        "  text-decoration: underline line-through;\n"
        "  writing-mode: vertical-rl;\n"
        "}\n");
}

void css_drop_shadow_follows_angle_distance_size_and_spread() {
  auto document = read_svg("<svg width=\"300\" height=\"200\">"
                           "<rect id=\"Panel\" x=\"10\" y=\"20\" width=\"100\" height=\"60\" fill=\"#ffffff\"/>"
                           "</svg>");
  const auto panel = document.layers()[0].id();
  // Light from 120 degrees (upper left): the shadow falls right and down. A 20% spread
  // of a 10 px size is 2 px of spread and 8 px of blur.
  document.find_layer(panel)->layer_style().drop_shadows.push_back(shadow(120.0F, 10.0F, 10.0F, 20.0F, 0.75F));
  CHECK(css_of(document, {panel}).find("  box-shadow: 5px 8.66px 8px 2px rgba(0, 0, 0, 0.75);\n") !=
        std::string::npos);
  // A second shadow joins the list; a disabled one and one at 0% do not.
  auto second = shadow(90.0F, 3.0F, 0.0F, 0.0F, 1.0F);
  second.color = RgbColor{200, 0, 0};
  auto disabled = shadow(0.0F, 9.0F, 9.0F, 0.0F, 1.0F);
  disabled.enabled = false;
  document.find_layer(panel)->layer_style().drop_shadows.push_back(second);
  document.find_layer(panel)->layer_style().drop_shadows.push_back(disabled);
  document.find_layer(panel)->layer_style().drop_shadows.push_back(shadow(0.0F, 9.0F, 9.0F, 0.0F, 0.0F));
  CHECK(css_of(document, {panel}).find(
            "  box-shadow: 5px 8.66px 8px 2px rgba(0, 0, 0, 0.75), 0px 3px 0px #c80000;\n") != std::string::npos);
  // Effects switched off: no shadow.
  document.find_layer(panel)->layer_style().effects_visible = false;
  CHECK(css_of(document, {panel}).find("shadow") == std::string::npos);

  // A pixel layer is not its box, so its shadow is a filter that follows its transparency
  // (the blur there is a standard deviation, half the size). Its fill opacity stays out of
  // `opacity` while an effect renders, since `opacity` would dim the shadow too.
  auto photo = solid_pixel_layer(document, "Photo", Rect{150, 20, 40, 40});
  photo.set_fill_opacity(0.0F);
  photo.set_opacity(0.9F);
  photo.layer_style().drop_shadows.push_back(shadow(120.0F, 10.0F, 10.0F, 20.0F, 0.75F));
  const auto photo_id = document.add_layer(std::move(photo)).id();
  const auto css = css_of(document, {photo_id});
  CHECK(css.find("  opacity: 0.9;\n") != std::string::npos);
  CHECK(css.find("  filter: drop-shadow(5px 8.66px 5px rgba(0, 0, 0, 0.75));\n") != std::string::npos);
}

void css_hidden_layers_are_skipped_and_folders_give_their_bounds() {
  Document document(300, 200, PixelFormat::rgba8());
  const auto background = document.add_layer(solid_pixel_layer(document, "Backdrop", Rect{0, 0, 300, 200})).id();
  Layer folder(document.allocate_layer_id(), "Buttons", LayerKind::Group);
  folder.set_opacity(0.5F);
  auto left_button = solid_pixel_layer(document, "Left", Rect{20, 30, 40, 20});
  auto right_button = solid_pixel_layer(document, "Right", Rect{100, 40, 40, 20});
  auto hidden_button = solid_pixel_layer(document, "Hidden", Rect{200, 150, 50, 50});
  hidden_button.set_visible(false);
  const auto left_id = left_button.id();
  const auto hidden_id = hidden_button.id();
  folder.add_child(std::move(left_button));
  folder.add_child(std::move(right_button));
  folder.add_child(std::move(hidden_button));
  const auto folder_id = document.add_layer(std::move(folder)).id();
  Layer adjustment(document.allocate_layer_id(), "Levels", LayerKind::Adjustment);
  const auto adjustment_id = document.add_layer(std::move(adjustment)).id();

  // The folder is the box of its visible layers, with its own opacity (pass-through is no
  // CSS blend mode); its layers get no class unless they are listed.
  CHECK(css_of(document, {folder_id}) ==
        "/* Patchy: test.psd */\n"
        "\n"
        ".buttons {\n"
        "  position: absolute;\n"
        "  left: 20px;\n"
        "  top: 30px;\n"
        "  width: 120px;\n"
        "  height: 30px;\n"
        "  z-index: 5;\n"
        "  opacity: 0.5;\n"
        "}\n");

  // Listed in any order, the classes come in panel order: the folder, then the layer in
  // it, then the backdrop under both. The hidden layer and the adjustment layer are
  // counted, not written.
  const std::vector<LayerId> ids{background, hidden_id, left_id, adjustment_id, folder_id};
  const auto result = layers_to_css(document, ids, "test.psd");
  CHECK(result.classes == 3);
  CHECK(result.hidden_skipped == 1);
  CHECK(result.empty_skipped == 1);
  const auto buttons = result.text.find("\n.buttons {");
  const auto left = result.text.find("\n.left {");
  const auto backdrop = result.text.find("\n.backdrop {");
  CHECK(buttons != std::string::npos && left != std::string::npos && backdrop != std::string::npos);
  CHECK(buttons < left && left < backdrop);
  CHECK(result.text.find("hidden") == std::string::npos);
  CHECK(result.text.find("levels") == std::string::npos);

  // The same ids in another order give the same bytes.
  const std::vector<LayerId> shuffled{folder_id, adjustment_id, background, left_id, hidden_id};
  CHECK(layers_to_css(document, shuffled, "test.psd").text == result.text);

  // A hidden folder hides its layers, and nothing usable gives no text at all.
  document.find_layer(folder_id)->set_visible(false);
  const std::vector<LayerId> hidden_only{folder_id, left_id, adjustment_id};
  const auto none = layers_to_css(document, hidden_only, "test.psd");
  CHECK(none.text.empty());
  CHECK(none.classes == 0);
  CHECK(none.hidden_skipped == 2);
  CHECK(none.empty_skipped == 1);
  CHECK(layers_to_css(document, {}, "test.psd").text.empty());
}

void css_header_names_the_document() {
  Document document(50, 50, PixelFormat::rgba8());
  const auto id = document.add_layer(solid_pixel_layer(document, "A", Rect{0, 0, 5, 5})).id();
  const std::vector<LayerId> ids{id};
  CHECK(layers_to_css(document, ids, "\xE3\x83\x9D\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC caf\xC3\xA9.psd").text.rfind(
            "/* Patchy: \xE3\x83\x9D\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC caf\xC3\xA9.psd */\n\n.a {\n", 0) == 0);
  // A name cannot close the comment or break the line.
  CHECK(layers_to_css(document, ids, "evil */ body{}\n.psd").text.rfind("/* Patchy: evil * / body{} .psd */\n", 0) ==
        0);
  CHECK(layers_to_css(document, ids, "").text.rfind("/* Patchy */\n", 0) == 0);
}

// z-index follows the document's stack, not the selection: every row counts from the bottom
// of the panel (1) up, hidden layers and layers in folders included, so pasted classes stack
// as the layers do.
void css_z_index_follows_the_document_stack() {
  Document document(300, 200, PixelFormat::rgba8());
  const auto backdrop = document.add_layer(solid_pixel_layer(document, "Backdrop", Rect{0, 0, 300, 200})).id();
  auto hidden = solid_pixel_layer(document, "Hidden", Rect{0, 0, 10, 10});
  hidden.set_visible(false);
  document.add_layer(std::move(hidden));
  Layer folder(document.allocate_layer_id(), "Buttons", LayerKind::Group);
  folder.add_child(solid_pixel_layer(document, "Under", Rect{25, 35, 40, 20}));
  auto inner = solid_pixel_layer(document, "Inner", Rect{20, 30, 40, 20});
  const auto inner_id = inner.id();
  folder.add_child(std::move(inner));
  const auto folder_id = document.add_layer(std::move(folder)).id();
  const auto badge = document.add_layer(solid_pixel_layer(document, "Badge", Rect{30, 40, 20, 20})).id();

  // Rows from the bottom: Backdrop 1, Hidden 2, Under 3, Inner 4, Buttons 5, Badge 6.
  const auto all = css_of(document, {backdrop, inner_id, folder_id, badge});
  const auto z_of = [&all](std::string_view class_name) {
    const auto start = all.find("\n." + std::string(class_name) + " {\n");
    const auto line = start == std::string::npos ? start : all.find("  z-index: ", start);
    if (line == std::string::npos) {
      return std::string();
    }
    const auto end = all.find(";\n", line);
    return all.substr(line + 11, end - line - 11);
  };
  CHECK(z_of("badge") == "6");
  CHECK(z_of("buttons") == "5");
  CHECK(z_of("inner") == "4");
  CHECK(z_of("backdrop") == "1");
  // Right after the box, before the other properties.
  CHECK(all.find(".badge {\n  position: absolute;\n  left: 30px;\n  top: 40px;\n  width: 20px;\n"
                 "  height: 20px;\n  z-index: 6;\n}\n") != std::string::npos);

  // A partial selection keeps the document's numbers.
  const auto two = css_of(document, {inner_id, backdrop});
  CHECK(two.find(".inner {\n  position: absolute;\n  left: 20px;\n  top: 30px;\n  width: 40px;\n"
                 "  height: 20px;\n  z-index: 4;\n}\n") != std::string::npos);
  CHECK(two.find("  height: 200px;\n  z-index: 1;\n") != std::string::npos);
}

}  // namespace

std::vector<patchy::test::TestCase> css_layer_export_tests() {
  return {
      {"css_class_names_are_valid_identifiers", css_class_names_are_valid_identifiers},
      {"css_class_names_are_deduplicated_in_panel_order", css_class_names_are_deduplicated_in_panel_order},
      {"css_pixel_layer_gives_visible_bounds_opacity_and_blend",
       css_pixel_layer_gives_visible_bounds_opacity_and_blend},
      {"css_shape_layers_give_background_border_and_radius", css_shape_layers_give_background_border_and_radius},
      {"css_shape_gradients_become_css_gradients", css_shape_gradients_become_css_gradients},
      {"css_other_outlines_give_their_drawn_box", css_other_outlines_give_their_drawn_box},
      {"css_text_layer_gives_font_properties_from_the_first_run",
       css_text_layer_gives_font_properties_from_the_first_run},
      {"css_text_layer_falls_back_to_its_font_keys_and_names_styles",
       css_text_layer_falls_back_to_its_font_keys_and_names_styles},
      {"css_drop_shadow_follows_angle_distance_size_and_spread",
       css_drop_shadow_follows_angle_distance_size_and_spread},
      {"css_hidden_layers_are_skipped_and_folders_give_their_bounds",
       css_hidden_layers_are_skipped_and_folders_give_their_bounds},
      {"css_header_names_the_document", css_header_names_the_document},
      {"css_z_index_follows_the_document_stack", css_z_index_follows_the_document_stack},
  };
}
