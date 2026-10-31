#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "formats/miniz/miniz.h"
#include "formats/svg_document_io.hpp"
#include "formats/svg_xml.hpp"
#include "formats/vector_export_plan.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using patchy::BlendMode;
using patchy::Document;
using patchy::Layer;
using patchy::LayerKind;
using patchy::LiveShapeKind;
using patchy::PathCombineOp;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;
using patchy::VectorFillKind;
using patchy::VectorShapeContent;
using patchy::VectorStrokeAlignment;
using patchy::VectorStrokeCap;

std::span<const std::uint8_t> bytes_of(std::string_view text) {
  return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

Document read_svg(std::string_view text, std::vector<std::string>* notices = nullptr) {
  return patchy::svg::DocumentIo::read(bytes_of(text), notices);
}

std::string write_svg(const Document& document, std::vector<std::string>* notices = nullptr) {
  const auto bytes = patchy::svg::DocumentIo::write(document, notices);
  return std::string(bytes.begin(), bytes.end());
}

// --- svg_xml -----------------------------------------------------------------

void svg_xml_parses_entities_dtd_cdata_and_namespaces() {
  const std::string_view text = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd" [
  <!ENTITY ns_svg "http://www.w3.org/2000/svg">
  <!ENTITY brand "Patchy &amp; Friends">
]>
<!-- an Illustrator-style prolog -->
<svg:svg xmlns:svg="&ns_svg;" xmlns:xlink="http://www.w3.org/1999/xlink" width="10" height="10">
  <svg:style><![CDATA[.st0{fill:#FF0000;}]]></svg:style>
  <svg:g id="A&#65;">
    <svg:path svg:d="M0 0" xlink:href="#x" data-note="&brand;"/>
  </svg:g>
</svg:svg>)";
  const auto root = patchy::svg::parse_xml(bytes_of(text));
  CHECK(root.name == "svg");  // svg: prefix resolved through the DTD entity namespace value
  CHECK(root.attribute("width") != nullptr && *root.attribute("width") == "10");
  const auto* style = &root.children.at(0);
  CHECK(style->name == "style");
  CHECK(style->all_text() == ".st0{fill:#FF0000;}");  // CDATA is literal
  const auto* group = &root.children.at(1);
  CHECK(group->name == "g");
  CHECK(group->attribute("id") != nullptr && *group->attribute("id") == "AA");  // &#65; = 'A'
  const auto* path = &group->children.at(0);
  CHECK(path->name == "path");
  CHECK(path->attribute("d") != nullptr);                  // svg:d resolves to the SVG namespace
  CHECK(path->attribute("href") != nullptr);               // xlink:href collapses to href
  CHECK(*path->attribute("data-note") == "Patchy & Friends");  // nested entity expansion
}

void svg_xml_reports_malformed_input() {
  bool threw = false;
  try {
    (void)patchy::svg::parse_xml(bytes_of("<svg><g></svg>"));
  } catch (const std::exception& error) {
    threw = true;
    CHECK(std::string(error.what()).find("mismatched closing tag") != std::string::npos);
  }
  CHECK(threw);
  threw = false;
  try {
    (void)patchy::svg::parse_xml(bytes_of("plain text, not xml"));
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

void svg_xml_transcodes_utf16() {
  const std::string_view narrow = "<svg width=\"4\" height=\"4\"/>";
  std::vector<std::uint8_t> wide{0xFF, 0xFE};  // UTF-16 LE BOM
  for (const char c : narrow) {
    wide.push_back(static_cast<std::uint8_t>(c));
    wide.push_back(0);
  }
  const auto root = patchy::svg::parse_xml(wide);
  CHECK(root.name == "svg");
  CHECK(*root.attribute("width") == "4");
}

// --- path grammar ------------------------------------------------------------

void svg_path_grammar_parses_all_commands() {
  // Relative forms, implicit repeats, run-together arc flags, scientific
  // notation, and comma/space laxity in one string.
  const auto document = read_svg(
      "<svg width=\"100\" height=\"100\">"
      "<path id=\"p\" fill=\"#102030\" d=\"m10,10 20 0 L30 30 h-10 v1e1 C10,50 20,60 30,70 s10,10 20,10 "
      "Q60,80 70,70 t10,-10 a5,5 0 0110 0 z\"/>"
      "</svg>");
  CHECK(document.layers().size() == 1);
  const auto* shape = document.layers().front().vector_shape();
  CHECK(shape != nullptr);
  CHECK(shape->path.subpaths.size() == 1);
  const auto& subpath = shape->path.subpaths.front();
  CHECK(subpath.closed);
  CHECK(subpath.anchors.size() >= 9);
  CHECK(std::abs(subpath.anchors[0].anchor_x - 10.0) < 1e-9);
  CHECK(std::abs(subpath.anchors[1].anchor_x - 30.0) < 1e-9);  // "20 0" relative implicit repeat of m -> line
  CHECK(std::abs(subpath.anchors[3].anchor_x - 20.0) < 1e-9);  // h-10
  CHECK(std::abs(subpath.anchors[4].anchor_y - 40.0) < 1e-9);  // v1e1
  CHECK(shape->fill.kind == VectorFillKind::Solid);
  CHECK(shape->fill.color.red == 0x10 && shape->fill.color.green == 0x20 && shape->fill.color.blue == 0x30);
}

// --- structure, order, styles ------------------------------------------------

void svg_import_builds_layer_stack_in_document_order() {
  const auto document = read_svg(
      "<svg width=\"100\" height=\"60\">"
      "<rect id=\"Bottom\" x=\"0\" y=\"0\" width=\"50\" height=\"50\" fill=\"red\"/>"
      "<g id=\"Middle\"><circle id=\"Inner1\" cx=\"20\" cy=\"20\" r=\"5\"/>"
      "<circle id=\"Inner2\" cx=\"40\" cy=\"20\" r=\"5\"/></g>"
      "<rect id=\"Top\" x=\"10\" y=\"10\" width=\"10\" height=\"10\" fill=\"blue\" display=\"none\"/>"
      "</svg>");
  // layers()[0] composites first = bottom; SVG paints first-to-last.
  CHECK(document.layers().size() == 3);
  CHECK(document.layers()[0].name() == "Bottom");
  CHECK(document.layers()[1].name() == "Middle");
  CHECK(document.layers()[1].kind() == LayerKind::Group);
  CHECK(document.layers()[1].children().size() == 2);
  CHECK(document.layers()[1].children()[0].name() == "Inner1");
  CHECK(document.layers()[2].name() == "Top");
  CHECK(!document.layers()[2].visible());  // display:none
  CHECK(document.layers()[0].visible());
  CHECK(patchy::layer_is_vector_shape(document.layers()[0]));
  // Baked pixel cache exists (never rasterized per repaint).
  CHECK(!std::as_const(document.layers()[0]).pixels().empty());
}

void svg_import_styles_cascade_and_colors() {
  std::vector<std::string> notices;
  const auto document = read_svg(
      "<svg width=\"60\" height=\"60\">"
      "<style>rect{fill:#00ff00;} .warm{fill:hsl(0,100%,50%);} #Special{fill:rgb(0,0,255);}</style>"
      // Presentation attribute loses to the type rule.
      "<rect id=\"A\" x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"black\"/>"
      // Class rule beats the type rule.
      "<rect id=\"B\" class=\"warm\" x=\"0\" y=\"12\" width=\"10\" height=\"10\"/>"
      // Id rule beats class; inline style beats everything.
      "<rect id=\"Special\" class=\"warm\" x=\"0\" y=\"24\" width=\"10\" height=\"10\"/>"
      "<rect id=\"D\" x=\"0\" y=\"36\" width=\"10\" height=\"10\" fill=\"green\" style=\"fill:papayawhip\"/>"
      // currentColor resolves through the inherited color property (a circle:
      // the rect type rule above would legitimately override a rect's
      // presentation attribute).
      "<g color=\"rgb(10,20,30)\"><circle id=\"E\" cx=\"5\" cy=\"53\" r=\"5\" fill=\"currentColor\"/></g>"
      "</svg>",
      &notices);
  const auto fill_of = [&](std::size_t index) { return document.layers()[index].vector_shape()->fill.color; };
  CHECK(fill_of(0).green == 255 && fill_of(0).red == 0);
  CHECK(fill_of(1).red == 255 && fill_of(1).green == 0);
  CHECK(fill_of(2).blue == 255 && fill_of(2).red == 0);
  CHECK(fill_of(3).red == 255 && fill_of(3).green == 239 && fill_of(3).blue == 213);  // papayawhip
  const auto& group = document.layers()[4];
  CHECK(group.children().size() == 1);
  const auto color = group.children()[0].vector_shape()->fill.color;
  CHECK(color.red == 10 && color.green == 20 && color.blue == 30);
}

void svg_import_opacity_and_blend() {
  const auto document = read_svg(
      "<svg width=\"40\" height=\"40\">"
      "<rect id=\"A\" width=\"10\" height=\"10\" fill=\"#ff0000\" opacity=\"0.5\" fill-opacity=\"0.5\"/>"
      "<rect id=\"B\" width=\"10\" height=\"10\" fill=\"rgba(255,0,0,0.5)\"/>"
      "<rect id=\"C\" width=\"10\" height=\"10\" fill=\"#ff0000\" style=\"mix-blend-mode:multiply\"/>"
      "<rect id=\"D\" width=\"10\" height=\"10\" fill=\"#ff0000\" stroke=\"#000000\" style=\"stroke-width:3px\"/>"
      "</svg>");
  CHECK(std::abs(document.layers()[0].opacity() - 0.25F) < 0.001F);  // opacity x fill-opacity
  CHECK(std::abs(document.layers()[1].opacity() - 0.5F) < 0.001F);   // color alpha folds into opacity
  CHECK(document.layers()[2].blend_mode() == BlendMode::Multiply);
  CHECK(document.layers()[0].blend_mode() == BlendMode::Normal);
  // CSS-ish "px" suffixes parse as their leading number.
  CHECK(document.layers()[3].vector_shape()->stroke.enabled);
  CHECK(std::abs(document.layers()[3].vector_shape()->stroke.width - 3.0) < 1e-9);
}

// A stroke more opaque than a translucent fill cannot ride the fill layer's
// opacity, so the element splits into a fill layer and a fill-less stroke
// layer above it; an opaque white outline on an rgba(…, 0.2) fill used to be
// clamped to 20% and disappear (GitHub issue 111).
void svg_import_splits_strokes_more_opaque_than_their_fill() {
  std::vector<std::string> notices;
  const auto document = read_svg(
      "<svg width=\"800\" height=\"600\">"
      "<circle cx=\"400\" cy=\"300\" r=\"120\" fill=\"rgba(255,255,255,0.2)\" stroke=\"white\" stroke-width=\"2\"/>"
      "<rect x=\"200\" y=\"150\" width=\"400\" height=\"300\" fill=\"none\" stroke=\"white\" stroke-width=\"2\"/>"
      "<polygon points=\"400,100 500,250 300,250\" fill=\"rgba(255,200,50,0.7)\" stroke=\"white\"/>"
      "<rect width=\"10\" height=\"10\" fill=\"#ff0000\" stroke=\"#000000\" opacity=\"0.5\"/>"
      "</svg>",
      &notices);
  const auto& layers = document.layers();
  CHECK(layers.size() == 6);
  if (layers.size() != 6) {
    return;
  }
  // Circle: the fill half keeps the 20% alpha, the stroke half is opaque.
  CHECK(layers[0].vector_shape() != nullptr && layers[0].vector_shape()->fill.kind != VectorFillKind::None);
  CHECK(!layers[0].vector_shape()->stroke.enabled);
  CHECK(std::abs(layers[0].opacity() - 0.2F) < 0.001F);
  CHECK(layers[1].name() == layers[0].name() + " Stroke");
  CHECK(layers[1].vector_shape() != nullptr && layers[1].vector_shape()->fill.kind == VectorFillKind::None);
  CHECK(layers[1].vector_shape()->stroke.enabled);
  CHECK(std::abs(layers[1].vector_shape()->stroke.width - 2.0) < 1e-9);
  CHECK(std::abs(layers[1].vector_shape()->stroke.opacity - 1.0) < 1e-9);
  CHECK(std::abs(layers[1].opacity() - 1.0F) < 0.001F);
  CHECK(layers[1].vector_shape()->path.subpaths.size() == layers[0].vector_shape()->path.subpaths.size());
  // A fill-less rect keeps its stroke on one layer.
  CHECK(layers[2].vector_shape()->stroke.enabled && layers[2].vector_shape()->fill.kind == VectorFillKind::None);
  // Polygon at 70%: split too, the default 1 px white stroke opaque.
  CHECK(std::abs(layers[3].opacity() - 0.7F) < 0.001F);
  CHECK(!layers[3].vector_shape()->stroke.enabled);
  CHECK(layers[4].vector_shape()->stroke.enabled && std::abs(layers[4].opacity() - 1.0F) < 0.001F);
  // Element opacity scales both halves alike, so an opaque fill with an opaque
  // stroke at opacity 0.5 stays one layer.
  CHECK(layers[5].vector_shape()->stroke.enabled && std::abs(layers[5].opacity() - 0.5F) < 0.001F);
  for (const auto& notice : notices) {
    CHECK(notice.find("more opaque") == std::string::npos);
  }
}

// --- live shapes -------------------------------------------------------------

void svg_import_live_shapes_and_transform_gating() {
  const auto document = read_svg(
      "<svg width=\"200\" height=\"200\">"
      "<rect id=\"R\" x=\"10\" y=\"20\" width=\"30\" height=\"40\" fill=\"red\"/>"
      "<rect id=\"RR\" x=\"10\" y=\"70\" width=\"30\" height=\"20\" rx=\"5\" fill=\"red\"/>"
      "<circle id=\"C\" cx=\"100\" cy=\"40\" r=\"25\" fill=\"blue\"/>"
      "<ellipse id=\"E\" cx=\"100\" cy=\"120\" rx=\"30\" ry=\"15\" fill=\"blue\"/>"
      "<line id=\"L\" x1=\"10\" y1=\"150\" x2=\"90\" y2=\"180\" stroke=\"#123456\" stroke-width=\"6\"/>"
      "<rect id=\"Scaled\" x=\"5\" y=\"5\" width=\"10\" height=\"10\" fill=\"red\" transform=\"translate(100 100) scale(2)\"/>"
      "<rect id=\"Rotated\" x=\"5\" y=\"5\" width=\"10\" height=\"10\" fill=\"red\" transform=\"rotate(30)\"/>"
      "</svg>");
  const auto live_kind = [&](std::size_t index) {
    const auto* shape = document.layers()[index].vector_shape();
    return shape->origination.empty() ? LiveShapeKind::None : shape->origination.front().kind;
  };
  CHECK(live_kind(0) == LiveShapeKind::Rectangle);
  CHECK(live_kind(1) == LiveShapeKind::RoundedRectangle);
  CHECK(std::abs(document.layers()[1].vector_shape()->origination.front().corner_radii[0] - 5.0) < 1e-9);
  CHECK(live_kind(2) == LiveShapeKind::Ellipse);
  CHECK(live_kind(3) == LiveShapeKind::Ellipse);
  // A plain stroked line becomes the live Line quad with the stroke paint as fill.
  CHECK(live_kind(4) == LiveShapeKind::Line);
  const auto* line_shape = document.layers()[4].vector_shape();
  CHECK(!line_shape->stroke.enabled);
  CHECK(line_shape->fill.color.red == 0x12 && line_shape->fill.color.blue == 0x56);
  CHECK(std::abs(line_shape->origination.front().line_weight - 6.0) < 1e-9);
  // Positive axis-aligned scale + translate keeps live parameters...
  CHECK(live_kind(5) == LiveShapeKind::Rectangle);
  const auto& scaled = document.layers()[5].vector_shape()->origination.front();
  CHECK(std::abs(scaled.left - 110.0) < 1e-9 && std::abs(scaled.right - 130.0) < 1e-9);
  // ...rotation drops them (the keyShapeInvalidated rule); the path stays.
  CHECK(live_kind(6) == LiveShapeKind::None);
  CHECK(!document.layers()[6].vector_shape()->path.subpaths.empty());
}

// --- gradients ---------------------------------------------------------------

void svg_import_gradients() {
  const auto document = read_svg(
      "<svg width=\"100\" height=\"100\">"
      "<defs>"
      "<linearGradient id=\"base\"><stop offset=\"0\" stop-color=\"#ff0000\"/>"
      "<stop offset=\"1\" stop-color=\"#0000ff\" stop-opacity=\"0.5\"/></linearGradient>"
      "<linearGradient id=\"Grad\" href=\"#base\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"0%\"/>"
      "<radialGradient id=\"Rad\" cx=\"50%\" cy=\"50%\" r=\"50%\">"
      "<stop offset=\"0\" stop-color=\"#ffffff\"/><stop offset=\"1\" stop-color=\"#000000\"/></radialGradient>"
      "<linearGradient id=\"Mirror\" spreadMethod=\"reflect\"><stop offset=\"0\" stop-color=\"#ffffff\"/>"
      "<stop offset=\"1\" stop-color=\"#000000\"/></linearGradient>"
      "</defs>"
      "<rect id=\"A\" width=\"100\" height=\"50\" fill=\"url(#Grad)\"/>"
      "<circle id=\"B\" cx=\"50\" cy=\"75\" r=\"20\" fill=\"url(#Rad)\"/>"
      "<rect id=\"CaseSensitive\" y=\"60\" width=\"40\" height=\"10\" fill=\"url(#Mirror)\"/>"
      "</svg>");
  const auto& linear = document.layers()[0].vector_shape()->fill;
  CHECK(linear.kind == VectorFillKind::Gradient);
  CHECK(linear.gradient.type == patchy::LayerStyleGradientType::Linear);
  CHECK(linear.gradient.color_stops.size() == 2);  // stops inherited through href
  CHECK(linear.gradient.color_stops.front().color.red == 255);
  CHECK(std::abs(linear.gradient.alpha_stops.back().opacity - 0.5F) < 0.001F);
  CHECK(std::abs(linear.gradient.angle_degrees - 0.0F) < 0.5F);  // left-to-right
  CHECK(linear.gradient.smoothness == 0);                        // SVG stops interpolate linearly
  const auto& radial = document.layers()[1].vector_shape()->fill;
  CHECK(radial.gradient.type == patchy::LayerStyleGradientType::Radial);
  const auto& mirrored = document.layers()[2].vector_shape()->fill;
  CHECK(mirrored.gradient.type == patchy::LayerStyleGradientType::Reflected);
}

// Where an imported gradient actually lands, in document pixels: the model (angle,
// scale, offsets against the canvas or the path bounds) read back through the
// export geometry, which is the import mapping inverted.
const Layer* find_layer_named(const std::vector<Layer>& layers, std::string_view name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* child = find_layer_named(layer.children(), name); child != nullptr) {
      return child;
    }
  }
  return nullptr;
}

patchy::vector_export::GradientExportGeometry gradient_geometry(const Document& document, std::string_view name) {
  const auto* layer = find_layer_named(document.layers(), name);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  if (layer == nullptr || layer->vector_shape() == nullptr) {
    return {};
  }
  const auto& shape = *layer->vector_shape();
  CHECK(shape.fill.kind == VectorFillKind::Gradient);
  return patchy::vector_export::gradient_export_geometry(shape.fill.gradient, shape.path, document.width(),
                                                         document.height());
}

bool ramp_runs(const patchy::vector_export::GradientExportGeometry& geometry, double x1, double y1, double x2,
               double y2) {
  constexpr double tolerance = 0.5;  // the model stores float angles and percentages
  return std::abs(geometry.x1 - x1) < tolerance && std::abs(geometry.y1 - y1) < tolerance &&
         std::abs(geometry.x2 - x2) < tolerance && std::abs(geometry.y2 - y2) < tolerance;
}

// userSpaceOnUse coordinates live in the painted element's user space, so they take
// the same viewBox mapping and ancestor transforms as its path. They used to stay in
// raw user units: a 2x viewBox halved the ramp and a translated group lost it.
constexpr std::string_view kUserSpaceGradientSvg =
    "<svg width=\"800\" height=\"400\" viewBox=\"0 0 400 200\">"
    "<defs>"
    "<linearGradient id=\"across\" gradientUnits=\"userSpaceOnUse\" x1=\"20\" y1=\"0\" x2=\"80\" y2=\"0\">"
    "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient>"
    "<linearGradient id=\"diagonal\" gradientUnits=\"userSpaceOnUse\" x1=\"320\" y1=\"120\" x2=\"360\" y2=\"160\">"
    "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient>"
    "<linearGradient id=\"percent\" gradientUnits=\"userSpaceOnUse\" x1=\"0%\" y1=\"50%\" x2=\"50%\" y2=\"50%\">"
    "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient>"
    "<radialGradient id=\"spot\" gradientUnits=\"userSpaceOnUse\" cx=\"50\" cy=\"150\" r=\"20\">"
    "<stop offset=\"0\" stop-color=\"#ffffff\"/><stop offset=\"1\" stop-color=\"#000000\"/></radialGradient>"
    "</defs>"
    "<rect id=\"Scaled\" x=\"20\" y=\"20\" width=\"60\" height=\"60\" fill=\"url(#across)\"/>"
    "<g transform=\"translate(200 0)\"><rect id=\"Moved\" x=\"20\" y=\"20\" width=\"60\" height=\"60\" fill=\"url(#across)\"/></g>"
    "<rect id=\"Diagonal\" x=\"320\" y=\"120\" width=\"60\" height=\"60\" fill=\"url(#diagonal)\"/>"
    "<rect id=\"Percent\" x=\"0\" y=\"90\" width=\"400\" height=\"20\" fill=\"url(#percent)\"/>"
    "<g transform=\"translate(10 0)\"><circle id=\"Spot\" cx=\"50\" cy=\"150\" r=\"30\" fill=\"url(#spot)\"/></g>"
    "</svg>";

void check_user_space_gradient_geometry(const Document& document) {
  CHECK(document.width() == 800 && document.height() == 400);
  CHECK(!find_layer_named(document.layers(), "Scaled")->vector_shape()->fill.gradient.align_with_layer);
  CHECK(ramp_runs(gradient_geometry(document, "Scaled"), 40.0, 0.0, 160.0, 0.0));     // the viewBox scale
  CHECK(ramp_runs(gradient_geometry(document, "Moved"), 440.0, 0.0, 560.0, 0.0));     // plus the group translate
  CHECK(ramp_runs(gradient_geometry(document, "Diagonal"), 640.0, 240.0, 720.0, 320.0));
  CHECK(ramp_runs(gradient_geometry(document, "Percent"), 0.0, 200.0, 400.0, 200.0));  // shares of the viewport
  const auto spot = gradient_geometry(document, "Spot");
  CHECK(std::abs(spot.center_x - 120.0) < 0.5 && std::abs(spot.center_y - 300.0) < 0.5);
  CHECK(std::abs(spot.radius - 40.0) < 0.5);
}

void svg_import_user_space_gradients_follow_the_element_transform() {
  check_user_space_gradient_geometry(read_svg(kUserSpaceGradientSvg));
}

// objectBoundingBox coordinates resolve in the unit square of the element's own box:
// gradientTransform applies there, a diagonal ramp's stripes tilt with a non-square
// box, and a rotated element carries its ramp around with it.
void svg_import_bounding_box_gradients_resolve_in_the_element_box() {
  const auto document = read_svg(
      "<svg width=\"300\" height=\"260\">"
      "<defs>"
      "<linearGradient id=\"corner\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
      "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient>"
      "<linearGradient id=\"turned\" gradientTransform=\"rotate(90 0.5 0.5)\">"
      "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient>"
      "<linearGradient id=\"plain\">"
      "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient>"
      "</defs>"
      "<rect id=\"Corner\" x=\"50\" y=\"50\" width=\"200\" height=\"100\" fill=\"url(#corner)\"/>"
      "<rect id=\"Turned\" x=\"50\" y=\"50\" width=\"200\" height=\"100\" fill=\"url(#turned)\"/>"
      "<rect id=\"Rotated\" x=\"50\" y=\"180\" width=\"100\" height=\"40\" transform=\"rotate(90 100 200)\" "
      "fill=\"url(#plain)\"/>"
      "</svg>");
  CHECK(find_layer_named(document.layers(), "Corner")->vector_shape()->fill.gradient.align_with_layer);
  // The stripes stay parallel to the box's other diagonal; the ramp across them is
  // what a browser draws for (0,0) -> (1,1) on a 2:1 box.
  CHECK(ramp_runs(gradient_geometry(document, "Corner"), 50.0, 50.0, 130.0, 210.0));
  // rotate(90) about the box center turns the top edge into the right edge: top-to-bottom.
  CHECK(ramp_runs(gradient_geometry(document, "Turned"), 250.0, 50.0, 250.0, 150.0));
  // The element's own rotation: its left-to-right ramp runs down the document.
  CHECK(ramp_runs(gradient_geometry(document, "Rotated"), 120.0, 150.0, 120.0, 250.0));
}

// A canvas-anchored gradient (align_with_layer off) exports against the canvas, the
// box the renderer uses, so the file re-imports to the same ramps.
void svg_export_reimport_keeps_user_space_gradients() {
  const auto document = read_svg(kUserSpaceGradientSvg);
  const auto text = write_svg(document);
  CHECK(text.find("<image") == std::string::npos);  // still vector
  check_user_space_gradient_geometry(read_svg(text));
}

// --- patterns ----------------------------------------------------------------

// An imported pattern fill with the tile it paints from and the layer it was baked into.
struct ImportedPattern {
  const Layer* layer{nullptr};
  patchy::VectorFill fill{};
  PixelBuffer tile{};
};

ImportedPattern imported_pattern(const Document& document, std::string_view name) {
  ImportedPattern result;
  result.layer = find_layer_named(document.layers(), name);
  CHECK(result.layer != nullptr && result.layer->vector_shape() != nullptr);
  result.fill = result.layer->vector_shape()->fill;
  CHECK(result.fill.kind == VectorFillKind::Pattern);
  const auto* resource = document.metadata().patterns.find(result.fill.pattern_id);
  CHECK(resource != nullptr && !resource->tile.empty());
  result.tile = resource->tile;
  return result;
}

// A layer's baked pixel at a document position; transparent outside its bounds.
std::array<std::uint8_t, 4> baked_pixel(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto bounds = layer.bounds();
  if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
    return {};
  }
  const auto* pixel = layer.pixels().pixel(x - bounds.x, y - bounds.y);
  return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

bool painted(const Layer& layer, std::int32_t x, std::int32_t y, std::uint8_t red, std::uint8_t green,
             std::uint8_t blue) {
  return baked_pixel(layer, x, y) == std::array<std::uint8_t, 4>{red, green, blue, 255};
}

bool clear_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  return baked_pixel(layer, x, y)[3] == 0;
}

// The opaque runs a baked row has between two document columns: one per tile of a checker.
int opaque_runs(const Layer& layer, std::int32_t y, std::int32_t from_x, std::int32_t to_x) {
  int runs = 0;
  bool inside = false;
  for (std::int32_t x = from_x; x < to_x; ++x) {
    const bool opaque = baked_pixel(layer, x, y)[3] == 255;
    runs += opaque && !inside ? 1 : 0;
    inside = opaque;
  }
  return runs;
}

bool placed_at(const patchy::VectorFill& fill, double x, double y) {
  return std::abs(fill.pattern_phase_x - x) < 1e-6 && std::abs(fill.pattern_phase_y - y) < 1e-6;
}

// A pattern's tile lives in the painted element's user space, so its size, phase and
// content take the same viewBox mapping and ancestor transforms as the element's path.
// They used to stay in raw user units: under this 2x viewBox the 20-unit checker came
// out with 20 px tiles, eight across each 160 px square instead of four.
constexpr std::string_view kUserSpacePatternSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"200\" viewBox=\"0 0 200 100\">"
    "<defs>"
    "<pattern id=\"checks\" patternUnits=\"userSpaceOnUse\" width=\"20\" height=\"20\">"
    "<rect width=\"10\" height=\"10\" fill=\"#000000\"/>"
    "<rect x=\"10\" y=\"10\" width=\"10\" height=\"10\" fill=\"#000000\"/>"
    "</pattern>"
    "</defs>"
    "<rect width=\"200\" height=\"100\" fill=\"#ffffff\"/>"
    "<rect id=\"Scaled\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"url(#checks)\"/>"
    "<g transform=\"translate(100 0)\">"
    "<rect id=\"Moved\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"url(#checks)\"/></g>"
    "</svg>";

void check_user_space_pattern_placement(const Document& document) {
  CHECK(document.width() == 400 && document.height() == 200);
  const auto scaled = imported_pattern(document, "Scaled");
  // Drawn at document resolution: a 40 px tile placed unscaled, so the checks stay sharp.
  CHECK(scaled.tile.width() == 40 && scaled.tile.height() == 40);
  CHECK(scaled.fill.pattern_scale == 1.0);
  CHECK(scaled.fill.pattern_angle_degrees == 0.0);
  CHECK(!scaled.fill.pattern_linked);
  CHECK(placed_at(scaled.fill, 0.0, 0.0));
  CHECK(opaque_runs(*scaled.layer, 30, 20, 180) == 4);  // four tiles across the 160 px square
  CHECK(painted(*scaled.layer, 30, 30, 0, 0, 0));
  CHECK(painted(*scaled.layer, 39, 30, 0, 0, 0) && clear_at(*scaled.layer, 40, 30));  // a hard edge
  CHECK(clear_at(*scaled.layer, 50, 30) && clear_at(*scaled.layer, 30, 50));
  CHECK(painted(*scaled.layer, 50, 50, 0, 0, 0));
  // The group translate carries the grid along with the square.
  const auto moved = imported_pattern(document, "Moved");
  CHECK(moved.tile.width() == 40 && moved.tile.height() == 40);
  CHECK(moved.fill.pattern_scale == 1.0);
  CHECK(placed_at(moved.fill, 200.0, 0.0));
  CHECK(opaque_runs(*moved.layer, 30, 220, 380) == 4);
  CHECK(painted(*moved.layer, 230, 30, 0, 0, 0) && clear_at(*moved.layer, 250, 30));
}

void svg_import_user_space_patterns_follow_the_element_transform() {
  const auto repro = read_svg(kUserSpacePatternSvg);
  check_user_space_pattern_placement(repro);
  // Both squares draw the same tile: one PatternStore entry, not a copy each.
  CHECK(repro.metadata().patterns.patterns.size() == 1);

  // The tile's x/y and patternTransform ride the same chain, and a translate that is
  // not a whole tile shows up as the grid's anchor.
  std::vector<std::string> notices;
  const auto document = read_svg(
      "<svg width=\"400\" height=\"200\" viewBox=\"0 0 200 100\">"
      "<defs>"
      "<pattern id=\"checks\" patternUnits=\"userSpaceOnUse\" width=\"20\" height=\"20\">"
      "<rect width=\"10\" height=\"10\" fill=\"#000000\"/>"
      "<rect x=\"10\" y=\"10\" width=\"10\" height=\"10\" fill=\"#000000\"/>"
      "</pattern>"
      "<pattern id=\"big\" href=\"#checks\" x=\"5\" y=\"5\" patternTransform=\"scale(2)\"/>"
      "</defs>"
      "<g transform=\"translate(105 5)\">"
      "<rect id=\"Offset\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"url(#checks)\"/></g>"
      "<rect id=\"Doubled\" x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"url(#big)\"/>"
      "</svg>",
      &notices);
  CHECK(notices.empty());
  const auto offset = imported_pattern(document, "Offset");
  CHECK(offset.tile.width() == 40 && offset.fill.pattern_scale == 1.0);
  CHECK(placed_at(offset.fill, 210.0, 10.0));
  CHECK(painted(*offset.layer, 240, 40, 0, 0, 0) && clear_at(*offset.layer, 260, 40));
  const auto doubled = imported_pattern(document, "Doubled");
  CHECK(doubled.tile.width() == 80 && doubled.tile.height() == 80);  // 20 units x scale(2) x the 2x viewBox
  CHECK(doubled.fill.pattern_scale == 1.0);
  CHECK(placed_at(doubled.fill, 20.0, 20.0));
  CHECK(painted(*doubled.layer, 30, 30, 0, 0, 0) && clear_at(*doubled.layer, 70, 30));
  CHECK(painted(*doubled.layer, 70, 70, 0, 0, 0));

  // A tile that is not a whole number of document pixels keeps its exact period: the
  // nearest whole tile, scaled by the remainder.
  const auto fractional = read_svg(
      "<svg width=\"150\" height=\"150\" viewBox=\"0 0 100 100\">"
      "<defs><pattern id=\"bars\" patternUnits=\"userSpaceOnUse\" width=\"7\" height=\"7\">"
      "<rect width=\"3\" height=\"7\" fill=\"#ff0000\"/></pattern></defs>"
      "<rect id=\"Odd\" width=\"100\" height=\"100\" fill=\"url(#bars)\"/>"
      "</svg>");
  const auto odd = imported_pattern(fractional, "Odd");
  CHECK(odd.tile.width() == 11 && odd.tile.height() == 11);  // 10.5 px
  CHECK(std::abs(odd.fill.pattern_scale - 10.5 / 11.0) < 1e-9);

  // A pattern painted with a pattern (here with itself, which would never end) is
  // refused where it nests: that child paints gray.
  const auto looped = read_svg(
      "<svg width=\"40\" height=\"40\">"
      "<defs><pattern id=\"loop\" patternUnits=\"userSpaceOnUse\" width=\"10\" height=\"10\">"
      "<rect width=\"5\" height=\"5\" fill=\"url(#loop)\"/></pattern></defs>"
      "<rect id=\"Loop\" width=\"40\" height=\"40\" fill=\"url(#loop)\"/>"
      "</svg>",
      &notices);
  const auto loop = imported_pattern(looped, "Loop");
  CHECK(loop.tile.width() == 10 && loop.tile.height() == 10);
  CHECK(painted(*loop.layer, 2, 2, 128, 128, 128) && clear_at(*loop.layer, 7, 7));
  CHECK(std::ranges::any_of(notices, [](const std::string& text) { return text.find("Nested") != std::string::npos; }));
}

// objectBoundingBox units (the default) are fractions of the element's own box, measured
// from its corner; patternContentUnits scales the content the same way.
void svg_import_bounding_box_patterns_resolve_in_the_element_box() {
  std::vector<std::string> notices;
  const auto document = read_svg(
      "<svg width=\"400\" height=\"200\" viewBox=\"0 0 200 100\">"
      "<defs>"
      "<pattern id=\"halves\" width=\"0.5\" height=\"0.5\"><rect width=\"20\" height=\"10\" fill=\"#ff0000\"/></pattern>"
      "<pattern id=\"percent\" href=\"#halves\" width=\"50%\" height=\"50%\"/>"
      "<pattern id=\"fractions\" width=\"0.5\" height=\"0.5\" patternContentUnits=\"objectBoundingBox\">"
      "<rect width=\"0.25\" height=\"0.25\" fill=\"#ff0000\"/></pattern>"
      "</defs>"
      "<rect id=\"Box\" x=\"10\" y=\"10\" width=\"80\" height=\"40\" fill=\"url(#halves)\"/>"
      "<rect id=\"Percent\" x=\"110\" y=\"10\" width=\"80\" height=\"40\" fill=\"url(#percent)\"/>"
      "<g transform=\"translate(100 0)\">"
      "<rect id=\"Fractions\" x=\"10\" y=\"50\" width=\"80\" height=\"40\" fill=\"url(#fractions)\"/></g>"
      "</svg>",
      &notices);
  CHECK(notices.empty());
  // Half of the 80 x 40 unit box is a 40 x 20 unit tile: 80 x 40 px, anchored at the
  // box corner.
  const auto box = imported_pattern(document, "Box");
  CHECK(box.tile.width() == 80 && box.tile.height() == 40);
  CHECK(box.fill.pattern_scale == 1.0);
  CHECK(placed_at(box.fill, 20.0, 20.0));
  CHECK(painted(*box.layer, 30, 30, 255, 0, 0));    // the 40 x 20 px red corner of the first tile
  CHECK(clear_at(*box.layer, 70, 30) && clear_at(*box.layer, 30, 45));
  CHECK(painted(*box.layer, 110, 30, 255, 0, 0));   // the second tile across
  CHECK(painted(*box.layer, 30, 65, 255, 0, 0));    // and down
  // A percentage is the same fraction, so the second box reuses the first one's tile.
  const auto percent = imported_pattern(document, "Percent");
  CHECK(percent.fill.pattern_id == box.fill.pattern_id);
  CHECK(placed_at(percent.fill, 220.0, 20.0));
  CHECK(painted(*percent.layer, 230, 30, 255, 0, 0) && clear_at(*percent.layer, 270, 30));
  // Content in box fractions draws the same tile, and the group translate moves the box.
  const auto fractions = imported_pattern(document, "Fractions");
  CHECK(fractions.tile.width() == 80 && fractions.tile.height() == 40);
  CHECK(std::ranges::equal(std::as_const(fractions.tile).data(), std::as_const(box.tile).data()));
  CHECK(placed_at(fractions.fill, 220.0, 100.0));
  CHECK(painted(*fractions.layer, 230, 110, 255, 0, 0) && clear_at(*fractions.layer, 270, 110));
}

// What a tile's pixels cannot carry rides the placement model: a rotation becomes the
// pattern angle (counterclockwise-positive, so an SVG rotate() flips sign). Unequal
// axis scales and mirrors are baked into the tile; only skew is approximated.
void svg_import_rotated_and_mirrored_patterns() {
  constexpr std::string_view kBands =
      "<pattern id=\"bands\" patternUnits=\"userSpaceOnUse\" width=\"20\" height=\"20\">"
      "<rect width=\"20\" height=\"10\" fill=\"#0000ff\"/></pattern>";  // blue over clear
  std::vector<std::string> notices;
  const auto document = read_svg(
      "<svg width=\"400\" height=\"100\">"
      "<defs>" + std::string(kBands) +
      "<pattern id=\"turned\" href=\"#bands\" patternTransform=\"rotate(90)\"/>"
      "</defs>"
      "<rect id=\"Turned\" width=\"100\" height=\"100\" fill=\"url(#turned)\"/>"
      "<g transform=\"translate(100 0)\">"
      "<rect id=\"Spun\" width=\"40\" height=\"40\" transform=\"rotate(90 50 50)\" fill=\"url(#bands)\"/></g>"
      "<g transform=\"translate(200 100) scale(1 -1)\">"
      "<rect id=\"Flipped\" width=\"100\" height=\"100\" fill=\"url(#bands)\"/></g>"
      "<g transform=\"translate(300 0)\">"
      "<rect id=\"Stretched\" width=\"10\" height=\"10\" transform=\"scale(2 3)\" fill=\"url(#bands)\"/></g>"
      "</svg>",
      &notices);
  CHECK(notices.empty());
  // rotate(90) stands the bands up: pattern y runs along -x, so the blue half of
  // each period is its right half.
  const auto turned = imported_pattern(document, "Turned");
  CHECK(turned.tile.width() == 20 && turned.tile.height() == 20);
  CHECK(turned.fill.pattern_scale == 1.0);
  CHECK(std::abs(turned.fill.pattern_angle_degrees + 90.0) < 1e-9);
  CHECK(painted(*turned.layer, 15, 50, 0, 0, 255) && clear_at(*turned.layer, 5, 50));
  CHECK(painted(*turned.layer, 35, 50, 0, 0, 255) && clear_at(*turned.layer, 25, 50));
  // The element's own rotation does the same and carries the anchor around with it:
  // user (0, 0) lands on (100, 0) of the group, (200, 0) of the document.
  const auto spun = imported_pattern(document, "Spun");
  CHECK(std::abs(spun.fill.pattern_angle_degrees + 90.0) < 1e-9);
  CHECK(placed_at(spun.fill, 200.0, 0.0));
  CHECK(painted(*spun.layer, 195, 20, 0, 0, 255) && clear_at(*spun.layer, 185, 20));
  // A mirror is baked into the tile (its rows reversed), not approximated.
  const auto flipped = imported_pattern(document, "Flipped");
  CHECK(flipped.fill.pattern_angle_degrees == 0.0 && flipped.fill.pattern_scale == 1.0);
  CHECK(placed_at(flipped.fill, 200.0, 100.0));
  CHECK(std::as_const(flipped.tile).pixel(0, 5)[3] == 0 && std::as_const(flipped.tile).pixel(0, 15)[3] == 255);
  CHECK(painted(*flipped.layer, 250, 95, 0, 0, 255) && clear_at(*flipped.layer, 250, 85));
  // Unequal axis scales are baked in too.
  const auto stretched = imported_pattern(document, "Stretched");
  CHECK(stretched.tile.width() == 40 && stretched.tile.height() == 60);
  CHECK(stretched.fill.pattern_scale == 1.0);
  CHECK(std::as_const(stretched.tile).pixel(0, 29)[3] == 255 && std::as_const(stretched.tile).pixel(0, 30)[3] == 0);

  const auto skewed = read_svg(
      "<svg width=\"100\" height=\"100\"><defs>" + std::string(kBands) +
          "</defs><rect id=\"Skewed\" width=\"50\" height=\"50\" transform=\"skewX(30)\" fill=\"url(#bands)\"/></svg>",
      &notices);
  (void)imported_pattern(skewed, "Skewed");  // still a pattern fill
  CHECK(notices.size() == 1 && notices.front().find("skew") != std::string::npos);
}

// Patchy writes a pattern fill as a <pattern> holding the tile as one embedded image
// that fills the cell, and reads that form back as the same tile and placement.
void svg_export_reimport_keeps_pattern_fills() {
  const auto document = read_svg(kUserSpacePatternSvg);
  std::vector<std::string> notices;
  const auto text = write_svg(document, &notices);
  CHECK(notices.empty());
  CHECK(text.find("<pattern id=\"pat1\" patternUnits=\"userSpaceOnUse\" width=\"40\" height=\"40\">") !=
        std::string::npos);
  const auto reimported = read_svg(text, &notices);
  CHECK(notices.empty());
  check_user_space_pattern_placement(reimported);
  const auto before = imported_pattern(document, "Moved");
  const auto after = imported_pattern(reimported, "Moved");
  CHECK(std::ranges::equal(std::as_const(after.tile).data(), std::as_const(before.tile).data()));
  CHECK(std::ranges::equal(after.layer->pixels().data(), before.layer->pixels().data()));

  // Scale, angle and phase make the trip as the cell size and patternTransform. The
  // pattern angle is counterclockwise-positive, an SVG rotate() clockwise.
  auto turned = read_svg(kUserSpacePatternSvg);
  auto& layer = turned.layers()[1];
  CHECK(layer.name() == "Scaled");
  auto content = *layer.vector_shape();
  content.fill.pattern_scale = 1.5;
  content.fill.pattern_angle_degrees = 30.0;
  content.fill.pattern_phase_x = 7.0;
  content.fill.pattern_phase_y = 3.0;
  layer.set_vector_shape(std::move(content));
  patchy::update_vector_shape_raster(layer, Rect::from_size(turned.width(), turned.height()),
                                     &turned.metadata().patterns);
  const auto turned_text = write_svg(turned);
  CHECK(turned_text.find("width=\"60\" height=\"60\" patternTransform=\"translate(7 3) rotate(-30)\"") !=
        std::string::npos);
  const auto returned = read_svg(turned_text, &notices);
  CHECK(notices.empty());
  const auto kept = imported_pattern(returned, "Scaled");
  CHECK(kept.tile.width() == 40 && kept.tile.height() == 40);
  CHECK(std::abs(kept.fill.pattern_scale - 1.5) < 1e-9);
  CHECK(std::abs(kept.fill.pattern_angle_degrees - 30.0) < 1e-9);
  CHECK(placed_at(kept.fill, 7.0, 3.0));
  const auto& sent = std::as_const(turned).layers()[1];
  CHECK(kept.layer->bounds().width == sent.bounds().width);
  CHECK(std::ranges::equal(kept.layer->pixels().data(), sent.pixels().data()));

  // An image that does not fill its tile would need resampling: gray, with a notice.
  const auto partial = read_svg(
      "<svg width=\"50\" height=\"50\"><defs>"
      "<pattern id=\"corner\" patternUnits=\"userSpaceOnUse\" width=\"20\" height=\"20\">"
      "<image width=\"10\" height=\"10\" href=\"data:image/png;base64,AAAA\"/></pattern></defs>"
      "<rect id=\"Partial\" width=\"50\" height=\"50\" fill=\"url(#corner)\"/></svg>",
      &notices);
  CHECK(find_layer_named(partial.layers(), "Partial")->vector_shape()->fill.kind == VectorFillKind::Solid);
  CHECK(!notices.empty());
}

// --- fill rules --------------------------------------------------------------

void svg_import_fill_rules() {
  const auto document = read_svg(
      "<svg width=\"200\" height=\"100\">"
      // Even-odd donut: both subpaths share one group (our exact rule).
      "<path id=\"EvenOdd\" fill-rule=\"evenodd\" fill=\"red\" d=\"M10 10H90V90H10Z M30 30H70V70H30Z\"/>"
      // Nonzero letter-O: opposite-winding inner ring becomes a Subtract group.
      "<path id=\"Nonzero\" fill=\"red\" d=\"M110 10H190V90H110Z M130 30V70H170V30Z\"/>"
      "</svg>");
  const auto& even_odd = document.layers()[0].vector_shape()->path;
  CHECK(even_odd.subpaths.size() == 2);
  CHECK(even_odd.subpaths[0].shape_group == even_odd.subpaths[1].shape_group);
  const auto& nonzero = document.layers()[1].vector_shape()->path;
  CHECK(nonzero.subpaths.size() == 2);
  CHECK(nonzero.subpaths[0].shape_group != nonzero.subpaths[1].shape_group);
  CHECK(nonzero.subpaths[0].op == PathCombineOp::Add);
  CHECK(nonzero.subpaths[1].op == PathCombineOp::Subtract);
  // The rendered coverage really has a hole: sample the baked pixels.
  const auto& layer = document.layers()[1];
  const auto bounds = std::as_const(layer).bounds();
  const auto& pixels = std::as_const(layer).pixels();
  const auto alpha_at = [&](std::int32_t x, std::int32_t y) {
    return pixels.pixel(x - bounds.x, y - bounds.y)[3];
  };
  CHECK(alpha_at(120, 50) == 255);  // ring
  CHECK(alpha_at(150, 50) == 0);    // hole
}

// --- clip paths and masks ----------------------------------------------------

void svg_import_clip_and_mask() {
  const auto document = read_svg(
      "<svg width=\"100\" height=\"100\">"
      "<defs>"
      "<clipPath id=\"Clip\"><circle cx=\"50\" cy=\"50\" r=\"30\"/></clipPath>"
      "<mask id=\"Fade\"><rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"#808080\"/></mask>"
      "</defs>"
      "<rect id=\"Clipped\" width=\"100\" height=\"100\" fill=\"red\" clip-path=\"url(#Clip)\"/>"
      "<rect id=\"Masked\" width=\"100\" height=\"100\" fill=\"blue\" mask=\"url(#Fade)\"/>"
      "</svg>");
  const auto& clipped = document.layers()[0];
  CHECK(clipped.vector_mask() != nullptr);
  CHECK(!clipped.vector_mask()->path.subpaths.empty());
  CHECK(!clipped.vector_mask()->cache.empty());  // baked coverage cache
  const auto& masked = document.layers()[1];
  CHECK(masked.mask().has_value());
  // A uniform 50%-gray luminance mask.
  const auto& mask = *masked.mask();
  CHECK(mask.pixels.pixel(50, 50)[0] == 128);
}

// --- units and sizing --------------------------------------------------------

void svg_import_units_and_ppi() {
  // Physical units: CSS 96 px/in canvas size, 96 PPI print metadata.
  const auto physical = read_svg("<svg width=\"2in\" height=\"1in\" viewBox=\"0 0 96 48\"><rect id=\"R\" width=\"96\" height=\"48\" fill=\"red\"/></svg>");
  CHECK(physical.width() == 192 && physical.height() == 96);
  CHECK(std::abs(physical.print_settings().horizontal_ppi - 96.0) < 1e-9);
  // The viewBox scales content into the viewport: the full-viewBox rect fills the canvas.
  const auto& scaled = physical.layers()[0].vector_shape()->origination.front();
  CHECK(std::abs(scaled.right - 192.0) < 1e-6);
  // viewBox-only sizing: user units at the untagged-import 72 PPI.
  const auto plain = read_svg("<svg viewBox=\"0 0 40 30\"/>");
  CHECK(plain.width() == 40 && plain.height() == 30);
  CHECK(std::abs(plain.print_settings().horizontal_ppi - 72.0) < 1e-9);
  // No size at all: the CSS replaced-element default, with a notice.
  std::vector<std::string> notices;
  const auto sized = read_svg("<svg><rect id=\"R\" width=\"10\" height=\"10\" fill=\"red\"/></svg>", &notices);
  CHECK(sized.width() == 300 && sized.height() == 150);
  CHECK(!notices.empty());
}

// --- text and images ---------------------------------------------------------

void svg_import_text_layer_metadata() {
  const auto document = read_svg(
      "<svg width=\"200\" height=\"100\">"
      "<text id=\"T\" x=\"100\" y=\"50\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"20\" "
      "font-weight=\"bold\" fill=\"#336699\">Hello <tspan>World</tspan></text>"
      "</svg>");
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers()[0];
  // Text layers are LayerKind::Pixel + patchy.text metadata (the shape/
  // smart-object pattern); layer_is_text is the predicate.
  CHECK(layer.kind() == LayerKind::Pixel);
  CHECK(patchy::layer_is_text(layer));
  const auto& metadata = layer.metadata();
  CHECK(metadata.at(patchy::kLayerMetadataText) == "Hello World");
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "Arial");
  CHECK(metadata.at(patchy::kLayerMetadataTextSize) == "20");
  CHECK(metadata.at(patchy::kLayerMetadataTextColor) == "#336699");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "true");
  CHECK(metadata.at(patchy::kLayerMetadataSvgPendingText) == "1");
  CHECK(metadata.at(patchy::kLayerMetadataSvgTextAnchor) == "middle");
  CHECK(metadata.at(patchy::kLayerMetadataSvgTextBaselineX) == "100");
  CHECK(metadata.at(patchy::kLayerMetadataSvgTextBaselineY) == "50");
}

void svg_import_image_and_unsupported_notices() {
  std::vector<std::string> notices;
  const auto document = read_svg(
      "<svg width=\"50\" height=\"50\">"
      "<image id=\"I\" x=\"5\" y=\"6\" width=\"20\" height=\"10\" href=\"data:image/png;base64,AAAA\"/>"
      "<image id=\"External\" x=\"0\" y=\"0\" width=\"5\" height=\"5\" href=\"photo.png\"/>"
      "<filter id=\"F\"/>"
      "<foreignObject width=\"5\" height=\"5\"/>"
      "</svg>",
      &notices);
  CHECK(document.layers().size() == 1);  // the data-URI image only
  const auto& layer = document.layers()[0];
  CHECK(layer.metadata().contains(patchy::kLayerMetadataSvgPendingImage));
  CHECK(layer.bounds().x == 5 && layer.bounds().y == 6 && layer.bounds().width == 20);
  const auto joined = [&] {
    std::string all;
    for (const auto& notice : notices) {
      all += notice;
      all += '\n';
    }
    return all;
  }();
  CHECK(joined.find("external SVG image") != std::string::npos ||
        joined.find("External") != std::string::npos || joined.find("embedded data URIs") != std::string::npos);
}

// --- svgz and limits ---------------------------------------------------------

void svg_import_svgz_round_trips() {
  const std::string_view source = "<svg width=\"12\" height=\"8\"><rect id=\"R\" width=\"6\" height=\"4\" fill=\"lime\"/></svg>";
  // Build a gzip member by hand: header + raw deflate + crc32 + size.
  std::size_t deflated_size = 0;
  void* deflated = tdefl_compress_mem_to_heap(source.data(), source.size(), &deflated_size, 0);
  CHECK(deflated != nullptr);
  std::vector<std::uint8_t> gz{0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0};
  gz.insert(gz.end(), static_cast<std::uint8_t*>(deflated), static_cast<std::uint8_t*>(deflated) + deflated_size);
  mz_free(deflated);
  const auto crc = static_cast<std::uint32_t>(
      mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(source.data()), source.size()));
  const auto size32 = static_cast<std::uint32_t>(source.size());
  for (int shift = 0; shift < 32; shift += 8) {
    gz.push_back(static_cast<std::uint8_t>((crc >> shift) & 0xFFU));
  }
  for (int shift = 0; shift < 32; shift += 8) {
    gz.push_back(static_cast<std::uint8_t>((size32 >> shift) & 0xFFU));
  }
  const auto document = patchy::svg::DocumentIo::read(gz);
  CHECK(document.width() == 12 && document.height() == 8);
  CHECK(document.layers().size() == 1);
  CHECK(patchy::svg::sniff(gz));
}

void svg_import_too_many_elements_throws() {
  std::string huge = "<svg width=\"10\" height=\"10\">";
  for (int i = 0; i < 2001; ++i) {
    huge += "<rect width=\"1\" height=\"1\" fill=\"red\"/>";
  }
  huge += "</svg>";
  bool threw = false;
  try {
    (void)read_svg(huge);
  } catch (const std::exception& error) {
    threw = true;
    CHECK(std::string(error.what()).find("drawable elements") != std::string::npos);
  }
  CHECK(threw);
}

// --- export ------------------------------------------------------------------

Document document_with_live_rect() {
  Document document(120, 80, PixelFormat::rgba8());
  patchy::LiveShapeParams params;
  params.kind = LiveShapeKind::Rectangle;
  params.left = 10;
  params.top = 20;
  params.right = 60;
  params.bottom = 50;
  params.index = 0;
  patchy::populate_live_shape_box_corners(params);
  VectorShapeContent content;
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination = {params};
  content.fill.kind = VectorFillKind::Solid;
  content.fill.color = {200, 40, 10};
  content.stroke.enabled = true;
  content.stroke.width = 4.0;
  content.stroke.cap = VectorStrokeCap::Round;
  content.stroke.content.kind = VectorFillKind::Solid;
  content.stroke.content.color = {10, 20, 30};
  Layer layer(document.allocate_layer_id(), "Hero Rect", LayerKind::Pixel);
  layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::mark_layer_vector_block_dirty(layer);
  layer.set_vector_shape(std::move(content));
  patchy::update_vector_shape_raster(layer, Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  document.add_layer(std::move(layer));
  return document;
}

void svg_export_is_deterministic_and_vector() {
  const auto document = document_with_live_rect();
  const auto first = write_svg(document);
  const auto second = write_svg(document);
  CHECK(first == second);  // two writes byte-identical
  CHECK(first.find("<rect x=\"10\" y=\"20\" width=\"50\" height=\"30\"") != std::string::npos);
  CHECK(first.find("id=\"Hero_Rect\"") != std::string::npos);
  CHECK(first.find("fill=\"#c8280a\"") != std::string::npos);
  CHECK(first.find("stroke-linecap=\"round\"") != std::string::npos);
  CHECK(first.find("<image") == std::string::npos);  // nothing rasterized
}

void svg_export_reimport_round_trips_model() {
  const auto document = document_with_live_rect();
  const auto text = write_svg(document);
  const auto reimported = read_svg(text);
  CHECK(reimported.width() == 120 && reimported.height() == 80);
  CHECK(reimported.layers().size() == 1);
  const auto& layer = reimported.layers()[0];
  CHECK(layer.name() == "Hero_Rect");  // id-sanitized name round trip
  const auto* shape = layer.vector_shape();
  CHECK(shape != nullptr);
  CHECK(!shape->origination.empty());
  CHECK(shape->origination.front().kind == LiveShapeKind::Rectangle);
  CHECK(std::abs(shape->origination.front().left - 10.0) < 1e-6);
  CHECK(shape->fill.kind == VectorFillKind::Solid);
  CHECK(shape->fill.color.red == 200 && shape->fill.color.green == 40);
  CHECK(shape->stroke.enabled);
  CHECK(std::abs(shape->stroke.width - 4.0) < 1e-6);
  CHECK(shape->stroke.cap == VectorStrokeCap::Round);
}

void svg_export_stroke_alignment_hints_round_trip() {
  auto document = document_with_live_rect();
  {
    auto content = *document.layers()[0].vector_shape();
    content.stroke.alignment = VectorStrokeAlignment::Inside;
    document.layers()[0].set_vector_shape(std::move(content));
  }
  const auto text = write_svg(document);
  CHECK(text.find("data-patchy-stroke-align=\"inside\"") != std::string::npos);
  CHECK(text.find("stroke-width=\"8\"") != std::string::npos);  // doubled for the clip trick
  CHECK(text.find("clip-path=") != std::string::npos);
  const auto reimported = read_svg(text);
  const auto* shape = reimported.layers()[0].vector_shape();
  CHECK(shape->stroke.alignment == VectorStrokeAlignment::Inside);
  CHECK(std::abs(shape->stroke.width - 4.0) < 1e-6);  // true width restored
  CHECK(reimported.layers()[0].vector_mask() == nullptr);  // the trick clip is not a mask
}

void svg_export_rasterizes_open_path_with_aligned_stroke() {
  // Issue 553: an Inside or Outside stroke on an open path moves sideways in
  // the renderer; no SVG clip draws that, so the layer exports as pixels.
  Document document(120, 80, PixelFormat::rgba8());
  VectorShapeContent content;
  patchy::PathSubpath line;
  line.closed = false;
  line.anchors = {patchy::PathAnchor{10, 20, 10, 20, 10, 20, false},
                  patchy::PathAnchor{100, 60, 100, 60, 100, 60, false}};
  content.path.subpaths = {line};
  content.stroke.enabled = true;
  content.stroke.width = 8.0;
  content.stroke.alignment = VectorStrokeAlignment::Inside;
  content.stroke.content.kind = VectorFillKind::Solid;
  content.stroke.content.color = {255, 0, 0};
  Layer layer(document.allocate_layer_id(), "Line", LayerKind::Pixel);
  layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::mark_layer_vector_block_dirty(layer);
  layer.set_vector_shape(std::move(content));
  patchy::update_vector_shape_raster(layer, Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  CHECK(!layer.bounds().empty());  // the line has pixels to export
  document.add_layer(std::move(layer));
  CHECK(write_svg(document).find("<image") != std::string::npos);
  {
    auto centered = *document.layers()[0].vector_shape();
    centered.stroke.alignment = VectorStrokeAlignment::Center;
    document.layers()[0].set_vector_shape(std::move(centered));
  }
  CHECK(write_svg(document).find("<image") == std::string::npos);  // a centered line stays vector
}

void svg_export_rasterizes_unsupported_and_reports() {
  Document document(40, 40, PixelFormat::rgba8());
  PixelBuffer pixels(40, 40, PixelFormat::rgba8());
  pixels.clear(255);
  Layer base(document.allocate_layer_id(), "Base", std::move(pixels));
  base.set_bounds(Rect{0, 0, 40, 40});
  document.add_layer(std::move(base));
  PixelBuffer top_pixels(10, 10, PixelFormat::rgba8());
  top_pixels.clear(200);
  Layer top(document.allocate_layer_id(), "Weird Blend", std::move(top_pixels));
  top.set_bounds(Rect{5, 5, 10, 10});
  top.set_blend_mode(BlendMode::Subtract);  // no CSS equivalent: barrier
  document.add_layer(std::move(top));
  std::vector<std::string> notices;
  const auto text = write_svg(document, &notices);
  CHECK(text.find("<image") != std::string::npos);
  CHECK(text.find("data:image/png;base64,") != std::string::npos);
  bool mentioned = false;
  for (const auto& notice : notices) {
    mentioned = mentioned || notice.find("Merged") != std::string::npos ||
                notice.find("blend") != std::string::npos;
  }
  CHECK(mentioned);
}

// Imports the committed fixture and writes its re-export next to the binary:
// an end-to-end reader+writer integration check whose artifact doubles as the
// external-tool acceptance file (browsers, Photoshop).
void svg_fixture_reexport_writes_artifact() {
  const auto fixture = patchy::test::committed_format_fixture_path("svg", "basic-shapes.svg");
  std::ifstream input(fixture, std::ios::binary);
  CHECK(input.good());
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  std::vector<std::string> notices;
  const auto document = patchy::svg::DocumentIo::read(bytes, &notices);
  CHECK(document.layers().size() == 6);
  std::filesystem::create_directories("test-artifacts");
  patchy::svg::DocumentIo::write_file(document, "test-artifacts/svg-roundtrip.svg");
  const auto reimported = patchy::svg::DocumentIo::read(patchy::svg::DocumentIo::write(document));
  CHECK(reimported.layers().size() == document.layers().size());
}

void svg_export_masks_and_hidden_layers() {
  auto document = document_with_live_rect();
  {
    auto& layer = document.layers()[0];
    patchy::LayerVectorMask mask;
    patchy::PathSubpath subpath;
    subpath.closed = true;
    subpath.anchors = {patchy::PathAnchor{0, 0, 0, 0, 0, 0, false}, patchy::PathAnchor{100, 0, 100, 0, 100, 0, false},
                       patchy::PathAnchor{100, 70, 100, 70, 100, 70, false}};
    mask.path.subpaths.push_back(subpath);
    layer.set_vector_mask(std::move(mask));
    patchy::update_vector_mask_raster(layer, Rect::from_size(document.width(), document.height()));
    layer.set_visible(false);
  }
  const auto text = write_svg(document);
  CHECK(text.find("<clipPath") != std::string::npos);
  CHECK(text.find("clip-path=\"url(#") != std::string::npos);
  CHECK(text.find("display:none") != std::string::npos);
}

// The dry run reports exactly what write() bakes: nothing for a shape-only
// document, then each text layer, raster mask, and barrier (with the layers
// merged under it) by kind and name, with no <image> until it says so.
void svg_baked_content_dry_run_matches_writer() {
  using patchy::svg::BakedContentKind;
  const auto kinds = [](const patchy::Document& document) {
    return patchy::svg::DocumentIo::baked_content(document);
  };
  auto document = document_with_live_rect();
  CHECK(kinds(document).empty());
  CHECK(write_svg(document).find("<image") == std::string::npos);

  // A raster mask leaves the vector world as a luminance <mask> image.
  {
    auto masked = document;
    patchy::LayerMask mask;
    mask.bounds = Rect{0, 0, masked.width(), masked.height()};
    mask.pixels = PixelBuffer(masked.width(), masked.height(), PixelFormat::gray8());
    mask.pixels.clear(255);
    masked.layers()[0].set_mask(std::move(mask));
    const auto baked = kinds(masked);
    CHECK(baked.size() == 1U);
    CHECK(baked[0].kind == BakedContentKind::RasterMask && baked[0].layer_name == "Hero Rect");
    CHECK(write_svg(masked).find("<mask ") != std::string::npos);
  }

  // A text layer (a pixel layer carrying the text marker) bakes on its own.
  {
    PixelBuffer pixels(20, 10, PixelFormat::rgba8());
    pixels.clear(255);
    Layer text(document.allocate_layer_id(), "Title", std::move(pixels));
    text.set_bounds(Rect{4, 4, 20, 10});
    text.metadata()[patchy::kLayerMetadataText] = "Title";
    document.add_layer(std::move(text));
  }
  {
    const auto baked = kinds(document);
    CHECK(baked.size() == 1U);
    CHECK(baked[0].kind == BakedContentKind::TextLayer && baked[0].layer_name == "Title");
    std::vector<std::string> notices;
    CHECK(write_svg(document, &notices).find("<image") != std::string::npos);
    CHECK(std::any_of(notices.begin(), notices.end(),
                      [](const std::string& notice) { return notice.find("Text layer 'Title'") != std::string::npos; }));
  }

  // A blend mode CSS cannot express is a barrier: it and everything below merge.
  {
    PixelBuffer pixels(10, 10, PixelFormat::rgba8());
    pixels.clear(200);
    Layer top(document.allocate_layer_id(), "Weird Blend", std::move(pixels));
    top.set_bounds(Rect{5, 5, 10, 10});
    top.set_blend_mode(BlendMode::Subtract);
    document.add_layer(std::move(top));
    const auto baked = kinds(document);
    CHECK(baked.size() == 3U);
    CHECK(baked[0].kind == BakedContentKind::MergedBelow && baked[0].layer_name == "Hero Rect");
    CHECK(baked[1].kind == BakedContentKind::TextLayer && baked[1].layer_name == "Title");
    CHECK(baked[2].kind == BakedContentKind::BlendMode && baked[2].layer_name == "Weird Blend");
  }
}

}  // namespace

std::vector<patchy::test::TestCase> svg_tests() {
  return {
      {"svg_xml_parses_entities_dtd_cdata_and_namespaces", svg_xml_parses_entities_dtd_cdata_and_namespaces},
      {"svg_xml_reports_malformed_input", svg_xml_reports_malformed_input},
      {"svg_xml_transcodes_utf16", svg_xml_transcodes_utf16},
      {"svg_path_grammar_parses_all_commands", svg_path_grammar_parses_all_commands},
      {"svg_import_builds_layer_stack_in_document_order", svg_import_builds_layer_stack_in_document_order},
      {"svg_import_styles_cascade_and_colors", svg_import_styles_cascade_and_colors},
      {"svg_import_opacity_and_blend", svg_import_opacity_and_blend},
      {"svg_import_splits_strokes_more_opaque_than_their_fill", svg_import_splits_strokes_more_opaque_than_their_fill},
      {"svg_import_live_shapes_and_transform_gating", svg_import_live_shapes_and_transform_gating},
      {"svg_import_gradients", svg_import_gradients},
      {"svg_import_user_space_gradients_follow_the_element_transform",
       svg_import_user_space_gradients_follow_the_element_transform},
      {"svg_import_bounding_box_gradients_resolve_in_the_element_box",
       svg_import_bounding_box_gradients_resolve_in_the_element_box},
      {"svg_export_reimport_keeps_user_space_gradients", svg_export_reimport_keeps_user_space_gradients},
      {"svg_import_user_space_patterns_follow_the_element_transform",
       svg_import_user_space_patterns_follow_the_element_transform},
      {"svg_import_bounding_box_patterns_resolve_in_the_element_box",
       svg_import_bounding_box_patterns_resolve_in_the_element_box},
      {"svg_import_rotated_and_mirrored_patterns", svg_import_rotated_and_mirrored_patterns},
      {"svg_export_reimport_keeps_pattern_fills", svg_export_reimport_keeps_pattern_fills},
      {"svg_import_fill_rules", svg_import_fill_rules},
      {"svg_import_clip_and_mask", svg_import_clip_and_mask},
      {"svg_import_units_and_ppi", svg_import_units_and_ppi},
      {"svg_import_text_layer_metadata", svg_import_text_layer_metadata},
      {"svg_import_image_and_unsupported_notices", svg_import_image_and_unsupported_notices},
      {"svg_import_svgz_round_trips", svg_import_svgz_round_trips},
      {"svg_import_too_many_elements_throws", svg_import_too_many_elements_throws},
      {"svg_export_is_deterministic_and_vector", svg_export_is_deterministic_and_vector},
      {"svg_export_reimport_round_trips_model", svg_export_reimport_round_trips_model},
      {"svg_export_stroke_alignment_hints_round_trip", svg_export_stroke_alignment_hints_round_trip},
      {"svg_export_rasterizes_open_path_with_aligned_stroke", svg_export_rasterizes_open_path_with_aligned_stroke},
      {"svg_export_rasterizes_unsupported_and_reports", svg_export_rasterizes_unsupported_and_reports},
      {"svg_fixture_reexport_writes_artifact", svg_fixture_reexport_writes_artifact},
      {"svg_export_masks_and_hidden_layers", svg_export_masks_and_hidden_layers},
      {"svg_baked_content_dry_run_matches_writer", svg_baked_content_dry_run_matches_writer},
  };
}
