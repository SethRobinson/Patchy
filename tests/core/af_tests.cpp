// Affinity .af importer: container walk, document-tree layer import, and the
// embedded-preview fallback. The committed fixtures were authored by the
// Patchy team through scripted Affinity 3.2.3 (a 64x48 gradient/pattern
// document; tiny-rgba16.af is the same document converted to 16-bit before
// saving; tiny-embedded-jpeg.af is a self-authored 400x300 JPEG opened and
// saved, which stores the untouched JPEG plus mips instead of base tiles), so
// their provenance is ours - see NOTICE-THIRD-PARTY.md. Adversarial cases are
// byte mutations of the fixtures.

#include "formats/af_document_io.hpp"

#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/smart_object.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
  CHECK(value("patchy.text.font") == "Arial");
  CHECK(value("patchy.text.size") == "24");
  CHECK(value("patchy.text.color") == "#dc0000");
  CHECK(value("patchy.text.runs") ==
        "v1\n"
        "0\t2\t24\t0\t0\t#dc0000\tArial\n"
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
      {"af_head_fat_revision_wins", af_head_fat_revision_wins},
      {"af_imports_adjustment_layers", af_imports_adjustment_layers},
      {"af_tier2_imports_cmyk_with_notice", af_tier2_imports_cmyk_with_notice},
      {"af_read_rejects_non_affinity_bytes", af_read_rejects_non_affinity_bytes},
      {"af_read_survives_truncation_sweep", af_read_survives_truncation_sweep},
      {"af_read_survives_mutation_sweep", af_read_survives_mutation_sweep},
  };
}
