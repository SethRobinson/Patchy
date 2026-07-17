#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "formats/miniz/miniz.h"
#include "formats/svg_document_io.hpp"
#include "formats/svg_xml.hpp"

#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"

#include <algorithm>
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
      {"svg_import_live_shapes_and_transform_gating", svg_import_live_shapes_and_transform_gating},
      {"svg_import_gradients", svg_import_gradients},
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
      {"svg_export_rasterizes_unsupported_and_reports", svg_export_rasterizes_unsupported_and_reports},
      {"svg_fixture_reexport_writes_artifact", svg_fixture_reexport_writes_artifact},
      {"svg_export_masks_and_hidden_layers", svg_export_masks_and_hidden_layers},
  };
}
