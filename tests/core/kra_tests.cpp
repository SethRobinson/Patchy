// Krita .kra reader (docs/kra.md): LZF, the tile format, the layer tree and its mapping,
// the Unicode path, hostile input, and the files Krita itself writes (local fixtures and a
// local corpus of openly licensed files, skipped when absent).

#include "color/color_management.hpp"
#include "color/rgb_profiles.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/document_channel.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/image_open_diagnosis.hpp"
#include "formats/kra_document_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/ora_document_io.hpp"
#include "formats/stb/stb_image.h"
#include "psd/psd_document_io.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::string_view_literals;
namespace kra = patchy::kra;
using patchy::BitDepth;
using patchy::BlendMode;
using patchy::Document;
using patchy::Layer;
using patchy::LayerKind;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;

class DeepEditingScope {
public:
  explicit DeepEditingScope(bool enabled) { patchy::set_deep_editing_override(enabled); }
  ~DeepEditingScope() { patchy::set_deep_editing_override(std::nullopt); }
  DeepEditingScope(const DeepEditingScope&) = delete;
  DeepEditingScope& operator=(const DeepEditingScope&) = delete;
};

const Layer* find_named(const std::vector<Layer>& layers, std::string_view name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* child = find_named(layer.children(), name); child != nullptr) {
      return child;
    }
  }
  return nullptr;
}

Rect rect_of(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  return Rect{x, y, width, height};
}

bool notices_mention(const std::vector<std::string>& notices, std::string_view text) {
  return std::any_of(notices.begin(), notices.end(),
                     [text](const std::string& notice) { return notice.find(text) != std::string::npos; });
}

std::vector<std::uint8_t> bytes_of(std::string_view text) {
  return {text.begin(), text.end()};
}

// An LZF encoder for the tests: literal runs, and back references at distance 1 for runs of
// three or more equal bytes (the published stream format, nothing Krita-specific).
std::vector<std::uint8_t> lzf_compress_runs(const std::vector<std::uint8_t>& data) {
  std::vector<std::uint8_t> out;
  std::vector<std::uint8_t> literal;
  const auto flush = [&] {
    for (std::size_t at = 0; at < literal.size(); at += 32) {
      const auto count = std::min<std::size_t>(32, literal.size() - at);
      out.push_back(static_cast<std::uint8_t>(count - 1));
      out.insert(out.end(), literal.begin() + static_cast<std::ptrdiff_t>(at),
                 literal.begin() + static_cast<std::ptrdiff_t>(at + count));
    }
    literal.clear();
  };
  std::size_t i = 0;
  while (i < data.size()) {
    std::size_t run = 1;
    while (i + run < data.size() && data[i + run] == data[i]) {
      ++run;
    }
    if (run < 4) {
      literal.insert(literal.end(), data.begin() + static_cast<std::ptrdiff_t>(i),
                     data.begin() + static_cast<std::ptrdiff_t>(i + run));
      i += run;
      continue;
    }
    literal.push_back(data[i]);
    flush();
    auto remaining = run - 1;
    while (remaining >= 3) {
      const auto length = std::min<std::size_t>(remaining, 264);
      const auto code = length - 2;
      if (code < 7) {
        out.push_back(static_cast<std::uint8_t>(code << 5U));
      } else {
        out.push_back(0xE0);
        out.push_back(static_cast<std::uint8_t>(code - 7));
      }
      out.push_back(0);  // distance 1
      remaining -= length;
    }
    i += run - remaining;
  }
  flush();
  return out;
}

// One tile: interleaved pixels (Krita's channel order) of a 64 x 64 tile at (x, y).
struct Tile {
  std::int32_t x{0};
  std::int32_t y{0};
  std::vector<std::uint8_t> pixels;
  bool stored{false};  // written uncompressed (flag 0) instead of LZF
};

// A tile file as Krita writes it: the text header, then per tile its record line, the
// compression flag and the planar samples (LZF, or stored for a tile marked so).
std::vector<std::uint8_t> tile_file(int pixel_bytes, const std::vector<Tile>& tiles) {
  std::string header = "VERSION 2\nTILEWIDTH 64\nTILEHEIGHT 64\nPIXELSIZE " + std::to_string(pixel_bytes) +
                       "\nDATA " + std::to_string(tiles.size()) + "\n";
  std::vector<std::uint8_t> out(header.begin(), header.end());
  for (const auto& tile : tiles) {
    std::vector<std::uint8_t> planar(tile.pixels.size());
    const std::size_t count = 64U * 64U;
    for (std::size_t p = 0; p < count; ++p) {
      for (std::size_t b = 0; b < static_cast<std::size_t>(pixel_bytes); ++b) {
        planar[b * count + p] = tile.pixels[p * static_cast<std::size_t>(pixel_bytes) + b];
      }
    }
    std::vector<std::uint8_t> payload;
    if (!tile.stored) {
      payload.push_back(1);
      const auto packed = lzf_compress_runs(planar);
      payload.insert(payload.end(), packed.begin(), packed.end());
    } else {
      payload.push_back(0);
      payload.insert(payload.end(), planar.begin(), planar.end());
    }
    const auto line = std::to_string(tile.x) + "," + std::to_string(tile.y) + ",LZF," + std::to_string(payload.size()) + "\n";
    out.insert(out.end(), line.begin(), line.end());
    out.insert(out.end(), payload.begin(), payload.end());
  }
  return out;
}

// A 64 x 64 BGRA tile whose pixel (x, y) is (b, g, r, a) from `pixel`.
template <typename Fn>
std::vector<std::uint8_t> bgra_tile(Fn pixel) {
  std::vector<std::uint8_t> out(64U * 64U * 4U);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const auto px = pixel(x, y);
      std::copy(px.begin(), px.end(), out.begin() + (y * 64 + x) * 4);
    }
  }
  return out;
}

constexpr std::string_view kSyntheticMaindoc = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE DOC PUBLIC '-//KDE//DTD krita 2.0//EN' 'http://www.calligra.org/DTD/krita-2.0.dtd'>
<DOC xmlns="http://www.calligra.org/DTD/krita" syntaxVersion="2.0" editor="Krita" kritaVersion="5.3.2">
 <IMAGE colorspacename="RGBA" x-res="150" y-res="150" name="Doc" width="100" height="80" mime="application/x-kra">
  <layers>
   <layer name="Unknown op" nodetype="paintlayer" filename="layer6" compositeop="freeze" opacity="255" visible="1" x="0" y="0" colorspacename="RGBA">
    <masks>
     <mask name="Saved" nodetype="selectionmask" filename="mask8" x="0" y="0" visible="1"/>
    </masks>
   </layer>
   <layer name="Group" nodetype="grouplayer" passthrough="0" filename="layer2" compositeop="screen" opacity="200" visible="1" x="0" y="0" collapsed="1" colorlabel="2">
    <layers>
     <layer name="Clipped" nodetype="paintlayer" filename="layer4" compositeop="multiply" opacity="128" visible="0" channelflags="1110" x="5" y="-3" colorspacename="RGBA"/>
     <layer name="Base" nodetype="paintlayer" filename="layer3" compositeop="normal" opacity="255" visible="1" locked="1" channellockflags="1110" x="0" y="0" colorspacename="RGBA">
      <masks>
       <mask name="Mask" nodetype="transparencymask" filename="mask5" x="0" y="0" visible="1"/>
      </masks>
     </layer>
    </layers>
   </layer>
   <layer name="Through" nodetype="grouplayer" passthrough="1" filename="layer7" compositeop="multiply" opacity="255" visible="1" x="0" y="0">
    <layers/>
   </layer>
   <layer name="Escape" nodetype="paintlayer" filename="../evil" compositeop="normal" opacity="255" visible="1" x="0" y="0" colorspacename="RGBA"/>
   <layer name="Background" nodetype="paintlayer" filename="layer1" compositeop="normal" opacity="255" visible="1" x="0" y="0" colorspacename="RGBA"/>
  </layers>
  <guides>
   <horizontalGuides type="array"><item_0 value="12" type="value"/></horizontalGuides>
   <verticalGuides type="array"><item_0 value="24" type="value"/><item_1 value="-4.8" type="value"/></verticalGuides>
  </guides>
 </IMAGE>
</DOC>
)";

std::vector<patchy::ora::detail::ZipEntry> synthetic_entries() {
  std::vector<patchy::ora::detail::ZipEntry> entries;
  entries.push_back({"mimetype", bytes_of("application/x-krita"), false});
  entries.push_back({"maindoc.xml", bytes_of(kSyntheticMaindoc), true});
  // Background: no tiles, a white default pixel.
  entries.push_back({"Doc/layers/layer1", tile_file(4, {}), true});
  entries.push_back({"Doc/layers/layer1.defaultpixel", {255, 255, 255, 255}, false});
  // Base: one stored tile at (0, 0) and one LZF tile at (64, 0); blue-green-red-alpha bytes.
  entries.push_back({"Doc/layers/layer3",
                     tile_file(4, {{0, 0,
                                    bgra_tile([](int x, int y) {
                                      return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(x * 4), 10,
                                                                         static_cast<std::uint8_t>(y * 4), 255};
                                    }),
                                    true},
                                   {64, 0, bgra_tile([](int, int) {
                                      return std::array<std::uint8_t, 4>{200, 100, 50, 128};
                                    })}}),
                     true});
  entries.push_back({"Doc/layers/layer4",
                     tile_file(4, {{0, 0, bgra_tile([](int, int) { return std::array<std::uint8_t, 4>{0, 0, 255, 255}; })}}),
                     true});
  // The transparency mask: the left half selected.
  {
    std::vector<std::uint8_t> mask(64U * 64U);
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 64; ++x) {
        mask[static_cast<std::size_t>(y * 64 + x)] = x < 32 ? 255 : 0;
      }
    }
    entries.push_back({"Doc/layers/mask5.pixelselection", tile_file(1, {{0, 0, mask}}), true});
    entries.push_back({"Doc/layers/mask5.pixelselection.defaultpixel", {0}, false});
  }
  entries.push_back({"Doc/layers/mask8.pixelselection",
                     tile_file(1, {{0, 0, std::vector<std::uint8_t>(64U * 64U, 255)}}), true});
  entries.push_back({"Doc/layers/layer6",
                     tile_file(4, {{0, 0, bgra_tile([](int, int) { return std::array<std::uint8_t, 4>{1, 2, 3, 4}; })}}),
                     true});
  return entries;
}

std::vector<std::uint8_t> synthetic_file() {
  return patchy::ora::detail::zip_archive(synthetic_entries());
}

void kra_lzf_decompresses_literals_and_back_references() {
  // "abc", then a back reference of 6 at distance 3, then a long run (an extra length byte).
  const std::vector<std::uint8_t> packed = {0x02, 'a', 'b', 'c', 0x80, 0x02, 0x00, 'z', 0xE0, 11, 0x00};
  std::vector<std::uint8_t> out(9 + 1 + 20);
  CHECK(kra::detail::lzf_decompress(packed, out));
  CHECK(std::string(out.begin(), out.begin() + 9) == "abcabcabc");
  CHECK(std::all_of(out.begin() + 9, out.end(), [](std::uint8_t ch) { return ch == 'z'; }));
  // Too short an output, a reference before the start, and a cut stream all fail.
  std::vector<std::uint8_t> short_out(8);
  CHECK(!kra::detail::lzf_decompress(packed, short_out));
  const std::vector<std::uint8_t> before_start = {0x00, 'a', 0x20, 0x05};
  std::vector<std::uint8_t> four(4);
  CHECK(!kra::detail::lzf_decompress(before_start, four));
  const std::vector<std::uint8_t> cut = {0x05, 'a', 'b'};
  CHECK(!kra::detail::lzf_decompress(cut, four));
  // The test encoder round-trips through the decoder.
  std::vector<std::uint8_t> data(5000);
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<std::uint8_t>((i / 300) % 2 == 0 ? 7 : i * 13);
  }
  std::vector<std::uint8_t> back(data.size());
  CHECK(kra::detail::lzf_decompress(lzf_compress_runs(data), back));
  CHECK(back == data);
}

void kra_reader_maps_the_layer_tree() {
  std::vector<std::string> notices;
  const auto read = kra::DocumentIo::read(synthetic_file(), &notices);
  CHECK(read.width() == 100 && read.height() == 80);
  CHECK(read.color_state().bit_depth == BitDepth::UInt8);
  CHECK(std::abs(read.print_settings().horizontal_ppi - 150.0) < 1e-9);
  // Bottom first: Background, Escape, Through, Group [Base, Clipped], Unknown op.
  CHECK(read.layers().size() == 5U);
  const auto& background = read.layers()[0];
  CHECK(background.name() == "Background");
  // A white default pixel and no tiles: the layer covers the canvas.
  CHECK(background.bounds() == rect_of(0, 0, 100, 80));
  CHECK(background.pixels().pixel(99, 79)[0] == 255 && background.pixels().pixel(99, 79)[3] == 255);
  CHECK(read.layers()[1].name() == "Escape" && read.layers()[1].pixels().empty());
  CHECK(read.layers()[2].name() == "Through" && read.layers()[2].blend_mode() == BlendMode::PassThrough);
  const auto& group = read.layers()[3];
  CHECK(group.kind() == LayerKind::Group && group.blend_mode() == BlendMode::Screen);
  CHECK(std::abs(group.opacity() - 200.0F / 255.0F) < 1e-6F);
  CHECK(group.color_label() == patchy::kLayerColorGreen);
  CHECK(group.children().size() == 2U);
  const auto& base = group.children()[0];
  CHECK(base.name() == "Base" && base.bounds() == rect_of(0, 0, 128, 64));
  CHECK(base.lock_flags() == patchy::kLayerLockAll);
  // Krita's blue-green-red order becomes red-green-blue.
  const auto* px = base.pixels().pixel(5, 7);
  CHECK(px[0] == 28 && px[1] == 10 && px[2] == 20 && px[3] == 255);
  const auto* lzf = base.pixels().pixel(100, 3);
  CHECK(lzf[0] == 50 && lzf[1] == 100 && lzf[2] == 200 && lzf[3] == 128);
  CHECK(base.mask().has_value());
  if (base.mask().has_value()) {
    const auto& mask = *base.mask();
    CHECK(mask.bounds == rect_of(0, 0, 64, 64) && mask.default_color == 0);
    CHECK(mask.pixels.pixel(10, 10)[0] == 255 && mask.pixels.pixel(40, 10)[0] == 0);
  }
  const auto& clipped = group.children()[1];
  CHECK(clipped.name() == "Clipped" && clipped.clipped() && !clipped.visible());
  CHECK(clipped.blend_mode() == BlendMode::Multiply);
  CHECK(clipped.bounds() == rect_of(5, -3, 64, 64));
  CHECK(read.layers()[4].blend_mode() == BlendMode::Normal);
  CHECK(notices_mention(notices, "Unknown op (freeze)"));
  // The selection mask is a saved selection: an alpha channel, white where selected.
  CHECK(read.channels().size() == 1U);
  if (!read.channels().empty()) {
    const auto& channel = read.channels().front();
    CHECK(channel.name() == "Unknown op Saved" && channel.kind() == patchy::DocumentChannelKind::Alpha);
    CHECK(channel.pixels().pixel(5, 5)[0] == 255 && channel.pixels().pixel(90, 70)[0] == 0);
  }
  // Guides are stored in points: 12 pt at 150 ppi is 25 px.
  CHECK(read.guides().size() == 3U);
  CHECK(read.guides()[0].orientation == patchy::GuideOrientation::Horizontal && read.guides()[0].position_32 == 25 * 32);
  CHECK(read.guides()[1].orientation == patchy::GuideOrientation::Vertical && read.guides()[1].position_32 == 50 * 32);
  CHECK(read.guides()[2].position_32 == -10 * 32);
}

void kra_sniff_and_registry_recognize_krita_files() {
  const auto bytes = synthetic_file();
  CHECK(kra::sniff(bytes));
  CHECK(!patchy::ora::sniff(bytes));
  CHECK(!kra::sniff(bytes_of("PK\x03\x04 a zip header but no Krita mimetype at all, padding padding")));
  const auto* handler = patchy::builtin_format_registry().find_by_extension(".kra");
  CHECK(handler != nullptr && handler->identifier == "patchy.formats.kra" && !handler->can_write());
  CHECK(patchy::builtin_format_registry().find_by_extension(".krz") == handler);
  const auto sniffed = patchy::formats::sniff_openable_image_format(bytes);
  CHECK(sniffed.has_value() && sniffed->extension == "kra");
  CHECK(handler != nullptr && handler->read(bytes).document.layers().size() == 5U);
}

void kra_unicode_path_reads() {
  // Every new file-reading entry point gets a Unicode-path test (AGENTS.md).
  const auto dir = patchy::test::unicode_artifact_dir(u8"kra");
  std::filesystem::create_directories(dir);
  const auto path = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem).concat(".kra");
  const auto bytes = synthetic_file();
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto read = kra::DocumentIo::read_file(path);
  CHECK(read.layers().size() == 5U && find_named(read.layers(), "Base") != nullptr);
  // A file layer beside the document, also under a Unicode name, loads from the file's folder.
  const auto entries = synthetic_entries();
  std::string maindoc(kSyntheticMaindoc);
  const std::string file_layer_name = std::string(reinterpret_cast<const char*>(patchy::test::kUnicodeCombinedStem.data()),
                                                  patchy::test::kUnicodeCombinedStem.size()) +
                                      "-ref.png";
  const auto at = maindoc.find("   <layer name=\"Background\"");
  maindoc.insert(at, "   <layer name=\"File\" nodetype=\"filelayer\" filename=\"layer9\" source=\"" + file_layer_name +
                         "\" compositeop=\"normal\" opacity=\"255\" visible=\"1\" x=\"7\" y=\"9\"/>\n");
  auto with_file = entries;
  with_file[1].data = bytes_of(maindoc);
  PixelBuffer reference(3, 2, PixelFormat::rgba8());
  reference.clear(0);
  reference.pixel(1, 1)[0] = 200;
  reference.pixel(1, 1)[3] = 255;
  const auto png = patchy::ora::detail::encode_png(reference);
  {
    std::ofstream out(dir / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(file_layer_name.data()), file_layer_name.size())), std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  }
  const auto container = patchy::ora::detail::zip_archive(with_file);
  const auto document_path = dir / "file-layer.kra";
  {
    std::ofstream out(document_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(container.data()), static_cast<std::streamsize>(container.size()));
  }
  std::vector<std::string> notices;
  const auto with_layer = kra::DocumentIo::read_file(document_path, &notices);
  const auto* file_layer = find_named(with_layer.layers(), "File");
  CHECK(file_layer != nullptr && file_layer->bounds() == rect_of(7, 9, 3, 2));
  CHECK(file_layer != nullptr && file_layer->pixels().pixel(1, 1)[0] == 200);
  CHECK(notices_mention(notices, "File layers were loaded"));
  // Without the folder (bytes only) the layer opens empty with a notice.
  notices.clear();
  const auto bytes_only = kra::DocumentIo::read(container, &notices);
  const auto* empty_file = find_named(bytes_only.layers(), "File");
  CHECK(empty_file != nullptr && empty_file->pixels().empty());
  CHECK(notices_mention(notices, "File layers whose file was not found"));
}

void kra_reader_refuses_hostile_and_broken_files() {
  const auto good = synthetic_file();
  const auto expect_throw = [](const std::vector<std::uint8_t>& bytes) {
    bool threw = false;
    try {
      (void)kra::DocumentIo::read(bytes);
    } catch (const std::exception&) {
      threw = true;
    }
    CHECK(threw);
  };
  expect_throw(bytes_of("not a zip"));
  expect_throw(std::vector<std::uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 2)));
  {
    auto entries = synthetic_entries();
    entries[0].data = bytes_of("image/openraster");
    expect_throw(patchy::ora::detail::zip_archive(entries));
  }
  {
    auto entries = synthetic_entries();
    entries[1].data = bytes_of("<DOC><IMAGE width=\"0\" height=\"5\"/></DOC>");
    expect_throw(patchy::ora::detail::zip_archive(entries));
  }
  {
    // Damaged tile data opens the layer empty with a notice; huge claims are refused.
    auto entries = synthetic_entries();
    for (auto& entry : entries) {
      if (entry.name == "Doc/layers/layer3") {
        entry.data.resize(entry.data.size() - 40);
      }
      if (entry.name == "Doc/layers/layer4") {
        entry.data = bytes_of("VERSION 2\nTILEWIDTH 64\nTILEHEIGHT 64\nPIXELSIZE 4\nDATA 3\n0,0,LZF,4\n\x01\x00\x00\x00"sv);
      }
      if (entry.name == "Doc/layers/layer6") {
        entry.data = bytes_of("VERSION 2\nTILEWIDTH 64\nTILEHEIGHT 64\nPIXELSIZE 4\nDATA 1\n9999999,0,LZF,2\n\x01\x00"sv);
      }
    }
    std::vector<std::string> notices;
    const auto read = kra::DocumentIo::read(patchy::ora::detail::zip_archive(entries), &notices);
    CHECK(find_named(read.layers(), "Base")->pixels().empty());
    CHECK(find_named(read.layers(), "Clipped")->pixels().empty());
    CHECK(find_named(read.layers(), "Unknown op")->pixels().empty());
    CHECK(notices_mention(notices, "could not be read"));
  }
  {
    // A tile count past the pixel budget refuses the file before anything is allocated.
    auto entries = synthetic_entries();
    entries[4].data = bytes_of("VERSION 2\nTILEWIDTH 64\nTILEHEIGHT 64\nPIXELSIZE 4\nDATA 999999999\n");
    expect_throw(patchy::ora::detail::zip_archive(entries));
  }
  // Truncations and byte flips never crash: they open or throw.
  for (std::size_t cut = 0; cut < good.size(); cut += std::max<std::size_t>(1, good.size() / 97)) {
    try {
      (void)kra::DocumentIo::read(std::vector<std::uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut)));
    } catch (const std::exception&) {
    }
  }
  for (std::size_t at = 0; at < good.size(); at += 7) {
    auto bytes = good;
    bytes[at] = static_cast<std::uint8_t>(bytes[at] ^ 0x5A);
    try {
      (void)kra::DocumentIo::read(bytes);
    } catch (const std::exception&) {
    }
  }
  // The same mutations on the stored tile file itself (no zip checksum in the way).
  auto entries = synthetic_entries();
  const auto tile_index = 4U;  // layer3 (two tiles, one stored, one LZF)
  const auto original = entries[tile_index].data;
  for (std::size_t at = 0; at < original.size(); at += 3) {
    entries[tile_index].data = original;
    entries[tile_index].data[at] = static_cast<std::uint8_t>(entries[tile_index].data[at] ^ 0xA5);
    try {
      (void)kra::DocumentIo::read(patchy::ora::detail::zip_archive(entries));
    } catch (const std::exception&) {
    }
  }
}

// ---------------------------------------------------------------------------------------
// Krita's own files.

struct Difference {
  double mean{0.0};
  int worst{0};
  bool compared{false};
};

std::optional<PixelBuffer> decode_png(const std::vector<std::uint8_t>& png) {
  int width = 0;
  int height = 0;
  int components = 0;
  stbi_uc* data = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &components, 4);
  if (data == nullptr) {
    return std::nullopt;
  }
  PixelBuffer out(width, height, PixelFormat::rgba8());
  std::memcpy(out.data().data(), data, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  stbi_image_free(data);
  return out;
}

// The open path's profile step (docs/rgb-profiles.md), so a file in another RGB profile
// compares in sRGB like Krita's merged image.
void convert_like_open(Document& document) {
  if (patchy::rgb_profile_needs_conversion(document.color_state().embedded_icc_profile)) {
    (void)patchy::convert_document_rgb_profile(document, {}, patchy::RenderingIntent::RelativeColorimetric, true);
  }
}

// Mean and worst difference in 8-bit units on premultiplied RGBA, so the color of fully
// transparent pixels does not count.
Difference compare_with(const PixelBuffer& ours, const PixelBuffer& theirs) {
  Difference result;
  if (ours.width() != theirs.width() || ours.height() != theirs.height()) {
    return result;
  }
  const auto a = ours.data();
  const auto b = theirs.data();
  double total = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const auto pixel = i / 4U * 4U;
    const int pa = a[i] * (i % 4 == 3 ? 255 : a[pixel + 3]) / 255;
    const int pb = b[i] * (i % 4 == 3 ? 255 : b[pixel + 3]) / 255;
    total += std::abs(pa - pb);
    result.worst = std::max(result.worst, std::abs(pa - pb));
  }
  result.mean = total / static_cast<double>(a.size());
  result.compared = true;
  return result;
}

// Krita's own render, mergedimage.png inside the file.
std::optional<PixelBuffer> merged_image(const std::vector<std::uint8_t>& kra_bytes) {
  mz_zip_archive zip{};
  if (mz_zip_reader_init_mem(&zip, kra_bytes.data(), kra_bytes.size(), 0) == MZ_FALSE) {
    return std::nullopt;
  }
  std::size_t size = 0;
  void* data = mz_zip_reader_extract_file_to_heap(&zip, "mergedimage.png", &size, 0);
  mz_zip_reader_end(&zip);
  if (data == nullptr) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> png(static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
  mz_free(data);
  return decode_png(png);
}

// Every `factor` x `factor` block averaged (straight RGBA, alpha-weighted): a smaller
// artifact for the corpus's large paintings.
PixelBuffer box_downscale(const PixelBuffer& pixels, int factor) {
  if (factor <= 1) {
    return pixels;
  }
  const auto width = std::max(1, pixels.width() / factor);
  const auto height = std::max(1, pixels.height() / factor);
  PixelBuffer out(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      std::uint64_t sum[4] = {0, 0, 0, 0};
      for (int dy = 0; dy < factor; ++dy) {
        for (int dx = 0; dx < factor; ++dx) {
          const auto* px = pixels.pixel(std::min(pixels.width() - 1, x * factor + dx),
                                        std::min(pixels.height() - 1, y * factor + dy));
          for (int c = 0; c < 3; ++c) {
            sum[c] += static_cast<std::uint64_t>(px[c]) * px[3];
          }
          sum[3] += px[3];
        }
      }
      auto* target = out.pixel(x, y);
      for (int c = 0; c < 3; ++c) {
        target[c] = sum[3] == 0 ? 0 : static_cast<std::uint8_t>(sum[c] / sum[3]);
      }
      target[3] = static_cast<std::uint8_t>(sum[3] / static_cast<std::uint64_t>(factor * factor));
    }
  }
  return out;
}

void write_artifact_png(const std::string& name, const PixelBuffer& pixels, const char* folder = "kra") {
  const auto dir = std::filesystem::path("test-artifacts") / folder;
  std::filesystem::create_directories(dir);
  const auto png = patchy::ora::detail::encode_png(pixels);
  std::ofstream out(dir / (name + "-patchy.png"), std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

// Per fixture: mean and worst allowed (8-bit units, premultiplied RGBA), against Krita's
// merged image or, for the fixtures Krita made from Patchy's PSDs, against Patchy's render of
// that PSD (Krita draws layer styles with its own engine). The measured values and why each
// limit is what it is: docs/kra.md "Measured differences". Fixtures not listed are reported
// only (Krita features Patchy approximates or leaves out: blend modes, fills, some filters).
struct FixtureLimit {
  const char* name;
  double mean;
  int worst;
  bool against_psd;
};

constexpr FixtureLimit kFixtureLimits[] = {
    {"structure-u8", 0.05, 1, false},   {"structure-u16", 0.01, 1, false}, {"structure-f16", 0.01, 1, false},
    {"structure-f32", 0.01, 1, false},  {"gray-u8", 0.1, 1, false},        {"gray-u16", 0.01, 1, false},
    {"masks", 0.0, 0, false},           {"channels", 0.0, 0, false},       {"clone-file", 0.0, 0, false},
    {"document", 0.0, 0, false},        {"profile-clay", 0.0, 0, false},   {"large", 0.01, 1, false},
    {"cmyk-u8", 4.5, 100, false},
    {"psd-emboss-styles", 0.01, 1, true},             {"psd-global-light-shadow", 0.01, 1, true},
    {"psd-inner-glow", 0.01, 1, true},                {"psd-inner-shadow", 0.01, 1, true},
    {"psd-outer-glow", 0.01, 1, true},                {"psd-outer-glow-range", 0.01, 1, true},
    {"psd-pattern-overlay", 0.01, 1, true},           {"psd-pillow-emboss", 0.01, 1, true},
    {"psd-satin-default", 0.01, 1, true},             {"psd-shape-solid", 0.01, 1, true},
    {"psd-stroke-positions", 0.01, 1, true},          {"psd-gradient-overlay-geometry", 1.0, 64, true},
    {"psd-inner-glow-range", 0.5, 64, true},          {"psd-interior-exterior-blending", 1.0, 160, true},
};

void kra_reads_krita_fixtures_if_available() {
  DeepEditingScope deep(true);
  const auto dir = patchy::test::local_format_fixture_path("kra", "");
  patchy::test::skip_unless_exists(patchy::test::local_format_fixture_path("kra", "structure-u8.kra"));
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() == ".kra") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  std::map<std::string, FixtureLimit> limits;
  for (const auto& limit : kFixtureLimits) {
    limits[limit.name] = limit;
  }
  for (const auto& path : files) {
    const auto stem = path.stem().string();
    std::vector<std::string> notices;
    auto read = kra::DocumentIo::read_file(path, &notices);
    convert_like_open(read);
    const auto ours = patchy::flatten_document_rgba8(read);
    write_artifact_png(stem, ours);
    auto png_path = path;
    png_path.replace_extension(".png");
    Difference difference;
    Difference against_psd;
    if (std::filesystem::exists(png_path)) {
      if (const auto theirs = decode_png(patchy::test::read_binary_file(png_path)); theirs.has_value()) {
        difference = compare_with(ours, *theirs);
      }
    }
    std::cout << "  " << std::left << std::setw(36) << stem << " depth " << static_cast<int>(read.color_state().bit_depth)
              << "  mean " << std::fixed << std::setprecision(3) << difference.mean << "  worst " << difference.worst
              << (difference.compared ? "" : "  (not compared)");
    // A fixture Krita made from one of Patchy's PSDs: Patchy's render of that PSD is the
    // second reference, the one layer styles are calibrated to (Photoshop's look).
    if (stem.rfind("psd-", 0) == 0) {
      const auto source = patchy::test::committed_psd_fixture_path("photoshop-" + stem.substr(4) + ".psd");
      if (std::filesystem::exists(source)) {
        const auto psd = patchy::psd::DocumentIo::read_file(source);
        const auto psd_render = patchy::flatten_document_rgba8(psd);
        write_artifact_png(stem + "-psd", psd_render);
        against_psd = compare_with(ours, psd_render);
        std::cout << "  | against the PSD: mean " << against_psd.mean << "  worst " << against_psd.worst;
      }
    }
    std::cout << "\n";
    for (const auto& notice : notices) {
      std::cout << "      " << notice << "\n";
    }
    if (const auto found = limits.find(stem); found != limits.end()) {
      const auto& measured = found->second.against_psd ? against_psd : difference;
      CHECK(measured.compared);
      CHECK(measured.mean <= found->second.mean);
      CHECK(measured.worst <= found->second.worst);
    }
  }
}

// The corpus sweep takes several minutes (65 paintings up to 2800 x 12658), past the
// harness's per-test limit, so it runs only on request: PATCHY_KRA_CORPUS=1 and
// PATCHY_TEST_TIMEOUT_SECONDS=0 (docs/kra.md "Tests").
void kra_reads_corpus_if_available() {
  const char* requested = std::getenv("PATCHY_KRA_CORPUS");
  if (requested == nullptr || std::string_view(requested) != "1") {
    patchy::test::skip("the corpus sweep runs with PATCHY_KRA_CORPUS=1");
  }
  DeepEditingScope deep(true);
  const auto dir = patchy::test::local_format_fixture_path("kra-corpus", "");
  patchy::test::skip_unless_exists(patchy::test::local_format_fixture_path("kra-corpus", "README.txt"));
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() == ".kra") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  int failures = 0;
  for (const auto& path : files) {
    const auto stem = path.stem().string();
    try {
      std::vector<std::string> notices;
      auto read = kra::DocumentIo::read_file(path, &notices);
      convert_like_open(read);
      const auto ours = patchy::flatten_document_rgba8(read);
      const int factor = std::max(1, (std::max(ours.width(), ours.height()) + 1199) / 1200);
      write_artifact_png(stem, box_downscale(ours, factor), "kra-corpus");
      Difference difference;
      // Krita's own render inside the file.
      const auto bytes = patchy::test::read_binary_file(path);
      std::cout << "  " << std::left << std::setw(46) << stem << " " << read.width() << "x" << read.height()
                << " depth " << static_cast<int>(read.color_state().bit_depth);
      if (const auto merged = merged_image(bytes); merged.has_value()) {
        difference = compare_with(ours, *merged);
      }
      std::cout << "  mean " << std::fixed << std::setprecision(3) << difference.mean << "  worst " << difference.worst
                << (difference.compared ? "" : "  (no merged image)") << "\n";
      for (const auto& notice : notices) {
        std::cout << "      " << notice << "\n";
      }
    } catch (const std::exception& error) {
      ++failures;
      std::cout << "  " << stem << " FAILED: " << error.what() << "\n";
    }
  }
  CHECK(failures == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> kra_tests() {
  return {
      {"kra_lzf_decompresses_literals_and_back_references", kra_lzf_decompresses_literals_and_back_references},
      {"kra_reader_maps_the_layer_tree", kra_reader_maps_the_layer_tree},
      {"kra_sniff_and_registry_recognize_krita_files", kra_sniff_and_registry_recognize_krita_files},
      {"kra_unicode_path_reads", kra_unicode_path_reads},
      {"kra_reader_refuses_hostile_and_broken_files", kra_reader_refuses_hostile_and_broken_files},
      {"kra_reads_krita_fixtures_if_available", kra_reads_krita_fixtures_if_available},
      {"kra_reads_corpus_if_available", kra_reads_corpus_if_available},
  };
}
