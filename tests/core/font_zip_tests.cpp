#include "formats/font_zip.hpp"

#include "formats/miniz/miniz.h"
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::formats::extract_font_files_from_zip;
using patchy::formats::ZipFontExtractLimits;

std::vector<std::uint8_t> pattern_bytes(std::size_t size, std::uint8_t seed) {
  std::vector<std::uint8_t> bytes(size);
  for (std::size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<std::uint8_t>(seed + i * 31);
  }
  return bytes;
}

std::vector<std::uint8_t> make_zip(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
  mz_zip_archive zip{};
  CHECK(mz_zip_writer_init_heap(&zip, 0, 0) == MZ_TRUE);
  for (const auto& [name, bytes] : entries) {
    CHECK(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(),
                                MZ_DEFAULT_LEVEL) == MZ_TRUE);
  }
  void* buffer = nullptr;
  std::size_t size = 0;
  CHECK(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size) == MZ_TRUE);
  std::vector<std::uint8_t> archive(static_cast<const std::uint8_t*>(buffer),
                                    static_cast<const std::uint8_t*>(buffer) + size);
  mz_free(buffer);
  mz_zip_writer_end(&zip);
  return archive;
}

void font_zip_extracts_fonts_in_archive_order() {
  const auto a = pattern_bytes(64, 1);
  const auto b = pattern_bytes(96, 2);
  const auto c = pattern_bytes(48, 3);
  const auto archive = make_zip({
      {"a.ttf", a},
      {"readme.txt", pattern_bytes(16, 9)},
      {"b.otf", b},
      {"c.ttc", c},
  });
  std::string error;
  const auto extracted = extract_font_files_from_zip(archive.data(), archive.size(), {}, &error);
  CHECK(error.empty());
  CHECK(extracted.size() == 3);
  CHECK(extracted[0].name == "a.ttf");
  CHECK(extracted[0].bytes == a);
  CHECK(extracted[1].name == "b.otf");
  CHECK(extracted[1].bytes == b);
  CHECK(extracted[2].name == "c.ttc");
  CHECK(extracted[2].bytes == c);
}

void font_zip_skips_macosx_dotfiles_directories_and_nonfonts() {
  const auto keep_upper = pattern_bytes(32, 4);
  const auto keep_nested = pattern_bytes(32, 5);
  const auto archive = make_zip({
      {"fonts/", {}},                                  // directory entry
      {"__MACOSX/shadow.ttf", pattern_bytes(32, 6)},   // resource-fork root
      {"fonts/__MACOSX/._a.ttf", pattern_bytes(8, 7)}, // nested resource fork
      {"fonts/.hidden.ttf", pattern_bytes(8, 8)},      // dotfile basename
      {"notes.txt", pattern_bytes(8, 9)},              // wrong extension
      {"empty.ttf", {}},                               // zero-byte entry
      {"UPPER.TTF", keep_upper},                       // extension match is case-insensitive
      {"fonts/sub/nested.otf", keep_nested},           // nested path flattens to basename
  });
  const auto extracted = extract_font_files_from_zip(archive.data(), archive.size());
  CHECK(extracted.size() == 2);
  CHECK(extracted[0].name == "UPPER.TTF");
  CHECK(extracted[0].bytes == keep_upper);
  CHECK(extracted[1].name == "nested.otf");
  CHECK(extracted[1].bytes == keep_nested);
}

void font_zip_deduplicates_and_sanitizes_names() {
  const auto archive = make_zip({
      {"a/f.ttf", pattern_bytes(16, 1)},
      {"b/f.ttf", pattern_bytes(16, 2)},
      {"c/F.TTF", pattern_bytes(16, 3)},   // collision check is case-insensitive
      {"d\\win.ttf", pattern_bytes(16, 4)},  // backslash path separator
      {"we\"ird?.ttf", pattern_bytes(16, 5)},
  });
  const auto extracted = extract_font_files_from_zip(archive.data(), archive.size());
  CHECK(extracted.size() == 5);
  CHECK(extracted[0].name == "f.ttf");
  CHECK(extracted[1].name == "f (2).ttf");
  CHECK(extracted[2].name == "F (3).TTF");
  CHECK(extracted[3].name == "win.ttf");
  CHECK(extracted[4].name == "we_ird_.ttf");
}

void font_zip_enforces_extraction_caps() {
  ZipFontExtractLimits limits;
  limits.max_file_bytes = 100;
  limits.max_total_bytes = 150;
  limits.max_entries = 2;
  const auto archive = make_zip({
      {"a.ttf", pattern_bytes(80, 1)},
      {"big.ttf", pattern_bytes(200, 2)},  // over the per-file cap
      {"b.ttf", pattern_bytes(80, 3)},     // would push the total past 150
      {"c.ttf", pattern_bytes(60, 4)},     // fits (80 + 60 <= 150)
      {"d.ttf", pattern_bytes(10, 5)},     // entry cap of 2 already reached
  });
  const auto extracted = extract_font_files_from_zip(archive.data(), archive.size(), limits);
  CHECK(extracted.size() == 2);
  CHECK(extracted[0].name == "a.ttf");
  CHECK(extracted[1].name == "c.ttf");
}

void font_zip_reports_malformed_archives() {
  std::string error;
  CHECK(extract_font_files_from_zip(nullptr, 0, {}, &error).empty());
  CHECK(!error.empty());

  const auto garbage = pattern_bytes(512, 7);
  CHECK(extract_font_files_from_zip(garbage.data(), garbage.size(), {}, &error).empty());
  CHECK(!error.empty());

  const auto archive = make_zip({{"a.ttf", pattern_bytes(64, 1)}});
  CHECK(extract_font_files_from_zip(archive.data(), archive.size() / 2, {}, &error).empty());
  CHECK(!error.empty());

  // The error resets on a subsequent good archive.
  CHECK(extract_font_files_from_zip(archive.data(), archive.size(), {}, &error).size() == 1);
  CHECK(error.empty());
}

}  // namespace

std::vector<patchy::test::TestCase> font_zip_tests() {
  return {
      {"font_zip_extracts_fonts_in_archive_order", font_zip_extracts_fonts_in_archive_order},
      {"font_zip_skips_macosx_dotfiles_directories_and_nonfonts",
       font_zip_skips_macosx_dotfiles_directories_and_nonfonts},
      {"font_zip_deduplicates_and_sanitizes_names", font_zip_deduplicates_and_sanitizes_names},
      {"font_zip_enforces_extraction_caps", font_zip_enforces_extraction_caps},
      {"font_zip_reports_malformed_archives", font_zip_reports_malformed_archives},
  };
}
