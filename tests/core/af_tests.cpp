// Affinity .af importer: container walk, document-tree layer import, and the
// embedded-preview fallback. The committed fixtures were authored by the
// Patchy team through scripted Affinity 3.2.3 (a 64x48 gradient/pattern
// document; tiny-rgba16.af is the same document converted to 16-bit before
// saving; tiny-embedded-jpeg.af is a self-authored 400x300 JPEG opened and
// saved, which stores the untouched JPEG plus mips instead of base tiles), so
// their provenance is ours - see NOTICE-THIRD-PARTY.md. Adversarial cases are
// byte mutations of the fixtures.

#include "formats/af_document_io.hpp"

#include "core/document.hpp"
#include "test_harness.hpp"

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
      {"af_tier2_imports_cmyk_with_notice", af_tier2_imports_cmyk_with_notice},
      {"af_read_rejects_non_affinity_bytes", af_read_rejects_non_affinity_bytes},
      {"af_read_survives_truncation_sweep", af_read_survives_truncation_sweep},
      {"af_read_survives_mutation_sweep", af_read_survives_mutation_sweep},
  };
}
