// Affinity .af tier-0 importer: container walk + embedded-preview decode. The
// committed fixtures were authored by the Patchy team through scripted Affinity
// 3.2.3 (a 64x48 gradient/pattern document; tiny-rgba16.af is the same document
// converted to 16-bit before saving), so their provenance is ours - see
// NOTICE-THIRD-PARTY.md. Adversarial cases are byte mutations of the fixture.

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
  // r=255*x/W, g=255*y/H, b=(x^y)&255 with a 4px semi-transparent border.
  const auto bytes = read_fixture("tiny-rgba8.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);

  // Tier 1: the document opens at its true canvas size with a real pixel layer,
  // NOT the small embedded preview.
  CHECK(document.width() == 64);
  CHECK(document.height() == 48);
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
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
  CHECK(document.layers().size() == 1);
  const std::uint8_t* center = document.layers().front().pixels().pixel(32, 24);
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

void af_tier2_imports_cmyk_with_notice() {
  // tiny-cmyk.af is the tiny gradient converted to CMYK/8. Tier 2 decodes it
  // (approximate, no ICC in the file) and says so in a notice.
  const auto bytes = read_fixture("tiny-cmyk.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 64);
  CHECK(document.height() == 48);
  CHECK(document.layers().size() == 1);
  CHECK(document.layers().front().pixels().width() == 64);

  bool has_approx_notice = false;
  for (const auto& notice : notices) {
    if (notice.find("CMYK") != std::string::npos && notice.find("approximate") != std::string::npos) {
      has_approx_notice = true;
    }
  }
  CHECK(has_approx_notice);

  // The gradient's red channel rises left-to-right; a coarse monotonic check
  // proves real color (not a flat fill or an inverted decode).
  const auto& pixels = document.layers().front().pixels();
  const int left = pixels.pixel(6, 24)[0];
  const int right = pixels.pixel(58, 24)[0];
  CHECK(right > left);
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
      {"af_tier2_imports_cmyk_with_notice", af_tier2_imports_cmyk_with_notice},
      {"af_read_rejects_non_affinity_bytes", af_read_rejects_non_affinity_bytes},
      {"af_read_survives_truncation_sweep", af_read_survives_truncation_sweep},
      {"af_read_survives_mutation_sweep", af_read_survives_mutation_sweep},
  };
}
