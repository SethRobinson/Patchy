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

void af_tier0_reads_embedded_preview() {
  const auto bytes = read_fixture("tiny-rgba8.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);

  CHECK(document.layers().size() == 1);
  CHECK(document.layers().front().name() == "Affinity preview");
  CHECK(document.width() >= 16);
  CHECK(document.height() >= 12);
  CHECK(document.width() <= 512);
  CHECK(document.height() <= 512);

  // The preview is Affinity's own render of the document; the fixture paints
  // r=255*x/W, g=255*y/H, b=(x^y)&255 with an opaque interior, so a loose
  // center-pixel check proves real pixels arrived (not noise or a blank).
  const auto& pixels = document.layers().front().pixels();
  const std::int32_t cx = document.width() / 2;
  const std::int32_t cy = document.height() / 2;
  const std::uint8_t* center = pixels.pixel(cx, cy);
  const auto close_to = [](int actual, int expected) {
    return actual >= expected - 12 && actual <= expected + 12;
  };
  CHECK(close_to(center[0], 255 * cx / document.width()));
  CHECK(close_to(center[1], 255 * cy / document.height()));
  CHECK(center[3] == 255);

  CHECK(!notices.empty());
  CHECK(notices.front().find("preview") != std::string::npos);
  CHECK(notices.front().find("document format version") != std::string::npos);
}

void af_tier0_reads_16bit_document_preview() {
  const auto bytes = read_fixture("tiny-rgba16.af");
  std::vector<std::string> notices;
  const auto document = patchy::af::DocumentIo::read(bytes, &notices);
  CHECK(document.layers().size() == 1);
  CHECK(document.width() >= 16);
  CHECK(!notices.empty());
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
  const auto original = read_fixture("tiny-rgba8.af");
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
      {"af_tier0_reads_embedded_preview", af_tier0_reads_embedded_preview},
      {"af_tier0_reads_16bit_document_preview", af_tier0_reads_16bit_document_preview},
      {"af_read_rejects_non_affinity_bytes", af_read_rejects_non_affinity_bytes},
      {"af_read_survives_truncation_sweep", af_read_survives_truncation_sweep},
      {"af_read_survives_mutation_sweep", af_read_survives_mutation_sweep},
  };
}
