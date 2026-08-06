// Affinity .af importer: container walk, document-tree layer import, and the
// embedded-preview fallback. The committed fixtures were authored by the
// Patchy team through scripted Affinity 3.2.3 (a 64x48 gradient/pattern
// document; tiny-rgba16.af is the same document converted to 16-bit before
// saving; tiny-embedded-jpeg.af is a self-authored 400x300 JPEG opened and
// saved, which stores the untouched JPEG plus mips instead of base tiles), so
// their provenance is ours - see NOTICE-THIRD-PARTY.md. The tiny-v2-*.afphoto
// fixtures were authored interactively in Affinity Photo 2.6.5 (the 2.x
// generation of the same container). tiny-v2-stale-dfsz.afphoto and
// tiny-lazy-placed.af are deterministic byte-level derivations of those
// (local-test-fixtures/af-spike/author_derived_fixtures.py). Adversarial
// cases are byte mutations of the fixtures.

#include "formats/af_document_io.hpp"

#include "core/adjustment_layer.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "local_psd_fixtures.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "test_groups.hpp"

namespace {

[[nodiscard]] std::filesystem::path af_fixture_path(const char* name) {
  return std::filesystem::path(PATCHY_SOURCE_DIR) / "test-fixtures" / "af" / name;
}

[[nodiscard]] std::vector<std::uint8_t> read_fixture(const char* name) {
  std::ifstream stream(af_fixture_path(name), std::ios::binary);
  CHECK(stream.good());
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
  CHECK(!bytes.empty());
  return bytes;
}

void af_sniff_detects_magic() {
  const auto bytes = read_fixture("tiny-rgba8.af");
  CHECK(patchy::af::sniff(bytes));
  CHECK(patchy::af::DocumentIo::can_read(bytes));

  const std::vector<std::uint8_t> bmp_ish = {'B', 'M', 0x00, 0x01, 0x02, 0x03};
  CHECK(!patchy::af::sniff(bmp_ish));
  const std::vector<std::uint8_t> short_buffer = {0x00, 0xFF};
  CHECK(!patchy::af::sniff(short_buffer));
}

void af_tier1_imports_layer_at_full_resolution() {
  // tiny-rgba8.af is a 64x48 document with one RGBA8 image layer painted
  // r=255*x/W, g=255*y/H, b=(x^y)&255 with a 4px semi-transparent border. The
  // spread is not transparent (SprT false), so a white "Background" fill layer
  // imports below the content, matching Affinity's own composite.
  const auto bytes = read_fixture("tiny-rgba8.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);

  // Tier 1: the document opens at its true canvas size with a real pixel layer,
  // NOT the small embedded preview.
  CHECK(document.width() == 64);
  CHECK(document.height() == 48);
  CHECK(document.layers().size() == 2);
  const auto& background = document.layers().front();
  CHECK(background.name() == "Background");
  const std::uint8_t* backdrop = background.pixels().pixel(2, 2);
  CHECK(backdrop[0] == 255);
  CHECK(backdrop[3] == 255);
  const auto& layer = document.layers().back();
  CHECK(layer.name() != "Affinity preview");
  CHECK(layer.pixels().width() == 64);
  CHECK(layer.pixels().height() == 48);

  // Interior pixel matches the analytic pattern exactly (opaque, full res).
  const std::uint8_t* center = layer.pixels().pixel(32, 24);
  CHECK(static_cast<int>(center[0]) == 255 * 32 / 64);
  CHECK(static_cast<int>(center[1]) == 255 * 24 / 48);
  CHECK(static_cast<int>(center[2]) == ((32 ^ 24) & 255));
  CHECK(center[3] == 255);
}

void af_tier1_imports_16bit_document() {
  // tiny-rgba16.af is the same content converted to 16-bit; tier 1 down-converts
  // (value/257) to 8-bit RGBA and must land within rounding of the 8-bit fixture.
  const auto bytes = read_fixture("tiny-rgba16.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 64);
  CHECK(document.height() == 48);
  CHECK(document.layers().size() == 2);  // white Background + the content layer
  const std::uint8_t* center = document.layers().back().pixels().pixel(32, 24);
  const auto close_to = [](int a, int b) { return a >= b - 1 && a <= b + 1; };
  CHECK(close_to(center[0], 255 * 32 / 64));
  CHECK(close_to(center[1], 255 * 24 / 48));
  CHECK(center[3] == 255);
}

void af_tier2_imports_group_hierarchy() {
  // tiny-group.af nests three RGBA rasters (inner-a/inner-b/sibling) inside a
  // container; tier 2 imports it as a Group layer with pixel-layer children.
  const auto bytes = read_fixture("tiny-group.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 48);
  CHECK(document.height() == 32);

  const patchy::Layer* group = nullptr;
  for (const auto& layer : document.layers()) {
    if (layer.kind() == patchy::LayerKind::Group) {
      group = &layer;
      break;
    }
  }
  CHECK(group != nullptr);
  CHECK(group->children().size() == 3);
  // Affinity always opens its groups closed and the tree stores no disclosure
  // state, so an imported group starts collapsed rather than on Patchy's
  // expanded default.
  CHECK(!patchy::layer_group_expanded(*group));
  // Children keep their names and real pixels.
  bool found_inner = false;
  for (const auto& child : group->children()) {
    CHECK(child.kind() == patchy::LayerKind::Pixel);
    if (child.name() == "inner-a") {
      found_inner = true;
      CHECK(child.pixels().width() == 20);
      const std::uint8_t* p = child.pixels().pixel(10, 10);
      CHECK(static_cast<int>(p[0]) > 150);  // painted (220,40,40)
      CHECK(static_cast<int>(p[2]) < 100);
    }
  }
  CHECK(found_inner);
}

void af_tier2_imports_embedded_jpeg_original() {
  // tiny-embedded-jpeg.af was authored by opening a 400x300 self-authored JPEG
  // (r=255*x/W, g=255*y/H, b=64) in Affinity and saving. That save path stores
  // NO base-level tiles: the base Sta codes are all 5 ("pixels come from the
  // placed original"), the untouched JPEG rides in a c/<n> stream named by the
  // DyBm's Bckg field, and only the mip pyramid is materialized. The importer
  // must decode the embedded JPEG, not produce a black/empty layer.
  const auto bytes = read_fixture("tiny-embedded-jpeg.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 400);
  CHECK(document.height() == 300);
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
  CHECK(layer.name() != "Affinity preview");
  CHECK(layer.pixels().width() == 400);
  CHECK(layer.pixels().height() == 300);

  // Pixels match the authored pattern within JPEG-lossy tolerance.
  const auto close_to = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
  for (const auto& [x, y] : {std::pair<int, int>{200, 150}, {40, 40}, {360, 260}}) {
    const std::uint8_t* p = layer.pixels().pixel(x, y);
    CHECK(close_to(p[0], 255 * x / 400));
    CHECK(close_to(p[1], 255 * y / 300));
    CHECK(close_to(p[2], 64));
    CHECK(p[3] == 255);
  }

  // The full-resolution original decoded, so the half-resolution mip fallback
  // notice must not appear.
  for (const auto& notice : notices) {
    CHECK(notice.find("half resolution") == std::string::npos);
  }

  // A pristine placed image (no hand-painted base tile) also becomes an
  // embedded Patchy smart object whose source is the untouched JPEG.
  CHECK(patchy::layer_is_smart_object(layer));
  const auto source_uuid = patchy::smart_object_source_uuid(layer);
  const auto* source = document.metadata().smart_objects.find(source_uuid);
  CHECK(source != nullptr);
  CHECK(source->kind == patchy::SmartObjectSourceKind::Embedded);
  CHECK(source->filetype == "JPEG");
  CHECK(source->file_bytes != nullptr && source->file_bytes->size() > 2);
  CHECK((*source->file_bytes)[0] == 0xFF && (*source->file_bytes)[1] == 0xD8);
  const auto placement = patchy::smart_object_placement_from_layer(layer);
  CHECK(placement.has_value());
  CHECK(placement->width == 400.0);
  CHECK(placement->height == 300.0);

  // The wrapper survives a PSD round trip (the embedded source rides along).
  const auto psd_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(psd_bytes);
  const auto& reread_layer = reread.layers().back();
  CHECK(patchy::layer_is_smart_object(reread_layer));
  const auto* reread_source =
      reread.metadata().smart_objects.find(patchy::smart_object_source_uuid(reread_layer));
  CHECK(reread_source != nullptr);
  CHECK(reread_source->file_bytes != nullptr &&
        *reread_source->file_bytes == *source->file_bytes);
}

void af_embedded_original_survives_hostile_bytes() {
  // Coarse truncation + mutation sweeps over the embedded-original fixture:
  // they walk the Bckg/c-stream parse and the stb_image JPEG decode against
  // damaged input. Strides are coarse because the fixture is ~560 KB.
  const auto original = read_fixture("tiny-embedded-jpeg.af");
  for (std::size_t cut = 0; cut < original.size(); cut += original.size() / 64 + 1) {
    const std::span<const std::uint8_t> prefix(original.data(), cut);
    try {
      (void)patchy::af::DocumentIo::read(prefix);
    } catch (const std::runtime_error&) {
    }
  }
  for (std::size_t at = 4; at < original.size(); at += 4099) {
    auto mutated = original;
    mutated[at] ^= 0x5A;
    try {
      std::vector<std::string> notices;
      (void)patchy::af::DocumentIo::read(mutated, &notices);
    } catch (const std::runtime_error&) {
    }
  }
}

void af_tier2_imports_cmyk_with_notice() {
  // tiny-cmyk.af is the tiny gradient converted to CMYK/8. Tier 2 decodes it
  // (approximate, no ICC in the file) and says so in a notice.
  const auto bytes = read_fixture("tiny-cmyk.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 64);
  CHECK(document.height() == 48);
  CHECK(document.layers().size() == 2);  // white Background + the content layer
  CHECK(document.layers().back().pixels().width() == 64);

  bool has_approx_notice = false;
  for (const auto& notice : notices) {
    if (notice.find("CMYK") != std::string::npos && notice.find("approximate") != std::string::npos) {
      has_approx_notice = true;
    }
  }
  CHECK(has_approx_notice);

  // The gradient's red channel rises left-to-right; a coarse monotonic check
  // proves real color (not a flat fill or an inverted decode).
  const auto& pixels = document.layers().back().pixels();
  const int left = pixels.pixel(6, 24)[0];
  const int right = pixels.pixel(58, 24)[0];
  CHECK(right > left);
}

void af_tier2_imports_transformed_layer() {
  // tiny-transform.af places an 80x60 raster (r=255*x/W, g=255*y/H, b=32 with
  // a blue 12x12 top-left marker) through rotate(0.35rad) * scale(1.25) *
  // translate(60,40) in a 220x160 document. The importer rasterizes through
  // the affine; the convention was pinned against Affinity's own PNG export.
  const auto bytes = read_fixture("tiny-transform.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 220);
  CHECK(document.height() == 160);
  CHECK(document.layers().size() == 2);  // white Background + the placed image
  for (const auto& notice : notices) {
    CHECK(notice.find("placeholder") == std::string::npos);
  }

  const auto& layer = document.layers().back();
  // Axis-aligned bounds of the transformed corners: x [34, 154), y [40, 145).
  CHECK(layer.bounds().x == 34);
  CHECK(layer.bounds().y == 40);
  CHECK(layer.bounds().width == 120);
  CHECK(layer.bounds().height == 105);

  // The source center (40, 30) lands at document (94.1, 92.4) = layer-local
  // (60, 52) and keeps its color; bilinear + JPEG-free source, so tight bounds.
  const auto close_to = [](int a, int b) { return a >= b - 4 && a <= b + 4; };
  const std::uint8_t* center = layer.pixels().pixel(60, 52);
  CHECK(close_to(center[0], 127));
  CHECK(close_to(center[1], 127));
  CHECK(close_to(center[2], 32));
  CHECK(center[3] == 255);

  // The bounds corner outside the rotated quad stays transparent.
  CHECK(layer.pixels().pixel(2, 2)[3] == 0);

  // The document was authored at 72 PPI (Patchy's default is 300, so this
  // proves the UVCn/UPPI read).
  CHECK(document.print_settings().horizontal_ppi == 72.0);
}

void af_reads_document_resolution() {
  // tiny-dpi300.af is a 40x30 document authored at 300 DPI.
  const auto bytes = read_fixture("tiny-dpi300.af");
  const auto document = patchy::af::DocumentIo::read(bytes);
  CHECK(document.width() == 40);
  CHECK(document.height() == 30);
  CHECK(document.print_settings().horizontal_ppi == 300.0);
  CHECK(document.print_settings().vertical_ppi == 300.0);
}

void af_tier2_imports_lab_document() {
  // tiny-lab.af is a 256x96 LABA16 document: eight 32px saturated patches
  // (red, green, blue, yellow, magenta, cyan, orange, purple) over rows 0..63
  // and an RGB ramp below, authored in RGBA8 and converted with
  // doc.format = LABA16. The wire is the ICC v4 Lab16 PCS encoding; the
  // importer converts through lcms2's built-in Lab profile, so the patches
  // must come back close to their source colors (Lab round-trip tolerance).
  const auto bytes = read_fixture("tiny-lab.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 256);
  CHECK(document.height() == 96);
  CHECK(document.layers().size() == 2);  // white Background + the content layer
  const auto& pixels = document.layers().back().pixels();
  CHECK(pixels.width() == 256);

  // Expected values are Affinity's OWN sRGB render of the Lab document (the
  // Lab round trip legitimately moves some channels, e.g. pure blue picks up
  // red ~23); Patchy matches it within +-1, the tolerance covers both.
  const auto close_to = [](int a, int b) { return a >= b - 6 && a <= b + 6; };
  const struct {
    int x;
    int red;
    int green;
    int blue;
  } patches[] = {
      {16, 252, 7, 4},    {48, 10, 255, 14},  {80, 23, 6, 253},   {112, 253, 254, 11},
      {144, 252, 10, 253}, {176, 33, 255, 253}, {208, 252, 128, 9}, {240, 128, 64, 200},
  };
  for (const auto& patch : patches) {
    const std::uint8_t* p = pixels.pixel(patch.x, 32);
    CHECK(close_to(p[0], patch.red));
    CHECK(close_to(p[1], patch.green));
    CHECK(close_to(p[2], patch.blue));
    CHECK(p[3] == 255);
  }

  // Lab documents must no longer degrade to placeholders.
  for (const auto& notice : notices) {
    CHECK(notice.find("placeholder") == std::string::npos);
  }
}

void af_imports_text_layers_as_pending_text() {
  // tiny-text-artistic.af: red 36px Arial artistic text "Color" anchored at
  // baseline (20, 60). The importer stores the story as standard patchy.text.*
  // metadata plus the .af pending-render markers (MainWindow renders it
  // post-open); no placeholder notice.
  {
    const auto bytes = read_fixture("tiny-text-artistic.af");
    std::vector<std::string> notices;
    const auto document = patchy::af::DocumentIo::read(bytes, &notices);
    CHECK(document.layers().size() == 2);  // white Background + the text layer
    const auto& layer = document.layers().back();
    const auto& metadata = layer.metadata();
    const auto value = [&](const char* key) {
      const auto found = metadata.find(key);
      return found == metadata.end() ? std::string() : found->second;
    };
    CHECK(value("patchy.text") == "Color");
    CHECK(value("patchy.text.font") == "Arial");
    CHECK(value("patchy.text.size") == "36");
    CHECK(value("patchy.text.color") == "#ff0000");
    CHECK(value("patchy.text.bold") == "false");
    CHECK(metadata.contains("patchy.af.pending_text_render"));
    CHECK(metadata.contains("patchy.af.text_frame"));
    CHECK(metadata.contains("patchy.af.text_ascent"));
    for (const auto& notice : notices) {
      CHECK(notice.find("placeholder") == std::string::npos);
    }
  }

  // tiny-text-frame.af: centre-aligned 24px frame text "One" / "Two two" in a
  // frame at (10, 10, 220x120); paragraphs join with newlines.
  {
    const auto bytes = read_fixture("tiny-text-frame.af");
    std::vector<std::string> notices;
    const auto document = patchy::af::DocumentIo::read(bytes, &notices);
    const auto& layer = document.layers().back();
    const auto& metadata = layer.metadata();
    const auto value = [&](const char* key) {
      const auto found = metadata.find(key);
      return found == metadata.end() ? std::string() : found->second;
    };
    CHECK(value("patchy.text") == "One\nTwo two");
    CHECK(value("patchy.text.size") == "24");
    CHECK(value("patchy.af.text_align") == "1");
    CHECK(value("patchy.af.text_frame").substr(0, 2) == "10");
    CHECK(!metadata.contains("patchy.af.text_ascent"));  // frame text
  }
}

void af_imports_mixed_text_style_runs() {
  // tiny-text-runs.af: frame text "AB" (Arial 24 red) + "cd" (Times New Roman
  // 32 blue) + "É😀X" (Courier New 18 green). Wire GlAR Indx boundaries count
  // CODEPOINTS (the emoji is one codepoint but two UTF-16 units), so the
  // serialized patchy.text.runs offsets - UTF-16 units - end at 8, not 7.
  const auto bytes = read_fixture("tiny-text-runs.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto& layer = document.layers().back();
  const auto& metadata = layer.metadata();
  const auto value = [&](const char* key) {
    const auto found = metadata.find(key);
    return found == metadata.end() ? std::string() : found->second;
  };
  CHECK(value("patchy.text") == "ABcd\xC3\x89\xF0\x9F\x98\x80X");
  // The fixture's first run is the Arial family's NARROW face (wire Famy
  // "Arial" + Widh 3); the PostScript name resolves the display family.
  CHECK(value("patchy.text.font") == "Arial Narrow");
  CHECK(value("patchy.text.size") == "24");
  CHECK(value("patchy.text.color") == "#dc0000");
  CHECK(value("patchy.text.runs") ==
        "v1\n"
        "0\t2\t24\t0\t0\t#dc0000\tArial%20Narrow\n"
        "2\t2\t32\t0\t0\t#0000dc\tTimes%20New%20Roman\n"
        "4\t4\t18\t0\t0\t#00a000\tCourier%20New");
  const auto html = value("patchy.text.html");
  CHECK(html.find("font-size:32px") != std::string::npos);
  CHECK(html.find("#0000dc") != std::string::npos);
  CHECK(html.find("Times New Roman") != std::string::npos);
  CHECK(metadata.contains("patchy.af.pending_text_render"));
  // No "simplified" downgrade notice anymore: the runs import fully.
  for (const auto& notice : notices) {
    CHECK(notice.find("simplified") == std::string::npos);
  }
}

void af_imports_all_caps_text() {
  // tiny-text-caps.af: artistic "Mixed Caseé" with All Caps (wire: the private
  // 'CAP\x01' OpenType feature setting in OtAt.Setn) and "Small Caps" with
  // SmallCaps (smcp). All Caps uppercases the imported text (ASCII+Latin-1);
  // the small-caps family renders as typed with a notice.
  const auto bytes = read_fixture("tiny-text-caps.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  bool found_upper = false;
  bool found_small = false;
  for (const auto& layer : document.layers()) {
    const auto found = layer.metadata().find("patchy.text");
    if (found == layer.metadata().end()) {
      continue;
    }
    found_upper = found_upper || found->second == "MIXED CASE\xC3\x89";
    found_small = found_small || found->second == "Small Caps";
  }
  CHECK(found_upper);
  CHECK(found_small);
  bool caps_notice = false;
  for (const auto& notice : notices) {
    caps_notice = caps_notice || notice.find("caps text style is not supported") != std::string::npos;
  }
  CHECK(caps_notice);
}

void af_imports_layer_effects() {
  // tiny-fx.af: five 24x16 rasters, each with one effect (authored via the
  // JSLib layer-effects API; wire semantics pinned by the fx-* corpus docs).
  const auto bytes = read_fixture("tiny-fx.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto find = [&](const char* name) -> const patchy::Layer& {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        return layer;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };

  {
    // Outer shadow: offset 10 at wire angle pi/2 (shadow falls DOWN), radius
    // 2, blue. Patchy stores the Photoshop light angle: 180 - 90 = 90.
    const auto& style = find("shadowed").layer_style();
    CHECK(style.drop_shadows.size() == 1);
    const auto& shadow = style.drop_shadows.front();
    CHECK(shadow.enabled);
    CHECK(shadow.blend_mode == patchy::BlendMode::Multiply);
    CHECK(shadow.color == (patchy::RgbColor{0, 0, 220}));
    CHECK(std::abs(shadow.angle_degrees - 90.0F) < 0.01F);
    CHECK(std::abs(shadow.distance - 10.0F) < 0.01F);
    CHECK(std::abs(shadow.size - 2.0F) < 0.01F);
    CHECK(std::abs(shadow.opacity - 0.5F) < 0.01F);
  }
  {
    // Outline: width 5, Inside (wire Alig e2), yellow.
    const auto& style = find("outlined").layer_style();
    CHECK(style.strokes.size() == 1);
    const auto& stroke = style.strokes.front();
    CHECK(stroke.enabled);
    CHECK(stroke.position == patchy::LayerStrokePosition::Inside);
    CHECK(std::abs(stroke.size - 5.0F) < 0.01F);
    CHECK(stroke.color == (patchy::RgbColor{220, 220, 30}));
    CHECK(!stroke.uses_gradient);
  }
  {
    // Colour overlay: half-alpha green, Multiply (wire BlnM id 2 / v0).
    const auto& style = find("overlaid").layer_style();
    CHECK(style.color_overlays.size() == 1);
    const auto& overlay = style.color_overlays.front();
    CHECK(overlay.blend_mode == patchy::BlendMode::Multiply);
    CHECK(overlay.color == (patchy::RgbColor{30, 200, 30}));
    CHECK(std::abs(overlay.opacity - 128.0F / 255.0F) < 0.01F);
  }
  {
    // Outer glow: radius 7, magenta, Screen default.
    const auto& style = find("glowing").layer_style();
    CHECK(style.outer_glows.size() == 1);
    const auto& glow = style.outer_glows.front();
    CHECK(glow.blend_mode == patchy::BlendMode::Screen);
    CHECK(glow.color == (patchy::RgbColor{220, 30, 220}));
    CHECK(std::abs(glow.size - 7.0F) < 0.01F);
  }
  {
    // Bevel: radius 4, Outer type (wire Beve e1), default lighting 135/45,
    // wire Dept 5 px -> depth ratio 5/4.
    const auto& style = find("beveled").layer_style();
    CHECK(style.bevels.size() == 1);
    const auto& bevel = style.bevels.front();
    CHECK(bevel.style == patchy::BevelEmbossStyleKind::OuterBevel);
    CHECK(std::abs(bevel.size - 4.0F) < 0.01F);
    CHECK(std::abs(bevel.angle_degrees - 135.0F) < 0.01F);
    CHECK(std::abs(bevel.altitude_degrees - 45.0F) < 0.01F);
    CHECK(std::abs(bevel.depth - 1.25F) < 0.01F);
    CHECK(bevel.highlight_blend_mode == patchy::BlendMode::Screen);
    CHECK(bevel.shadow_blend_mode == patchy::BlendMode::Multiply);
  }
  bool bevel_notice = false;
  for (const auto& notice : notices) {
    bevel_notice = bevel_notice || notice.find("bevel/emboss effect approximated") != std::string::npos;
  }
  CHECK(bevel_notice);
}

void af_imports_paragraph_spacing() {
  // tiny-text-para-spacing.af: frame text "One"/"Two"/"Three" with paragraph
  // space-before 9 / space-after 17 (wire PAtt Doub[5]/[6]). One paragraph
  // run covers the whole story; it starts at 0, so its space-before is
  // dropped (Affinity does not push the first paragraph down) and the
  // serialized v2 paragraph run carries only the space-after.
  const auto bytes = read_fixture("tiny-text-para-spacing.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto& layer = document.layers().back();
  const auto found = layer.metadata().find("patchy.text.paragraph_runs");
  CHECK(found != layer.metadata().end());
  CHECK(found->second == "v2\n0\t13\tleft\t0\t0\t0\t0\t17");
}

void af_imports_paragraph_indents() {
  // tiny-text-indent.af: two frame-text layers from the text-indent probe
  // recipe. Affinity's left indent (wire PAtt Doub[2]) positions
  // CONTINUATION lines only and the first-line indent (Doub[4]) is absolute
  // from the column edge, so "hanging" (left indent 40 alone) serializes as
  // the PS/Qt hanging pair (first-line -40 relative to start 40) and
  // "firstline" (first-line indent 25) keeps a plain +25. Both carry the
  // story's default space-after 12.
  const auto bytes = read_fixture("tiny-text-indent.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto runs_for = [&](const char* name) -> std::string {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        const auto found = layer.metadata().find("patchy.text.paragraph_runs");
        CHECK(found != layer.metadata().end());
        return found->second;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };
  CHECK(runs_for("hanging") == "v2\n0\t52\tleft\t-40\t40\t0\t0\t12");
  CHECK(runs_for("firstline") == "v2\n0\t52\tleft\t25\t0\t0\t0\t12");
}

void af_imports_gradient_overlay_placement() {
  // tiny-fx-gradient.af: three rasters with gradient overlays - the default
  // (linear, base direction left->right = PS angle 0), one under a rotate(45)
  // FDeX descriptor transform (direction down-right = PS angle 315), and one
  // elliptical (-> Radial).
  const auto bytes = read_fixture("tiny-fx-gradient.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto overlay = [&](const char* name) -> const patchy::LayerGradientFill& {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        CHECK(layer.layer_style().gradient_fills.size() == 1);
        return layer.layer_style().gradient_fills.front();
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };
  {
    const auto& fill = overlay("plain");
    CHECK(fill.gradient.type == patchy::LayerStyleGradientType::Linear);
    CHECK(std::abs(fill.gradient.angle_degrees - 0.0F) < 0.01F);
    CHECK(fill.gradient.color_stops.size() == 2);
    CHECK(fill.gradient.color_stops.front().color == (patchy::RgbColor{0, 0, 0}));
    CHECK(fill.gradient.color_stops.back().color == (patchy::RgbColor{255, 255, 255}));
  }
  {
    const auto& fill = overlay("rotated");
    CHECK(fill.gradient.type == patchy::LayerStyleGradientType::Linear);
    CHECK(std::abs(fill.gradient.angle_degrees - 315.0F) < 0.1F);
  }
  CHECK(overlay("radial").gradient.type == patchy::LayerStyleGradientType::Radial);
}

void af_imports_rotated_artistic_text_with_transform_marker() {
  // tiny-text-rotated.af: artistic "Rotated" (24px, red, the Arial family's
  // NARROW face - wire Famy "Arial" + Widh 3, resolved via the PostScript
  // name) under a -90deg node Xfrm. The importer keeps the raw box and wire
  // sizes and carries the full affine in patchy.af.text_xfrm; no
  // "approximated" notice (the post-open pass renders through the affine).
  const auto bytes = read_fixture("tiny-text-rotated.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto& layer = document.layers().back();
  const auto& metadata = layer.metadata();
  const auto value = [&](const char* key) {
    const auto found = metadata.find(key);
    return found == metadata.end() ? std::string() : found->second;
  };
  CHECK(value("patchy.text") == "Rotated");
  CHECK(value("patchy.text.font") == "Arial Narrow");
  CHECK(value("patchy.text.size") == "24");
  const auto transform = value("patchy.af.text_xfrm");
  CHECK(!transform.empty());
  CHECK(std::count(transform.begin(), transform.end(), ' ') == 5);
  for (const auto& notice : notices) {
    CHECK(notice.find("approximated") == std::string::npos);
  }
}

void af_imports_vector_curves_as_shape_layers() {
  // tiny-vector.af: four PCrv nodes - a red rect, a green ellipse with a blue
  // 3px stroke, an orange stroke-only open line, and a purple two-subpath
  // donut (even-odd hole). Each imports as a real shape layer with vector
  // content and baked pixels.
  const auto bytes = read_fixture("tiny-vector.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto shape = [&](const char* name) -> const patchy::Layer& {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        CHECK(patchy::layer_is_vector_shape(layer));
        CHECK(!std::as_const(layer).pixels().empty());
        return layer;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };
  {
    const auto& layer = shape("rect");
    const auto* content = layer.vector_shape();
    CHECK(content->fill.kind == patchy::VectorFillKind::Solid);
    CHECK(content->fill.color == (patchy::RgbColor{220, 30, 30}));
    CHECK(!content->stroke.enabled);
    CHECK(content->path.subpaths.size() == 1);
    CHECK(content->path.subpaths.front().anchors.size() == 4);
    CHECK(content->path.subpaths.front().closed);
    CHECK(layer.bounds().x == 10 && layer.bounds().y == 10);
    CHECK(layer.bounds().width == 40 && layer.bounds().height == 25);
  }
  {
    const auto& layer = shape("ellipse-stroked");
    const auto* content = layer.vector_shape();
    CHECK(content->fill.color == (patchy::RgbColor{30, 180, 30}));
    CHECK(content->stroke.enabled);
    CHECK(std::abs(content->stroke.width - 3.0) < 0.001);
    CHECK(content->stroke.content.color == (patchy::RgbColor{30, 30, 220}));
    // Ellipse anchors are smooth with real bezier handles.
    const auto& anchor = content->path.subpaths.front().anchors.front();
    CHECK(anchor.smooth);
    CHECK(std::abs(anchor.out_x - anchor.anchor_x) > 1.0);
  }
  {
    const auto& layer = shape("line");
    const auto* content = layer.vector_shape();
    CHECK(content->fill.kind == patchy::VectorFillKind::None);
    CHECK(content->stroke.enabled);
    CHECK(!content->path.subpaths.front().closed);
  }
  {
    const auto& layer = shape("donut");
    const auto* content = layer.vector_shape();
    CHECK(content->path.subpaths.size() == 2);
    // Even-odd within one shape group: the inner subpath cuts a hole.
    CHECK(content->path.subpaths[0].shape_group == content->path.subpaths[1].shape_group);
    const auto& pixels = std::as_const(layer).pixels();
    const auto local_x = 40 - layer.bounds().x;   // document (40, 82) = hole centre
    const auto local_y = 82 - layer.bounds().y;
    CHECK(pixels.pixel(local_x, local_y)[3] == 0);
  }
  for (const auto& notice : notices) {
    CHECK(notice.find("vector content") == std::string::npos);  // no placeholders
  }
}

void af_imports_parametric_shapes_as_shape_layers() {
  // tiny-shapes.af: four ShpN parametric shape nodes on a 120x80 canvas -
  // "roundrect" (unlocked per-corner: TL Round 0.5, TR Round 0.1, BR
  // Straight 0.3, BL None on a 50x30 box), "locked" (single-radius mode:
  // corner 0 Round 0.25 renders on ALL corners; Lock absent = locked),
  // "ellipse", and "star" (default 5-point ShSt; a real shape layer since
  // July 2026, pinned at rmse 0.1 against Affinity's own probe render).
  const auto bytes = read_fixture("tiny-shapes.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 120);
  CHECK(document.height() == 80);
  const auto layer_named = [&](const char* name) -> const patchy::Layer& {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        return layer;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };
  {
    const auto& layer = layer_named("roundrect");
    CHECK(patchy::layer_is_vector_shape(layer));
    const auto* content = layer.vector_shape();
    CHECK(content->fill.kind == patchy::VectorFillKind::Solid);
    CHECK(content->fill.color == (patchy::RgbColor{200, 60, 40}));
    CHECK(content->path.subpaths.size() == 1);
    // TL Round (2 anchors) + TR Round (2) + BR Straight (2) + BL None (1).
    CHECK(content->path.subpaths.front().anchors.size() == 7);
    // Box (6,6)-(56,36): the TL radius is 0.5 * min(50,30) = 15, so local
    // (8,8) is outside the arc while the centre is filled.
    const auto& pixels = std::as_const(layer).pixels();
    CHECK(pixels.pixel(8 - layer.bounds().x, 8 - layer.bounds().y)[3] == 0);
    CHECK(pixels.pixel(31 - layer.bounds().x, 21 - layer.bounds().y)[3] == 255);
    // BL is a sharp corner: local (7,34) stays filled.
    CHECK(pixels.pixel(7 - layer.bounds().x, 34 - layer.bounds().y)[3] == 255);
  }
  {
    // Lock absent = single-radius mode: corner 0's radius (0.25 * 30 = 7.5)
    // rounds every corner, so the BR corner pixel clips too.
    const auto& layer = layer_named("locked");
    CHECK(patchy::layer_is_vector_shape(layer));
    CHECK(layer.vector_shape()->path.subpaths.front().anchors.size() == 8);
    const auto& pixels = std::as_const(layer).pixels();
    CHECK(pixels.pixel(55 - layer.bounds().x, 73 - layer.bounds().y)[3] == 0);
    CHECK(pixels.pixel(31 - layer.bounds().x, 59 - layer.bounds().y)[3] == 255);
  }
  {
    const auto& layer = layer_named("ellipse");
    CHECK(patchy::layer_is_vector_shape(layer));
    const auto* content = layer.vector_shape();
    CHECK(content->fill.color == (patchy::RgbColor{30, 180, 90}));
    const auto& anchors = content->path.subpaths.front().anchors;
    CHECK(anchors.size() == 4);
    CHECK(anchors.front().smooth);
    const auto& pixels = std::as_const(layer).pixels();
    CHECK(pixels.pixel(66 - layer.bounds().x, 8 - layer.bounds().y)[3] == 0);
    CHECK(pixels.pixel(89 - layer.bounds().x, 21 - layer.bounds().y)[3] == 255);
  }
  {
    // The star (default 5 points, inner radius 0.5, box (64,44)-(114,74))
    // imports as a real shape layer: 10 corner anchors alternating outer
    // vertices on the box ellipse with half-step inner vertices.
    const auto& layer = layer_named("star");
    CHECK(patchy::layer_is_vector_shape(layer));
    const auto* content = layer.vector_shape();
    CHECK(content->fill.color == (patchy::RgbColor{240, 200, 40}));
    CHECK(content->path.subpaths.size() == 1);
    CHECK(content->path.subpaths.front().anchors.size() == 10);
    const auto& pixels = std::as_const(layer).pixels();
    // Centre filled; the box's top-left corner lies between two arms.
    CHECK(pixels.pixel(89 - layer.bounds().x, 59 - layer.bounds().y)[3] == 255);
    CHECK(pixels.pixel(66 - layer.bounds().x, 46 - layer.bounds().y)[3] == 0);
    for (const auto& notice : notices) {
      CHECK(notice.find("does not model") == std::string::npos);
    }
  }
}

void af_imports_long_tail_parametric_shapes() {
  // tiny-shapes-2.af: twelve default-parameter shape nodes (the 2026-07-29
  // long-tail kinds), one per 44x60 cell on a 220x240 canvas - rows of
  // diamond/pie/heart/arrow, doublestar/cog/crescent/cloud,
  // trapezoid/segment/squarestar/tear. Constructions are pinned against
  // Affinity's own convert-to-curves geometry (af-spike shape-curves sweep);
  // the whole fixture renders at rmse 1.7 vs Affinity's own PNG export.
  const auto bytes = read_fixture("tiny-shapes-2.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 220);
  CHECK(document.height() == 240);
  const auto layer_named = [&](const char* name) -> const patchy::Layer& {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        return layer;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };
  const auto alpha_at = [&](const patchy::Layer& layer, std::int32_t x, std::int32_t y) -> int {
    const auto& pixels = std::as_const(layer).pixels();
    const std::int32_t lx = x - layer.bounds().x;
    const std::int32_t ly = y - layer.bounds().y;
    if (lx < 0 || ly < 0 || lx >= pixels.width() || ly >= pixels.height()) {
      return 0;
    }
    return pixels.pixel(lx, ly)[3];
  };
  for (const char* name :
       {"diamond", "pie", "heart", "arrow", "doublestar", "cog", "crescent", "cloud",
        "trapezoid", "segment", "squarestar", "tear"}) {
    CHECK(patchy::layer_is_vector_shape(layer_named(name)));
  }
  CHECK(layer_named("diamond").vector_shape()->path.subpaths.front().anchors.size() == 4);
  CHECK(layer_named("heart").vector_shape()->path.subpaths.front().anchors.size() == 6);
  CHECK(layer_named("doublestar").vector_shape()->path.subpaths.front().anchors.size() == 20);
  CHECK(layer_named("cog").vector_shape()->path.subpaths.size() == 2);  // gear + hole

  // Geometry spot checks (document coordinates; cells at 8+col*54, 8+row*80).
  CHECK(alpha_at(layer_named("diamond"), 30, 38) == 255);
  CHECK(alpha_at(layer_named("diamond"), 12, 12) == 0);
  CHECK(alpha_at(layer_named("pie"), 70, 50) == 255);   // filled sector
  CHECK(alpha_at(layer_named("pie"), 95, 20) == 0);     // the missing quarter
  CHECK(alpha_at(layer_named("heart"), 125, 25) == 255);
  CHECK(alpha_at(layer_named("heart"), 138, 14) == 0);  // above the cleft
  CHECK(alpha_at(layer_named("arrow"), 190, 38) == 255);
  CHECK(alpha_at(layer_named("arrow"), 176, 12) == 0);  // above the head edge
  CHECK(alpha_at(layer_named("doublestar"), 30, 118) == 255);
  CHECK(alpha_at(layer_named("cog"), 84, 100) == 255);  // gear ring
  CHECK(alpha_at(layer_named("cog"), 84, 118) == 0);    // centre hole
  CHECK(alpha_at(layer_named("crescent"), 120, 118) == 255);
  CHECK(alpha_at(layer_named("crescent"), 150, 118) == 0);  // outside the sliver
  CHECK(alpha_at(layer_named("cloud"), 192, 118) == 255);
  CHECK(alpha_at(layer_named("trapezoid"), 30, 200) == 255);
  CHECK(alpha_at(layer_named("trapezoid"), 10, 170) == 0);
  CHECK(alpha_at(layer_named("segment"), 84, 190) == 255);
  CHECK(alpha_at(layer_named("segment"), 84, 220) == 0);  // below the chord
  CHECK(alpha_at(layer_named("squarestar"), 138, 198) == 255);
  CHECK(alpha_at(layer_named("tear"), 192, 206) == 255);
  CHECK(alpha_at(layer_named("tear"), 172, 172) == 0);

  for (const auto& notice : notices) {
    CHECK(notice.find("does not model") == std::string::npos);
    CHECK(notice.find("placeholder") == std::string::npos);
  }
}

void af_bakes_gaussian_blur_layer_effects() {
  // tiny-fx-blur.af: "blurred" (40x24 red raster, Gaussian blur layer effect
  // radius 3) and "keptalpha" (24x24 blue raster, radius 4 with Preserve
  // Alpha). PSD cannot store a content-blur effect, so the importer bakes it
  // into the pixels: plain blur grows the bounds with the spill and softens
  // the edges; Preserve Alpha keeps the original coverage. Affinity's wire
  // radius is 2 sigma while the PS-calibrated kernel radius is ~1 sigma
  // (edge-profile fit); the whole fixture scores rmse 0.46 vs Affinity's own
  // render.
  const auto bytes = read_fixture("tiny-fx-blur.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const auto layer_named = [&](const char* name) -> const patchy::Layer& {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        return layer;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };
  const auto alpha_at = [&](const patchy::Layer& layer, std::int32_t x, std::int32_t y) -> int {
    const auto& pixels = std::as_const(layer).pixels();
    const std::int32_t lx = x - layer.bounds().x;
    const std::int32_t ly = y - layer.bounds().y;
    if (lx < 0 || ly < 0 || lx >= pixels.width() || ly >= pixels.height()) {
      return 0;
    }
    return pixels.pixel(lx, ly)[3];
  };

  const auto& blurred = layer_named("blurred");
  CHECK(blurred.bounds().x < 0);  // the spill grew the bounds
  CHECK(blurred.bounds().y < 0);
  CHECK(alpha_at(blurred, 20, 12) == 255);  // solid centre
  const int edge = alpha_at(blurred, 41, 12);
  CHECK(edge > 0);
  CHECK(edge < 255);  // softened boundary just past the sharp 40px edge
  CHECK(!blurred.metadata().contains("patchy.af.pending_blur"));

  const auto& kept = layer_named("keptalpha");
  CHECK(kept.bounds().x == 0);
  CHECK(kept.bounds().y == 0);
  CHECK(kept.bounds().width == 24);
  CHECK(kept.bounds().height == 24);  // Preserve Alpha keeps the coverage
  CHECK(alpha_at(kept, 0, 0) == 255);
  CHECK(alpha_at(kept, 23, 23) == 255);

  int baked_notices = 0;
  for (const auto& notice : notices) {
    if (notice.find("Gaussian blur layer effect baked") != std::string::npos) {
      ++baked_notices;
    }
    CHECK(notice.find("'Gaus' is not supported") == std::string::npos);
  }
  CHECK(baked_notices == 2);
}

void af_imports_multi_artboard_document() {
  // tiny-artboards.af: two artboards - "board-a" (orange, box (0,0)-(80,60),
  // with a blue "overflow" child rect spanning x 60..100) and "board-b"
  // (teal, box (100,10)-(160,70), with a yellow "dot" child whose box
  // (10,10)-(50,40) lies entirely OUTSIDE the artboard). Affinity's own
  // export (160x70) pins the union canvas, white spread background between
  // the boards, the overflow rect clipped at x=80, and the dot fully
  // clipped away.
  const auto bytes = read_fixture("tiny-artboards.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 160);
  CHECK(document.height() == 70);
  const patchy::Layer* board_a = nullptr;
  const patchy::Layer* board_b = nullptr;
  for (const auto& layer : document.layers()) {
    if (layer.name() == "board-a") {
      board_a = &layer;
    }
    if (layer.name() == "board-b") {
      board_b = &layer;
    }
  }
  CHECK(board_a != nullptr && board_a->kind() == patchy::LayerKind::Group);
  CHECK(board_b != nullptr && board_b->kind() == patchy::LayerKind::Group);
  // Synthesized groups collapse with the rest of the import.
  CHECK(!patchy::layer_group_expanded(*board_a));
  CHECK(!patchy::layer_group_expanded(*board_b));
  // Each artboard group clips to its box via a rectangular mask.
  const auto rect_is = [](const patchy::Rect& rect, std::int32_t x, std::int32_t y,
                          std::int32_t w, std::int32_t h) {
    return rect.x == x && rect.y == y && rect.width == w && rect.height == h;
  };
  CHECK(board_a->mask().has_value());
  CHECK(rect_is(board_a->mask()->bounds, 0, 0, 80, 60));
  CHECK(board_a->mask()->default_color == 0);
  CHECK(board_b->mask().has_value());
  CHECK(rect_is(board_b->mask()->bounds, 100, 10, 60, 60));

  const auto flattened = patchy::flatten_document_rgba8(document);
  const auto probe = [&](std::int32_t x, std::int32_t y) {
    const std::uint8_t* p = flattened.pixel(x, y);
    return patchy::RgbColor{p[0], p[1], p[2]};
  };
  CHECK(probe(40, 30) == (patchy::RgbColor{230, 120, 20}));    // board-a fill
  CHECK(probe(70, 30) == (patchy::RgbColor{40, 60, 220}));     // overflow child
  CHECK(probe(85, 5) == (patchy::RgbColor{255, 255, 255}));    // clipped at x=80
  CHECK(probe(110, 40) == (patchy::RgbColor{20, 160, 150}));   // board-b fill
  CHECK(probe(20, 20) == (patchy::RgbColor{230, 120, 20}));    // dot clipped away
  CHECK(probe(0, 69) == (patchy::RgbColor{255, 255, 255}));    // spread gap
}

void af_head_fat_revision_wins() {
  // tiny-incremental-chain.af carries a TWO-link stream-table chain (the
  // incremental-save layout): the head revision's doc.dat has a colour
  // overlay + outline on the subject, the older link's doc.dat has neither.
  // The importer must resolve doc.dat from the HEAD link (regression: the
  // one-pass walk imported the OLDEST revision - stale text styles and
  // missing effects on incrementally-saved documents).
  const auto bytes = read_fixture("tiny-incremental-chain.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  const patchy::Layer* subject = nullptr;
  for (const auto& layer : document.layers()) {
    if (layer.name() == "subject") {
      subject = &layer;
    }
  }
  CHECK(subject != nullptr);
  const auto& style = subject->layer_style();
  CHECK(style.color_overlays.size() == 1);
  CHECK(style.color_overlays.front().color == (patchy::RgbColor{30, 200, 30}));
  CHECK(style.strokes.size() == 1);
  CHECK(std::abs(style.strokes.front().size - 3.0F) < 0.01F);
}

void af_imports_adjustment_layers() {
  // tiny-adjust-curves.af: a gradient raster under a Curves adjustment whose
  // master spline was authored with points (0,0), (0.4,0.65), (1,1). The
  // importer maps it onto a real Patchy Curves adjustment layer (Patchy's
  // full-document render of this fixture scores RMSE 0.34 vs Affinity's).
  {
    const auto bytes = read_fixture("tiny-adjust-curves.af");
    std::vector<std::string> notices;
    const auto document = patchy::af::DocumentIo::read(bytes, &notices);
    const auto& layer = document.layers().back();
    CHECK(layer.kind() == patchy::LayerKind::Adjustment);
    const auto settings = patchy::adjustment_settings_from_layer(layer);
    CHECK(settings.has_value());
    CHECK(settings->kind == patchy::AdjustmentKind::Curves);
    CHECK(settings->curves.rgb.size() == 3);
    CHECK(settings->curves.rgb[1].input == 102);   // 0.4 * 255
    CHECK(settings->curves.rgb[1].output == 166);  // 0.65 * 255
    for (const auto& notice : notices) {
      CHECK(notice.find("placeholder") == std::string::npos);
    }
  }

  // tiny-adjust-hsl.af: HSL shift authored as visually -40deg hue, +0.3
  // saturation, -0.1 luminosity (wire HueA is turns, 1:1 with the visual
  // shift).
  {
    const auto bytes = read_fixture("tiny-adjust-hsl.af");
    const auto document = patchy::af::DocumentIo::read(bytes);
    const auto& layer = document.layers().back();
    CHECK(layer.kind() == patchy::LayerKind::Adjustment);
    const auto settings = patchy::adjustment_settings_from_layer(layer);
    CHECK(settings.has_value());
    CHECK(settings->kind == patchy::AdjustmentKind::HueSaturation);
    CHECK(settings->hue_saturation.hue_shift == -40);
    CHECK(settings->hue_saturation.saturation_delta == 30);
    CHECK(settings->hue_saturation.lightness_delta == -10);
  }
}

void af_live_filter_and_unmapped_adjustment_import_honestly() {
  // tiny-live-filter.af: a red "base" rect under a Gaussian Blur live filter
  // node "blurfx" (FlRN; its Bitm is the filter's mask plane, not content)
  // and an Exposure adjustment "exposure" (ExRA, a kind Patchy does not map)
  // holding a blue child rect "clipchild". Both unmapped nodes must import as
  // the HONEST "adjustment or live filter" placeholder - never the misleading
  // "unsupported pixel format" - and the adjustment's clipped child must
  // survive as a clipped layer instead of being dropped.
  const auto bytes = read_fixture("tiny-live-filter.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 80);
  CHECK(document.height() == 60);

  const auto layer_index = [&](const char* name) -> std::size_t {
    for (std::size_t i = 0; i < document.layers().size(); ++i) {
      if (document.layers()[i].name() == name) {
        return i;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };

  const auto& base = document.layers()[layer_index("base")];
  CHECK(patchy::layer_is_vector_shape(base));
  CHECK(base.vector_shape()->fill.color == (patchy::RgbColor{200, 40, 40}));

  // Both unmapped nodes become named empty placeholders.
  const auto& blurfx = document.layers()[layer_index("blurfx")];
  CHECK(blurfx.kind() == patchy::LayerKind::Pixel);
  CHECK(!patchy::layer_is_vector_shape(blurfx));
  const auto& exposure = document.layers()[layer_index("exposure")];
  CHECK(exposure.kind() == patchy::LayerKind::Pixel);

  // The adjustment's child imports as a clipped layer ABOVE the placeholder.
  const auto& clipchild = document.layers()[layer_index("clipchild")];
  CHECK(layer_index("clipchild") > layer_index("exposure"));
  CHECK(clipchild.clipped());
  CHECK(patchy::layer_is_vector_shape(clipchild));
  CHECK(clipchild.vector_shape()->fill.color == (patchy::RgbColor{40, 60, 220}));

  bool blur_notice = false;
  bool exposure_notice = false;
  for (const auto& notice : notices) {
    CHECK(notice.find("unsupported pixel format") == std::string::npos);
    if (notice.find("'blurfx'") != std::string::npos) {
      CHECK(notice.find("adjustment or live filter") != std::string::npos);
      blur_notice = true;
    }
    if (notice.find("'exposure'") != std::string::npos) {
      CHECK(notice.find("adjustment or live filter") != std::string::npos);
      exposure_notice = true;
    }
  }
  CHECK(blur_notice);
  CHECK(exposure_notice);
}

void af_imports_vector_mask_adjuncts() {
  // tiny-vector-mask.af (100x70): "reddisc" (red ellipse, box (8,8)-(68,48))
  // masked by a single ShpN ellipse adjunct (box (18,13)-(58,43)), and
  // "bluebox" (blue rect, box (30,20)-(90,60)) masked by a container of two
  // rects "m1" (34,24)-(54,56) and "m2" (62,24)-(86,56) - the group-of-shapes
  // construct the steam-logo wild file pins. Both must import as native
  // vector masks (PSD vmsk round-trips them); Affinity's own render shows the
  // clipped ellipse and two blue bars with a gap between them.
  const auto bytes = read_fixture("tiny-vector-mask.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 100);
  CHECK(document.height() == 70);
  const auto layer_named = [&](const char* name) -> const patchy::Layer& {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        return layer;
      }
    }
    throw std::runtime_error(std::string("layer not found: ") + name);
  };
  const auto coverage_at = [](const patchy::LayerVectorMask& mask, std::int32_t x,
                              std::int32_t y) -> int {
    if (x < mask.cache_bounds.x || y < mask.cache_bounds.y ||
        x >= mask.cache_bounds.x + mask.cache_bounds.width ||
        y >= mask.cache_bounds.y + mask.cache_bounds.height) {
      return 0;  // beyond the rasterized coverage the mask hides the layer
    }
    return mask.cache.pixel(x - mask.cache_bounds.x, y - mask.cache_bounds.y)[0];
  };

  {
    const auto& layer = layer_named("reddisc");
    CHECK(patchy::layer_is_vector_shape(layer));
    const auto* mask = layer.vector_mask();
    CHECK(mask != nullptr);
    CHECK(mask->path.subpaths.size() == 1);
    CHECK(mask->path.subpaths.front().anchors.size() == 4);  // the mask ellipse
    // Mask ellipse centre (38,28), rx 20 / ry 15: the centre is covered, the
    // owner's left edge (12,28) lies outside the mask.
    CHECK(coverage_at(*mask, 38, 28) == 255);
    CHECK(coverage_at(*mask, 12, 28) == 0);
  }
  {
    const auto& layer = layer_named("bluebox");
    const auto* mask = layer.vector_mask();
    CHECK(mask != nullptr);
    CHECK(mask->path.subpaths.size() == 2);  // one rect per group child
    CHECK(mask->path.subpaths[0].shape_group != mask->path.subpaths[1].shape_group);
    CHECK(coverage_at(*mask, 44, 40) == 255);  // inside m1
    CHECK(coverage_at(*mask, 58, 40) == 0);    // the gap between the bars
    CHECK(coverage_at(*mask, 70, 40) == 255);  // inside m2
  }

  // The composite applies the masks: the gap shows the white background, the
  // bars stay blue, and the red disc survives only inside its mask ellipse.
  const auto flat = patchy::flatten_document_rgba8(document);
  const auto rgb_at = [&](std::int32_t x, std::int32_t y) {
    const std::uint8_t* p = flat.pixel(x, y);
    return std::array<int, 3>{p[0], p[1], p[2]};
  };
  CHECK(rgb_at(44, 40) == (std::array<int, 3>{40, 60, 220}));
  CHECK(rgb_at(58, 40) == (std::array<int, 3>{255, 255, 255}));
  CHECK(rgb_at(30, 28) == (std::array<int, 3>{200, 40, 40}));
  CHECK(rgb_at(12, 28) == (std::array<int, 3>{255, 255, 255}));

  for (const auto& notice : notices) {
    CHECK(notice.find("mask") == std::string::npos);
  }
}

void af_approximates_affinity_only_blend_modes() {
  // tiny-blend-affinity.af: a gradient image "base" under one rect per
  // Affinity-only blend mode - wire stamps pinned by the fixture's tree dump:
  // "avg" Average (21,v0), "neg" Negation (22,v0), "refl" Reflect (23,v0),
  // "glow" Glow (24,v0), "pig" Pigment (1,v6), "cneg" ContrastNegate (28,v2),
  // "erase" Erase (25,v0) - plus container "refgroup" (Blnd Reflect) holding
  // "gchild". Best-fit remaps chosen by RMSE against Affinity's full-gamut
  // probe renders (af-spike blend_probes): Average is EXACTLY Normal at half
  // opacity; Negation -> Exclusion; Reflect/Pigment -> Overlay; Glow ->
  // Linear Light. ContrastNegate stays Normal with the plain not-supported
  // notice (nothing existing is close enough to render as if right). Erase
  // (an alpha-removal operator) folds into an isolated Normal group over the
  // layers beneath it whose group mask is the carrier's inverse alpha - the
  // PSD-native construction.
  const auto bytes = read_fixture("tiny-blend-affinity.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  // Recursive: the Erase fold nests the lower siblings inside a wrapper group.
  const auto layer_named = [&](const char* name) -> const patchy::Layer& {
    const patchy::Layer* found = nullptr;
    std::function<void(const std::vector<patchy::Layer>&)> visit =
        [&](const std::vector<patchy::Layer>& layers) {
          for (const auto& layer : layers) {
            if (layer.name() == name) {
              found = &layer;
              return;
            }
            visit(layer.children());
            if (found != nullptr) {
              return;
            }
          }
        };
    visit(document.layers());
    if (found == nullptr) {
      throw std::runtime_error(std::string("layer not found: ") + name);
    }
    return *found;
  };

  const auto& avg = layer_named("avg");
  CHECK(avg.blend_mode() == patchy::BlendMode::Normal);
  CHECK(std::abs(avg.opacity() - 0.5F) < 0.005F);  // authored 1.0, folded x0.5
  CHECK(layer_named("neg").blend_mode() == patchy::BlendMode::Exclusion);
  CHECK(layer_named("refl").blend_mode() == patchy::BlendMode::Overlay);
  CHECK(layer_named("glow").blend_mode() == patchy::BlendMode::LinearLight);
  CHECK(layer_named("pig").blend_mode() == patchy::BlendMode::Overlay);
  CHECK(layer_named("cneg").blend_mode() == patchy::BlendMode::Normal);

  // The group must not silently keep pass-through for an explicit mode.
  const auto& group = layer_named("refgroup");
  CHECK(group.kind() == patchy::LayerKind::Group);
  CHECK(group.blend_mode() == patchy::BlendMode::Overlay);

  // Erase (ShpB [82,8]..[92,28]) folds into an isolated Normal wrapper group
  // masked by the carrier's inverse alpha; the carrier layer itself is gone.
  const auto& wrapper = layer_named("erase (Erase)");
  CHECK(wrapper.kind() == patchy::LayerKind::Group);
  CHECK(wrapper.blend_mode() == patchy::BlendMode::Normal);
  CHECK(std::abs(wrapper.opacity() - 1.0F) < 0.005F);
  CHECK(!wrapper.clipped());
  CHECK(!patchy::layer_group_expanded(wrapper));  // the fold's machinery stays folded away
  CHECK(wrapper.children().size() == 7);  // base + the six blend rects
  CHECK(wrapper.children().front().name() == "base");
  CHECK(wrapper.children().back().name() == "cneg");
  CHECK(wrapper.mask().has_value());
  CHECK(wrapper.mask()->default_color == 255);
  CHECK(!wrapper.mask()->disabled);
  CHECK(wrapper.mask()->pixels.format() == patchy::PixelFormat::gray8());
  CHECK(wrapper.mask()->bounds.width == wrapper.mask()->pixels.width());
  CHECK(wrapper.mask()->bounds.height == wrapper.mask()->pixels.height());
  CHECK(wrapper.mask()->bounds.contains(87, 18));
  CHECK(*wrapper.mask()->pixels.pixel(87 - wrapper.mask()->bounds.x,
                                      18 - wrapper.mask()->bounds.y) == 0);
  bool erase_at_top_level = false;
  for (const auto& layer : document.layers()) {
    erase_at_top_level = erase_at_top_level || layer.name() == "erase";
  }
  CHECK(!erase_at_top_level);

  // The composite: inside the erase rect the stack is cut through to the
  // white spread background; below the rect the base gradient still shows.
  const auto flat = patchy::flatten_document_rgba8(document);
  const std::uint8_t* hole = flat.pixel(87, 18);
  CHECK(hole[0] == 255 && hole[1] == 255 && hole[2] == 255);
  const std::uint8_t* below = flat.pixel(87, 40);
  CHECK(!(below[0] == 255 && below[1] == 255 && below[2] == 255));

  const auto notice_for = [&](const char* layer, const char* text) {
    const std::string needle = std::string("'") + layer + "'";
    for (const auto& notice : notices) {
      if (notice.find(needle) != std::string::npos &&
          notice.find(text) != std::string::npos) {
        return true;
      }
    }
    return false;
  };
  CHECK(notice_for("avg", "'Average' approximated as Normal at half opacity"));
  CHECK(notice_for("neg", "'Negation' approximated as Exclusion"));
  CHECK(notice_for("refl", "'Reflect' approximated as Overlay"));
  CHECK(notice_for("glow", "'Glow' approximated as Linear Light"));
  CHECK(notice_for("pig", "'Pigment' approximated as Overlay"));
  CHECK(notice_for("cneg", "not supported by Patchy; shown as Normal"));
  CHECK(notice_for("erase", "'Erase' imported as a mask on a new group"));
  CHECK(!notice_for("erase", "not supported by Patchy"));
  CHECK(notice_for("refgroup", "'Reflect' approximated as Overlay"));
}

void af_erase_blend_round_trips_through_psd() {
  // The Erase construction must survive Patchy's own PSD writer unchanged:
  // group + gray8 group mask are native PSD records.
  const auto bytes = read_fixture("tiny-blend-affinity.af");
  const auto document = patchy::af::DocumentIo::read(bytes, nullptr);
  const auto psd_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(psd_bytes);

  const patchy::Layer* wrapper = nullptr;
  std::function<void(const std::vector<patchy::Layer>&)> visit =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (layer.name() == "erase (Erase)") {
            wrapper = &layer;
            return;
          }
          visit(layer.children());
          if (wrapper != nullptr) {
            return;
          }
        }
      };
  visit(reread.layers());
  CHECK(wrapper != nullptr);
  CHECK(wrapper->kind() == patchy::LayerKind::Group);
  CHECK(wrapper->blend_mode() == patchy::BlendMode::Normal);
  CHECK(wrapper->children().size() == 7);
  CHECK(wrapper->mask().has_value());
  CHECK(wrapper->mask()->default_color == 255);
  CHECK(*wrapper->mask()->pixels.pixel(87 - wrapper->mask()->bounds.x,
                                       18 - wrapper->mask()->bounds.y) == 0);

  const auto flat = patchy::flatten_document_rgba8(reread);
  const std::uint8_t* hole = flat.pixel(87, 18);
  CHECK(hole[0] == 255 && hole[1] == 255 && hole[2] == 255);
  const std::uint8_t* below = flat.pixel(87, 40);
  CHECK(!(below[0] == 255 && below[1] == 255 && below[2] == 255));
}

void af_reads_affinity2_raster_document() {
  // tiny-v2-raster.afphoto was authored interactively in Affinity Photo 2.6.5
  // (the 2.x generation shares the .af container and doc-tree grammar; only
  // the extension differs): a 96x64 sRGB/8 document whose base pixel layer is
  // filled (200,40,40) under a second pixel layer holding a (40,60,220)
  // rectangle set to Multiply at 50% opacity. The blend mode pins the
  // version-0 wire enum: Multiply is id 2 there, which the version-6 (JS)
  // numbering misreads as Darken.
  const auto& registry = patchy::builtin_format_registry();
  const auto* handler = registry.find_by_extension(".afphoto");
  CHECK(handler != nullptr);
  CHECK(handler->identifier == "patchy.formats.af");
  CHECK(registry.find_by_extension("AFDESIGN") == handler);
  CHECK(registry.find_by_extension(".afpub") == handler);
  CHECK(registry.find_by_extension(".af") == handler);
  CHECK(!handler->can_write());  // read-only, like .af itself

  const auto bytes = read_fixture("tiny-v2-raster.afphoto");
  const auto result = handler->read(bytes);
  const auto& document = result.document;
  CHECK(document.width() == 96);
  CHECK(document.height() == 64);
  CHECK(document.layers().size() == 3);  // white Background + two pixel layers
  const auto& base = document.layers()[1];
  const std::uint8_t* red = base.pixels().pixel(8, 8);
  CHECK(red[0] == 200);
  CHECK(red[1] == 40);
  CHECK(red[2] == 40);
  CHECK(red[3] == 255);
  const auto& top = document.layers()[2];
  CHECK(top.blend_mode() == patchy::BlendMode::Multiply);
  CHECK(std::abs(top.opacity() - 0.5F) < 0.005F);
  const std::uint8_t* blue = top.pixels().pixel(50 - top.bounds().x, 32 - top.bounds().y);
  CHECK(blue[0] == 40);
  CHECK(blue[1] == 60);
  CHECK(blue[2] == 220);
  CHECK(blue[3] == 255);
  for (const auto& notice : result.notices) {
    CHECK(notice.find("placeholder") == std::string::npos);
    CHECK(notice.find("not supported") == std::string::npos);
  }
}

void af_reads_affinity2_shape_text_document() {
  // tiny-v2-shape-text.afphoto (Affinity Photo 2.6.5, interactive): a 128x80
  // document with a green (40,180,90) rectangle drawn with the shape tool and
  // converted to curves, plus the artistic text "Af2" (Arial). The 2.x wire
  // matches 3.x, so the curve imports as a real vector shape layer and the
  // text as pending-render text metadata.
  const auto bytes = read_fixture("tiny-v2-shape-text.afphoto");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 128);
  CHECK(document.height() == 80);

  const patchy::Layer* shape = nullptr;
  const patchy::Layer* text = nullptr;
  for (const auto& layer : document.layers()) {
    if (patchy::layer_is_vector_shape(layer)) {
      shape = &layer;
    }
    if (layer.metadata().contains("patchy.text")) {
      text = &layer;
    }
  }
  CHECK(shape != nullptr);
  const auto* content = shape->vector_shape();
  CHECK(content->fill.kind == patchy::VectorFillKind::Solid);
  CHECK(content->fill.color == (patchy::RgbColor{40, 180, 90}));
  CHECK(content->path.subpaths.size() == 1);
  CHECK(content->path.subpaths.front().closed);
  CHECK(shape->bounds().x == 12 && shape->bounds().y == 30);
  CHECK(shape->bounds().width == 48 && shape->bounds().height == 40);

  CHECK(text != nullptr);
  const auto& metadata = text->metadata();
  CHECK(metadata.at("patchy.text") == "Af2");
  CHECK(metadata.at("patchy.text.font") == "Arial");
  CHECK(metadata.contains("patchy.af.pending_text_render"));
}

void af_page_rect_beats_stale_dfsz() {
  // tiny-v2-stale-dfsz.afphoto derives from tiny-v2-raster.afphoto
  // (author_derived_fixtures.py): DfSz is patched to 48x32 while the spread's
  // SpMd -> PagR[0].PgIn.rctp page rect keeps 0,0,96,64 and there is no SprB.
  // Affinity tracks canvas resizes in the page rect only (a wild resized
  // document kept DfSz at its creation size 512x512 with rctp 1024x1024), so
  // the canvas must come from rctp, not the stale DfSz.
  const auto bytes = read_fixture("tiny-v2-stale-dfsz.afphoto");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 96);
  CHECK(document.height() == 64);
  CHECK(document.layers().size() == 3);  // white Background + two pixel layers
  const std::uint8_t* red = document.layers()[1].pixels().pixel(8, 8);
  CHECK(red[0] == 200);
  CHECK(red[1] == 40);
  CHECK(red[2] == 40);
}

void af_lazy_placed_image_decodes_original() {
  // tiny-lazy-placed.af derives from tiny-embedded-jpeg.af
  // (author_derived_fixtures.py): the DyBm's base-plane fields (TWi/THi/Idx/
  // Sta 1..4) are removed entirely, which is the 3.x lazy save shape for
  // untouched placed images - pixels exist ONLY as the Bckg original plus a
  // mip pyramid (the esdreika wild file carries 33 such layers). The base is
  // implicitly "every tile from the original": the importer must decode the
  // embedded JPEG, not fail to an empty placeholder. Desc is blanked and IRFN
  // added, so the layer must take the image resource's file name.
  const auto bytes = read_fixture("tiny-lazy-placed.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 400);
  CHECK(document.height() == 300);
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
  CHECK(layer.name() == "placed_image.jpg");
  CHECK(layer.pixels().width() == 400);
  CHECK(layer.pixels().height() == 300);

  // Same authored gradient as tiny-embedded-jpeg, within JPEG-lossy tolerance.
  const auto close_to = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
  for (const auto& [x, y] : {std::pair<int, int>{200, 150}, {40, 40}, {360, 260}}) {
    const std::uint8_t* p = layer.pixels().pixel(x, y);
    CHECK(close_to(p[0], 255 * x / 400));
    CHECK(close_to(p[1], 255 * y / 300));
    CHECK(close_to(p[2], 64));
    CHECK(p[3] == 255);
  }
  for (const auto& notice : notices) {
    CHECK(notice.find("placeholder") == std::string::npos);
    CHECK(notice.find("half resolution") == std::string::npos);
  }

  // Still a pristine placed image: the smart-object wrap applies.
  CHECK(patchy::layer_is_smart_object(layer));
  const auto* source =
      document.metadata().smart_objects.find(patchy::smart_object_source_uuid(layer));
  CHECK(source != nullptr);
  CHECK(source->kind == patchy::SmartObjectSourceKind::Embedded);
  CHECK(source->filetype == "JPEG");
}

void af_reads_esdreika_wild_file_if_available() {
  // Wild regression file (local-only, skipped where absent): a long-lived
  // Affinity Photo 2.x document (doc version 26, 44 incremental FAT
  // revisions) resized from 512x512 to 1024x1024. Pins the canvas coming from
  // the page rect (DfSz stays 512x512), the lazy placed-image DyBms decoding
  // from their originals, and unnamed ImgN nodes taking their IRFN file name.
  const auto path = patchy::test::local_format_fixture_path(
      "af-spike/from_esdreika", "interior_textures_v3_1024k.afphoto");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream stream(path, std::ios::binary);
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                        std::istreambuf_iterator<char>());
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 1024);
  CHECK(document.height() == 1024);

  std::size_t metal_grid_layers = 0;
  std::size_t empty_pixel_layers = 0;
  std::size_t clipped_adjustments = 0;
  const patchy::Layer* fabric = nullptr;
  const patchy::Layer* gravel_group = nullptr;
  const patchy::Layer* maintex_group = nullptr;
  const std::function<void(const std::vector<patchy::Layer>&)> walk =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (layer.kind() == patchy::LayerKind::Group) {
            if (layer.name() == "Gravel") {
              gravel_group = &layer;
            }
            if (layer.name() == "MainTex") {
              maintex_group = &layer;
            }
            walk(layer.children());
            continue;
          }
          if (layer.kind() == patchy::LayerKind::Adjustment && layer.clipped()) {
            ++clipped_adjustments;
          }
          if (layer.name() == "metal_grid.png") {
            ++metal_grid_layers;
          }
          if (layer.name() == "fabric_leather_01_diff_4k") {
            fabric = &layer;
          }
          if (layer.kind() == patchy::LayerKind::Pixel && layer.pixels().empty()) {
            ++empty_pixel_layers;
          }
        }
      };
  walk(document.layers());

  // The two unnamed metal_grid ImgN nodes take their IRFN file name.
  CHECK(metal_grid_layers == 2);
  // A lazy placed image (4096x4096 JPEG at 1/32 scale) decodes to real pixels.
  CHECK(fabric != nullptr);
  CHECK(fabric->pixels().width() > 100);
  bool fabric_has_opaque = false;
  for (std::int32_t y = 0; y < fabric->pixels().height() && !fabric_has_opaque; ++y) {
    for (std::int32_t x = 0; x < fabric->pixels().width(); ++x) {
      if (fabric->pixels().pixel(x, y)[3] > 200) {
        fabric_has_opaque = true;
        break;
      }
    }
  }
  CHECK(fabric_has_opaque);
  // Minification is box-filtered: the 4096px leather original placed at 1/32
  // scale must come out smooth like Affinity's own render, not point-sampled
  // noise (aliasing pushed the mean neighbor delta well above 8).
  {
    const auto& pixels = fabric->pixels();
    std::int64_t total = 0;
    std::int64_t samples = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x + 1 < pixels.width(); ++x) {
        const std::uint8_t* left = pixels.pixel(x, y);
        const std::uint8_t* right = pixels.pixel(x + 1, y);
        if (left[3] == 0 || right[3] == 0) {
          continue;
        }
        for (int channel = 0; channel < 3; ++channel) {
          total += std::abs(static_cast<int>(left[channel]) - right[channel]);
          ++samples;
        }
      }
    }
    CHECK(samples > 0);
    CHECK(total < samples * 6);
  }
  // Every raster in the file decodes; nothing imports as an empty placeholder.
  CHECK(empty_pixel_layers == 0);
  // Affinity scopes group-member adjustments to the group: the "Gravel" group
  // (which holds a -180 degree HSL shift) must import ISOLATED so the shift
  // cannot leak over every layer below it, while adjustment-free groups keep
  // pass-through.
  CHECK(gravel_group != nullptr);
  CHECK(gravel_group->blend_mode() == patchy::BlendMode::Normal);
  CHECK(maintex_group != nullptr);
  CHECK(maintex_group->blend_mode() == patchy::BlendMode::PassThrough);
  // Adjustments attached through a layer's AdCh adjunct list (a metal texture
  // carries Brightness/Contrast + HSL that way; two brown_leather nodes nest
  // HSL as Chld children) import as CLIPPED adjustment layers, never as the
  // owner's mask.
  CHECK(clipped_adjustments >= 4);
}

void af_reads_old_generation_wild_files_if_available() {
  // Third-party wild samples (local-test-fixtures/af-spike/web_samples, never
  // committed; this test skips where they are absent) pin the old-generation
  // wire variants from the 2026-07-28 sweep. fladder-banner2.afphoto: the
  // embedded Fladder logo only renders when the EmDc stream wrapper is
  // unwrapped AND the nested v3-era document's BFil fill descriptor is read
  // (the logo is an orange-gradient curve). qlmarkdown-icon.afdesign: the
  // enabled gradient overlay's stops are HSLA color classes (white -> black).
  {
    const auto path = patchy::test::local_format_fixture_path("af-spike/web_samples",
                                                              "fladder-banner2.afphoto");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      std::ifstream stream(path, std::ios::binary);
      const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                            std::istreambuf_iterator<char>());
      const auto document = patchy::af::DocumentIo::read(bytes);
      bool found_orange = false;
      for (const auto& layer : document.layers()) {
        const auto& pixels = layer.pixels();
        for (std::int32_t y = 0; y < pixels.height() && !found_orange; y += 3) {
          for (std::int32_t x = 0; x < pixels.width(); x += 3) {
            const std::uint8_t* p = pixels.pixel(x, y);
            if (p[3] > 200 && p[0] > 200 && p[1] > 60 && p[1] < 190 && p[2] < 110) {
              found_orange = true;
              break;
            }
          }
        }
      }
      CHECK(found_orange);
    }
  }
  {
    const auto path = patchy::test::local_format_fixture_path("af-spike/web_samples",
                                                              "qlmarkdown-icon.afdesign");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      std::ifstream stream(path, std::ios::binary);
      const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                            std::istreambuf_iterator<char>());
      const auto document = patchy::af::DocumentIo::read(bytes);
      const patchy::Layer* box = nullptr;
      for (const auto& layer : document.layers()) {
        if (layer.name() == "box") {
          box = &layer;
        }
      }
      CHECK(box != nullptr);
      CHECK(box->layer_style().gradient_fills.size() == 1);
      const auto& gradient = box->layer_style().gradient_fills.front().gradient;
      CHECK(gradient.color_stops.size() == 2);
      CHECK(gradient.color_stops.front().color == (patchy::RgbColor{255, 255, 255}));
      CHECK(gradient.color_stops.back().color == (patchy::RgbColor{0, 0, 0}));
    }
  }

  // dbacchet osd-mux (doc version 9): old-generation DyBms store NO TWi/THi
  // tile-grid fields; the grid is derived from the bitmap dimensions. Before
  // the derivation every raster wider than 256px failed the plane-size check
  // and imported as an empty placeholder (the whole document flattened to
  // white plus text). The second layer is the full-canvas schematic drawing.
  {
    const auto path = patchy::test::local_format_fixture_path(
        "af-spike/web_samples2", "dbacchet_crt_mods_restorations__osd-mux-v2.afphoto");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      std::ifstream stream(path, std::ios::binary);
      const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                            std::istreambuf_iterator<char>());
      const auto document = patchy::af::DocumentIo::read(bytes);
      CHECK(document.width() == 1007);
      CHECK(document.height() == 1005);
      CHECK(document.layers().size() >= 2);
      const auto& schematic = document.layers()[1];
      CHECK(schematic.name() == "Background");
      CHECK(!schematic.pixels().empty());
      CHECK(schematic.bounds().x == 0);
      CHECK(schematic.bounds().y == 0);
      CHECK(schematic.bounds().width == 1007);
      CHECK(schematic.bounds().height == 1005);
      // The schematic's red wire must survive into the flattened composite.
      const auto flat = patchy::flatten_document_rgba8(document);
      bool found_red = false;
      for (std::int32_t y = 0; y < flat.height() && !found_red; y += 2) {
        for (std::int32_t x = 0; x < flat.width(); x += 2) {
          const std::uint8_t* p = flat.pixel(x, y);
          if (p[3] > 200 && p[0] > 200 && p[1] < 80 && p[2] < 80) {
            found_red = true;
            break;
          }
        }
      }
      CHECK(found_red);
    }
  }

  // vh-check (doc v4): the oldest vector-paint wire. BFil/PFil carry the Fill
  // class DIRECTLY (no FDsc/FDeF wrapper), stroke width rides the node's LSty
  // LDsc, and the paint-less phone container holds its five painted panels as
  // PLAIN children (clipping them to its empty placeholder hid the mockup;
  // only text rendered - rmse 104 vs its own thumbnail, ~23 after).
  {
    const auto path = patchy::test::local_format_fixture_path(
        "af-spike/web_samples2", "Hiswe_vh-check__issue-schema.afdesign");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      std::ifstream stream(path, std::ios::binary);
      const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                            std::istreambuf_iterator<char>());
      const auto document = patchy::af::DocumentIo::read(bytes);
      CHECK(document.width() == 750);
      CHECK(document.height() == 1334);
      const auto flat = patchy::flatten_document_rgba8(document);
      const auto probe = [&](std::int32_t x, std::int32_t y) {
        const std::uint8_t* p = flat.pixel(x, y);
        return std::array<std::uint8_t, 3>{p[0], p[1], p[2]};
      };
      CHECK(probe(500, 500) == (std::array<std::uint8_t, 3>{255, 0, 120}));    // hot-pink column
      CHECK(probe(200, 500) == (std::array<std::uint8_t, 3>{255, 229, 242}));  // pale-pink column
      CHECK(probe(90, 60) == (std::array<std::uint8_t, 3>{130, 183, 209}));    // blue-gray band
      CHECK(probe(40, 500) == (std::array<std::uint8_t, 3>{68, 68, 85}));      // PFil outline
    }
  }
}

void af_modern_embeds_are_center_anchored_if_available() {
  // restaurant-menu-inside.af (local af-spike corpus; skips where absent) is
  // a version-30 document with three placed .afdesign swashes. Modern EmbN
  // transforms map the CENTER of the embedded canvas: the first swash
  // (nested canvas 1193x409 at scale ~1.006, translate (2979, 206)) must
  // land with its right edge on the host's right edge (x 3579) and its top
  // edge at y=0 - Affinity's own render pins those corners. Raw-origin
  // placement (the old bug) put it at (2979, 206), half a canvas low/right.
  if constexpr (sizeof(void*) < 8) {
    // The tier-1 import of this fixture holds a dozen 75-150 MB scan layers
    // live at once and exceeds a 32-bit address space (wasm32 throws
    // std::bad_alloc and falls back to the preview, which has no swash layer).
    std::cout << "[SKIP] af_modern_embeds_are_center_anchored_if_available: "
                 "this import needs a 64-bit address space\n";
    return;
  }
  const auto path =
      patchy::test::local_format_fixture_path("af-spike/corpus", "restaurant-menu-inside.af");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local corpus fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream stream(path, std::ios::binary);
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                        std::istreambuf_iterator<char>());
  const auto document = patchy::af::DocumentIo::read(bytes);
  const patchy::Layer* swash = nullptr;
  const std::function<void(const std::vector<patchy::Layer>&)> visit =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (swash == nullptr && layer.name() == "Layer 1" && !layer.pixels().empty()) {
            swash = &layer;
          }
          visit(layer.children());
        }
      };
  visit(document.layers());
  CHECK(swash != nullptr);
  CHECK(std::abs(swash->bounds().width - 1200) <= 3);
  CHECK(std::abs(swash->bounds().x + swash->bounds().width - 3579) <= 2);
  CHECK(std::abs(swash->bounds().y) <= 2);
}

// Affinity 2.x wild files (2026-07-28 sweep, local-only web_samples2/; each
// block skips independently when its sample is absent). These pin the three
// 2.x-generation fixes the sweep surfaced: the first spread's SprB is the
// canvas (not DfSz, which stores the New Document preset size), mask
// enclosures compose through their owner node's transform, and Designer
// symbol instances resolve geometry/paint through their SLnk/ILOb rings.
void af_reads_affinity2_wild_files_if_available() {
  const auto read_document = [](const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                          std::istreambuf_iterator<char>());
    return patchy::af::DocumentIo::read(bytes);
  };

  // Canvas from SprB: DfSz says 1920x1080 but the document (and Affinity's
  // own square thumbnail) is the 800x800 spread.
  {
    const auto path = patchy::test::local_format_fixture_path(
        "af-spike/web_samples2", "bluefeet_bluefeet.dev__favicon.afphoto");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      const auto document = read_document(path);
      CHECK(document.width() == 800);
      CHECK(document.height() == 800);
    }
  }

  // Mask anchoring: the phone mockup's screen mask lives in its owner's
  // local space (no adjunct Xfrm). Before the fix the mask anchored at the
  // spread origin, masking the white screen away and flattening the whole
  // phone to black.
  {
    const auto path = patchy::test::local_format_fixture_path(
        "af-spike/web_samples2", "paulgessinger_swift-paperless__single.afphoto");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      const auto document = read_document(path);
      const auto flat = patchy::flatten_document_rgba8(document);
      // A point inside the app screenshot area: opaque and bright (the
      // Add Document form is white), not the black-screen failure mode.
      const auto* screen = flat.pixel(300, 600);
      CHECK(screen[3] == 255);
      CHECK(screen[0] > 100 && screen[1] > 100 && screen[2] > 100);
    }
  }

  // Designer symbols: the logo is one raccoon symbol placed twice (the
  // second mirrored); both instances' PCrv members carry no local Crvs or
  // paint and resolve through the SLnk/ILOb ring.
  {
    const auto path = patchy::test::local_format_fixture_path(
        "af-spike/web_samples2", "spensbot_beat-bot__logo.afdesign");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      const auto document = read_document(path);
      const auto flat = patchy::flatten_document_rgba8(document);
      // A cheek point on each raccoon: opaque, blue-leaning fur (blue is the
      // strongest channel; before the symbol fix the right raccoon area was
      // blank and the left one only had the soft reference raster).
      for (const std::int32_t x : {245, 735}) {
        const auto* fur = flat.pixel(x, 200);
        CHECK(fur[3] == 255);
        CHECK(fur[2] > 120);
        CHECK(fur[2] > fur[0]);
      }
    }
  }

  // Vector-mask adjuncts: the Steam logo's steam silhouette is a Grup of
  // four ShpN shapes plus a PCrv riding the black circle's AdCh enclosure.
  // It must import as a native vector mask (dropping it left the circle
  // solid black - rmse 138 vs its own thumbnail; with the mask it scores
  // ~51, the rest being the gradient-overlay gap).
  {
    const auto path = patchy::test::local_format_fixture_path(
        "af-spike/web_samples2", "The-Noah_steam-screenshot-organizer__logo.afdesign");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      const auto document = read_document(path);
      const patchy::Layer* masked = nullptr;
      for (const auto& layer : document.layers()) {
        if (layer.vector_mask() != nullptr) {
          CHECK(masked == nullptr);  // exactly one vector-masked layer
          masked = &layer;
        }
      }
      CHECK(masked != nullptr);
      CHECK(masked->vector_mask()->path.subpaths.size() >= 5);
    }
  }

  // Erase blend + line-style None on a transparent spread: the sprite's top
  // ellipse (Blnd 25/v0) must punch a fully transparent hole through the
  // concentric rings (folded wrapper group, SprT=true so no Background), and
  // its base disc's black 1px stroke carries line style 0 = None, so no dark
  // ring may appear at the sprite edge. Was rmse 84 vs its own thumbnail
  // (solid disc + black outline); ~7 after.
  {
    const auto path = patchy::test::local_format_fixture_path(
        "af-spike/web_samples2", "filiph_game_benchmarks__sprite.afdesign");
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local wild fixture missing: " << path.string() << '\n';
    } else {
      const auto document = read_document(path);
      CHECK(document.width() == 64 && document.height() == 64);
      CHECK(document.layers().size() == 1);
      const auto& wrapper = document.layers().front();
      CHECK(wrapper.kind() == patchy::LayerKind::Group);
      CHECK(wrapper.blend_mode() == patchy::BlendMode::Normal);
      CHECK(wrapper.children().size() == 6);
      CHECK(wrapper.mask().has_value());
      CHECK(wrapper.mask()->default_color == 255);
      const auto flat = patchy::flatten_document_rgba8(document);
      CHECK(flat.pixel(32, 32)[3] == 0);   // the erased hole
      CHECK(flat.pixel(32, 12)[3] > 200);  // a ring above it survives
      // Line style None: the disc's black stroke must not render.
      const auto* edge = flat.pixel(32, 1);
      CHECK(edge[3] > 0);
      CHECK(edge[0] > 100 || edge[2] > 100);  // magenta-ish ring, not black
    }
  }
}

void af_read_rejects_non_affinity_bytes() {
  const std::vector<std::uint8_t> garbage = {'n', 'o', 't', ' ', 'a', 'f', 0, 1, 2, 3};
  bool threw = false;
  try {
    (void)patchy::af::DocumentIo::read(garbage);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

void af_read_survives_truncation_sweep() {
  const auto bytes = read_fixture("tiny-rgba8.af");
  // Every prefix must either import or throw std::runtime_error - never crash.
  // Fine-grained near the front (header/info blocks), coarser across the rest.
  std::vector<std::size_t> cuts;
  for (std::size_t cut = 0; cut < 96 && cut < bytes.size(); ++cut) {
    cuts.push_back(cut);
  }
  for (std::size_t cut = 96; cut < bytes.size(); cut += 61) {
    cuts.push_back(cut);
  }
  for (const auto cut : cuts) {
    const std::span<const std::uint8_t> prefix(bytes.data(), cut);
    try {
      (void)patchy::af::DocumentIo::read(prefix);
    } catch (const std::runtime_error&) {
    }
  }
}

void af_read_survives_mutation_sweep() {
  // The group fixture exercises the richest tree (nested container, three
  // rasters), so mutating it hits the most tier-1/2 code paths.
  const auto original = read_fixture("tiny-group.af");
  // Flip a byte at positions spread across the whole file (header, stream
  // table, compressed payloads, thumbnail): reads must throw or succeed with
  // notices, never crash or hang. 0x5A flips both nibbles and the sign bit.
  for (std::size_t at = 4; at < original.size(); at += 37) {
    auto mutated = original;
    mutated[at] ^= 0x5A;
    try {
      std::vector<std::string> notices;
      (void)patchy::af::DocumentIo::read(mutated, &notices);
    } catch (const std::runtime_error&) {
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> af_format_tests() {
  return {
      {"af_sniff_detects_magic", af_sniff_detects_magic},
      {"af_tier1_imports_layer_at_full_resolution", af_tier1_imports_layer_at_full_resolution},
      {"af_tier1_imports_16bit_document", af_tier1_imports_16bit_document},
      {"af_tier2_imports_group_hierarchy", af_tier2_imports_group_hierarchy},
      {"af_tier2_imports_embedded_jpeg_original", af_tier2_imports_embedded_jpeg_original},
      {"af_embedded_original_survives_hostile_bytes", af_embedded_original_survives_hostile_bytes},
      {"af_tier2_imports_transformed_layer", af_tier2_imports_transformed_layer},
      {"af_reads_document_resolution", af_reads_document_resolution},
      {"af_tier2_imports_lab_document", af_tier2_imports_lab_document},
      {"af_imports_text_layers_as_pending_text", af_imports_text_layers_as_pending_text},
      {"af_imports_mixed_text_style_runs", af_imports_mixed_text_style_runs},
      {"af_imports_all_caps_text", af_imports_all_caps_text},
      {"af_imports_layer_effects", af_imports_layer_effects},
      {"af_imports_gradient_overlay_placement", af_imports_gradient_overlay_placement},
      {"af_imports_paragraph_spacing", af_imports_paragraph_spacing},
      {"af_imports_paragraph_indents", af_imports_paragraph_indents},
      {"af_imports_rotated_artistic_text_with_transform_marker",
       af_imports_rotated_artistic_text_with_transform_marker},
      {"af_imports_vector_curves_as_shape_layers", af_imports_vector_curves_as_shape_layers},
      {"af_imports_parametric_shapes_as_shape_layers",
       af_imports_parametric_shapes_as_shape_layers},
      {"af_imports_long_tail_parametric_shapes", af_imports_long_tail_parametric_shapes},
      {"af_bakes_gaussian_blur_layer_effects", af_bakes_gaussian_blur_layer_effects},
      {"af_imports_multi_artboard_document", af_imports_multi_artboard_document},
      {"af_head_fat_revision_wins", af_head_fat_revision_wins},
      {"af_imports_adjustment_layers", af_imports_adjustment_layers},
      {"af_live_filter_and_unmapped_adjustment_import_honestly",
       af_live_filter_and_unmapped_adjustment_import_honestly},
      {"af_imports_vector_mask_adjuncts", af_imports_vector_mask_adjuncts},
      {"af_approximates_affinity_only_blend_modes", af_approximates_affinity_only_blend_modes},
      {"af_erase_blend_round_trips_through_psd", af_erase_blend_round_trips_through_psd},
      {"af_tier2_imports_cmyk_with_notice", af_tier2_imports_cmyk_with_notice},
      {"af_reads_affinity2_raster_document", af_reads_affinity2_raster_document},
      {"af_reads_affinity2_shape_text_document", af_reads_affinity2_shape_text_document},
      {"af_page_rect_beats_stale_dfsz", af_page_rect_beats_stale_dfsz},
      {"af_lazy_placed_image_decodes_original", af_lazy_placed_image_decodes_original},
      {"af_reads_esdreika_wild_file_if_available", af_reads_esdreika_wild_file_if_available},
      {"af_reads_old_generation_wild_files_if_available",
       af_reads_old_generation_wild_files_if_available},
      {"af_modern_embeds_are_center_anchored_if_available",
       af_modern_embeds_are_center_anchored_if_available},
      {"af_reads_affinity2_wild_files_if_available", af_reads_affinity2_wild_files_if_available},
      {"af_read_rejects_non_affinity_bytes", af_read_rejects_non_affinity_bytes},
      {"af_read_survives_truncation_sweep", af_read_survives_truncation_sweep},
      {"af_read_survives_mutation_sweep", af_read_survives_mutation_sweep},
  };
}
