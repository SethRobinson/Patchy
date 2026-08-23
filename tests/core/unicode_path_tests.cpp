// Unicode and special-character file paths through every Qt-free reader and writer
// that takes a std::filesystem::path. These pin the file-name half of the August
// 2026 Save As bug (a UTF-8 std::string handed to a path parameter was decoded with
// the ANSI code page on Windows, so "sesu.psd" typed in Japanese landed on disk as
// mojibake). Names come from tests/unicode_path_names.hpp; every check lists the
// directory afterwards so a mojibake sibling fails the test even when the reader
// happens to find its own mangled name again.

#include "core/document.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/af_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/svg_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using patchy::test::directory_holds_only;
using patchy::test::kUnicodeCombinedStem;
using patchy::test::kUnicodePathStems;
using patchy::test::read_binary_file;
using patchy::test::unicode_artifact_dir;
using patchy::test::unicode_path_piece;
using patchy::test::utf8_string;

namespace {

// A small opaque RGB document with a recognizable pixel at (1, 0).
patchy::Document rgb_document(std::int32_t width = 4, std::int32_t height = 3) {
  patchy::Document document(width, height, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(40 + x * 50);
      px[1] = static_cast<std::uint8_t>(30 + y * 60);
      px[2] = static_cast<std::uint8_t>(200 - x * 20);
    }
  }
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

patchy::Document rgba_document(std::int32_t width = 4, std::int32_t height = 3) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(40 + x * 50);
      px[1] = static_cast<std::uint8_t>(30 + y * 60);
      px[2] = static_cast<std::uint8_t>(200 - x * 20);
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Art", std::move(pixels));
  return document;
}

void check_pixel_1_0(const patchy::Document& document) {
  CHECK(!document.layers().empty());
  const auto* px = document.layers().front().pixels().pixel(1, 0);
  CHECK(px[0] == 90);
  CHECK(px[1] == 30);
  CHECK(px[2] == 180);
}

std::filesystem::path named(const std::filesystem::path& dir, std::u8string_view stem, const char* extension) {
  auto path = dir / unicode_path_piece(stem);
  path += extension;
  return path;
}

void copy_fixture_into(const std::filesystem::path& source, const std::filesystem::path& target) {
  CHECK(std::filesystem::exists(source));
  std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
}

}  // namespace

void unicode_path_psd_round_trips_each_character_class() {
  const auto dir = unicode_artifact_dir(u8"psd-classes");
  std::vector<std::filesystem::path> written;
  const auto document = rgb_document();
  for (const auto stem : kUnicodePathStems) {
    const auto path = named(dir, stem, ".psd");
    patchy::psd::DocumentIo::write_flat_rgb8_file(document, path);
    written.push_back(path);
    CHECK(std::filesystem::exists(path));
    CHECK(directory_holds_only(dir, written));
    const auto reread = patchy::psd::DocumentIo::read_file(path);
    CHECK(reread.width() == 4);
    CHECK(reread.height() == 3);
    check_pixel_1_0(reread);
  }
}

void unicode_path_layered_psd_psb_round_trip() {
  const auto dir = unicode_artifact_dir(u8"psd-layered");
  patchy::Document document = rgba_document(6, 5);
  document.add_pixel_layer(utf8_string(u8"\u30EC\u30A4\u30E4\u30FC 2"),
                           patchy::PixelBuffer(6, 5, patchy::PixelFormat::rgba8()));

  const auto psd_path = named(dir, kUnicodeCombinedStem, ".psd");
  const auto psb_path = named(dir, kUnicodeCombinedStem, ".psb");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, psd_path);
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, psb_path, patchy::psd::WriteOptions{true});
  CHECK(directory_holds_only(dir, {psd_path, psb_path}));

  for (const auto& path : {psd_path, psb_path}) {
    const auto reread = patchy::psd::DocumentIo::read_file(path);
    CHECK(reread.width() == 6);
    CHECK(reread.height() == 5);
    CHECK(reread.layers().size() == 2);
    CHECK(reread.layers().front().name() == "Art");
    CHECK(reread.layers().back().name() == utf8_string(u8"\u30EC\u30A4\u30E4\u30FC 2"));
    check_pixel_1_0(reread);
  }
}

void unicode_path_flat_format_file_io_round_trips() {
  const auto dir = unicode_artifact_dir(u8"flat-formats");
  std::vector<std::filesystem::path> written;
  const auto rgb = rgb_document();
  const auto rgba = rgba_document();

  // BMP, including a palette file under a Unicode name (the second path parameter).
  const auto palette_path = named(dir, u8"\u30D1\u30EC\u30C3\u30C8 caf\u00E9", ".pal");
  {
    std::ofstream file(palette_path, std::ios::binary);
    CHECK(file.good());
    file << "JASC-PAL\n0100\n4\n0 0 0\n255 0 0\n0 255 0\n0 0 255\n";
  }
  written.push_back(palette_path);
  const auto bmp_path = named(dir, kUnicodeCombinedStem, ".bmp");
  patchy::bmp::DocumentIo::write_file(rgb, bmp_path,
                                      patchy::bmp::WriteOptions{patchy::bmp::BmpEncoding::Indexed4,
                                                                patchy::bmp::BmpPaletteMode::PaletteFile, false,
                                                                palette_path});
  written.push_back(bmp_path);
  {
    const auto reread = patchy::bmp::DocumentIo::read_file(bmp_path);
    CHECK(reread.width() == 4);
    CHECK(reread.indexed_palette().has_value());
    CHECK(reread.indexed_palette()->colors.size() == 4);
    CHECK(reread.indexed_palette()->colors[1].red == 255);
  }

  const auto tga_path = named(dir, kUnicodeCombinedStem, ".tga");
  patchy::tga::DocumentIo::write_file(rgba, tga_path);
  written.push_back(tga_path);
  check_pixel_1_0(patchy::tga::DocumentIo::read_file(tga_path));

  const auto pcx_path = named(dir, kUnicodeCombinedStem, ".pcx");
  patchy::pcx::DocumentIo::write_file(rgb, pcx_path);
  written.push_back(pcx_path);
  check_pixel_1_0(patchy::pcx::DocumentIo::read_file(pcx_path));

  const auto lbm_path = named(dir, kUnicodeCombinedStem, ".lbm");
  patchy::ilbm::DocumentIo::write_file(rgba, lbm_path);
  written.push_back(lbm_path);
  check_pixel_1_0(patchy::ilbm::DocumentIo::read_file(lbm_path));

  const auto ico_path = named(dir, kUnicodeCombinedStem, ".ico");
  patchy::ico::DocumentIo::write_file(rgba, ico_path, patchy::ico::WriteOptions{{16}, true, false, 0, 0});
  written.push_back(ico_path);
  CHECK(patchy::ico::DocumentIo::read_file(ico_path).width() == 16);

  const auto cur_path = named(dir, kUnicodeCombinedStem, ".cur");
  patchy::ico::DocumentIo::write_file(rgba, cur_path, patchy::ico::WriteOptions{{16}, true, true, 3, 2});
  written.push_back(cur_path);
  CHECK(patchy::ico::DocumentIo::read_file(cur_path).width() == 16);

  const auto ase_path = named(dir, kUnicodeCombinedStem, ".aseprite");
  patchy::aseprite::DocumentIo::write_file(rgba, ase_path);
  written.push_back(ase_path);
  check_pixel_1_0(patchy::aseprite::DocumentIo::read_file(ase_path));

  const auto gif_path = named(dir, kUnicodeCombinedStem, ".gif");
  patchy::gif::write_file(rgb, gif_path);
  written.push_back(gif_path);
  {
    const auto bytes = read_binary_file(gif_path);
    CHECK(bytes.size() > 6);
    CHECK(std::string(bytes.begin(), bytes.begin() + 6) == "GIF89a");
  }

  const auto svg_path = named(dir, kUnicodeCombinedStem, ".svg");
  patchy::svg::DocumentIo::write_file(rgb, svg_path);
  written.push_back(svg_path);
  {
    const auto bytes = read_binary_file(svg_path);
    CHECK(!bytes.empty());
    const auto reread = patchy::svg::DocumentIo::read(bytes);
    CHECK(reread.width() == 4);
    CHECK(reread.height() == 3);
  }

  const auto acv_path = named(dir, kUnicodeCombinedStem, ".acv");
  patchy::CurvesAdjustment curves;
  curves.rgb = {{0, 0}, {64, 48}, {255, 255}};
  patchy::acv::write_file(acv_path, curves);
  written.push_back(acv_path);
  CHECK(patchy::acv::read_file(acv_path) == curves);

  const auto gpl_path = named(dir, kUnicodeCombinedStem, ".gpl");
  const std::array<patchy::RgbColor, 3> colors = {{{1, 2, 3}, {4, 5, 6}, {250, 251, 252}}};
  patchy::palette_io::write_palette_file(gpl_path, colors, patchy::palette_io::PaletteFileFormat::Gpl,
                                         utf8_string(u8"\u30D1\u30EC\u30C3\u30C8"));
  written.push_back(gpl_path);
  {
    const auto reread = patchy::palette_io::read_palette_file(gpl_path);
    CHECK(reread.colors.size() == 3);
    CHECK(reread.colors[2].blue == 252);
  }

  CHECK(directory_holds_only(dir, written));
}

void unicode_path_read_only_formats_and_plugin_probe() {
  const auto dir = unicode_artifact_dir(u8"read-only");
  std::vector<std::filesystem::path> written;

  const auto af_path = named(dir, kUnicodeCombinedStem, ".af");
  copy_fixture_into(patchy::test::committed_format_fixture_path("af", "tiny-rgba8.af"), af_path);
  written.push_back(af_path);
  const auto af_document = patchy::af::DocumentIo::read_file(af_path);
  CHECK(af_document.width() > 0);
  CHECK(!af_document.layers().empty());

  const auto plugin_path = named(dir, kUnicodeCombinedStem, ".8bf");
  copy_fixture_into(patchy::test::source_root_path() / "test-fixtures" / "photoshop-plugins" / "Greyscale64.8bf",
                    plugin_path);
  written.push_back(plugin_path);
  patchy::LegacyPhotoshopAdapter adapter;
  const auto probe = adapter.probe(plugin_path);
  CHECK(probe.kind == patchy::LegacyPhotoshopPluginKind::Filter8bf);
  // The probe stats and reads the file through its path; a mangled name would report
  // "not found" instead of the PE kind and architecture. `supported` also depends on
  // the host platform and architecture, so only kind and architecture are pinned.
  CHECK(probe.architecture == "x64");

  CHECK(directory_holds_only(dir, written));
}

void unicode_path_stem_names_first_layer_utf8() {
  // Flat readers name their single layer after the file stem; that name is UTF-8
  // everywhere else in Patchy, so it must not go through path::string() (ANSI on
  // MSVC, which turns every Japanese character into '?').
  const auto dir = unicode_artifact_dir(u8"stem-names");
  const auto expected_name = utf8_string(kUnicodeCombinedStem);
  const auto rgb = rgb_document();
  const auto rgba = rgba_document();

  const auto tga_path = named(dir, kUnicodeCombinedStem, ".tga");
  patchy::tga::DocumentIo::write_file(rgba, tga_path);
  CHECK(patchy::tga::DocumentIo::read_file(tga_path).layers().front().name() == expected_name);

  const auto pcx_path = named(dir, kUnicodeCombinedStem, ".pcx");
  patchy::pcx::DocumentIo::write_file(rgb, pcx_path);
  CHECK(patchy::pcx::DocumentIo::read_file(pcx_path).layers().front().name() == expected_name);

  const auto bmp_path = named(dir, kUnicodeCombinedStem, ".bmp");
  patchy::bmp::DocumentIo::write_file(rgb, bmp_path);
  CHECK(patchy::bmp::DocumentIo::read_file(bmp_path).layers().front().name() == expected_name);

  const auto lbm_path = named(dir, kUnicodeCombinedStem, ".lbm");
  patchy::ilbm::DocumentIo::write_file(rgba, lbm_path);
  CHECK(patchy::ilbm::DocumentIo::read_file(lbm_path).layers().front().name() == expected_name);

  CHECK(directory_holds_only(dir, {tga_path, pcx_path, bmp_path, lbm_path}));
}

std::vector<patchy::test::TestCase> unicode_path_tests() {
  return {
      {"unicode_path_psd_round_trips_each_character_class", unicode_path_psd_round_trips_each_character_class},
      {"unicode_path_layered_psd_psb_round_trip", unicode_path_layered_psd_psb_round_trip},
      {"unicode_path_flat_format_file_io_round_trips", unicode_path_flat_format_file_io_round_trips},
      {"unicode_path_read_only_formats_and_plugin_probe", unicode_path_read_only_formats_and_plugin_probe},
      {"unicode_path_stem_names_first_layer_utf8", unicode_path_stem_names_first_layer_utf8},
  };
}
