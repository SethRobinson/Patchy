// OpenRaster (.ora) reader and writer (docs/ora.md): round trips at 8 and 16 bits, the
// byte canary, Unicode paths, hostile input, what a save bakes, and the files GIMP and
// Krita wrote (local fixtures, skipped when absent).

#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/image_open_diagnosis.hpp"
#include "formats/miniz/miniz.h"
#include "formats/ora_document_io.hpp"
#include "formats/stb/stb_image.h"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace ora = patchy::ora;
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

// Deterministic RGBA with varying color and coverage; `seed` keeps layers distinct. At 16
// bits the samples are not multiples of 257, so an 8-bit detour shows.
PixelBuffer pattern(std::int32_t width, std::int32_t height, int seed, BitDepth depth = BitDepth::UInt8,
                    bool opaque = false) {
  PixelBuffer pixels(width, height, patchy::with_bit_depth(PixelFormat::rgba8(), depth));
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const std::array<int, 4> value8 = {(x * 37 + seed * 11) % 256, (y * 53 + seed * 29) % 256,
                                         ((x + y) * 17 + seed * 7) % 256,
                                         opaque ? 255 : ((x * 5 + y * 3 + seed) % 7 == 0 ? 0 : 60 + (x * y + seed) % 196)};
      auto* px = pixels.pixel(x, y);
      for (int c = 0; c < 4; ++c) {
        if (depth == BitDepth::UInt16) {
          const auto value = static_cast<std::uint16_t>(value8[static_cast<std::size_t>(c)] * 257 -
                                                        (c == 3 && value8[3] == 255 ? 0 : (x + y + c) % 200));
          const auto stored = value8[static_cast<std::size_t>(c)] == 0 ? std::uint16_t{0} : value;
          std::memcpy(px + c * 2, &stored, sizeof(stored));
        } else {
          px[c] = static_cast<std::uint8_t>(value8[static_cast<std::size_t>(c)]);
        }
      }
    }
  }
  return pixels;
}

Layer pixel_layer(Document& document, std::string name, Rect bounds, int seed, bool opaque = false) {
  Layer layer(document.allocate_layer_id(), std::move(name),
              pattern(bounds.width, bounds.height, seed, document.color_state().bit_depth, opaque));
  layer.set_bounds(bounds);
  return layer;
}

// Every blend mode the writer has a name for, in enum order.
std::vector<BlendMode> written_blend_modes() {
  std::vector<BlendMode> modes;
  for (int value = static_cast<int>(BlendMode::Normal); value <= static_cast<int>(BlendMode::Dissolve); ++value) {
    modes.push_back(static_cast<BlendMode>(value));
  }
  return modes;
}

std::string unicode_name() {
  return patchy::test::utf8_string(u8"\u30EC\u30A4\u30E4\u30FC caf\u00E9 \U0001F3A8");
}

// Nested groups (an isolated one holding a pass-through one), offsets on and off the
// canvas, opacities, a hidden layer, a Unicode name, an empty layer, one tile per blend
// mode (unless `mode_tiles` is off), and a selected layer.
Document structure_document(BitDepth depth, bool mode_tiles = true) {
  Document document(96, 64, patchy::with_bit_depth(PixelFormat::rgba8(), depth));
  document.color_state().bit_depth = depth;
  document.print_settings().horizontal_ppi = 150.0;
  document.print_settings().vertical_ppi = 144.0;
  document.add_layer(pixel_layer(document, "Background", Rect{0, 0, 96, 64}, 1, /*opaque*/ true));

  Layer outer(document.allocate_layer_id(), "Outer group", LayerKind::Group);
  outer.set_opacity(0.75F);
  outer.add_child(pixel_layer(document, "Offset", Rect{-6, 10, 20, 16}, 2));
  Layer inner(document.allocate_layer_id(), "Inner pass-through", LayerKind::Group);
  inner.set_blend_mode(BlendMode::PassThrough);
  auto multiply = pixel_layer(document, "Multiply", Rect{30, 5, 24, 20}, 3);
  multiply.set_blend_mode(BlendMode::Multiply);
  inner.add_child(std::move(multiply));
  auto hidden = pixel_layer(document, "Hidden", Rect{50, 30, 12, 12}, 4);
  hidden.set_visible(false);
  inner.add_child(std::move(hidden));
  outer.add_child(std::move(inner));
  document.add_layer(std::move(outer));

  int index = 0;
  for (const auto mode : mode_tiles ? written_blend_modes() : std::vector<BlendMode>{}) {
    auto tile = pixel_layer(document, "mode " + std::to_string(static_cast<int>(mode)),
                            Rect{(index % 12) * 8, 40 + (index / 12) * 8, 8, 8}, 10 + index);
    tile.set_blend_mode(mode);
    document.add_layer(std::move(tile));
    ++index;
  }
  auto unicode = pixel_layer(document, unicode_name(), Rect{70, 50, 30, 20}, 5);
  unicode.set_opacity(127.0F / 255.0F);
  const auto unicode_id = unicode.id();
  document.add_layer(std::move(unicode));
  document.add_layer(Layer(document.allocate_layer_id(), "Empty",
                           PixelBuffer(96, 64, patchy::with_bit_depth(PixelFormat::rgba8(), depth))));
  document.set_active_layer(unicode_id);
  return document;
}

void check_same_tree(const std::vector<Layer>& expected, const std::vector<Layer>& actual) {
  CHECK(expected.size() == actual.size());
  for (std::size_t i = 0; i < std::min(expected.size(), actual.size()); ++i) {
    const auto& a = expected[i];
    const auto& b = actual[i];
    CHECK(a.name() == b.name());
    CHECK(a.kind() == b.kind());
    CHECK(a.visible() == b.visible());
    CHECK(a.opacity() == b.opacity());
    CHECK(a.blend_mode() == b.blend_mode());
    CHECK(a.clipped() == b.clipped());
    if (a.kind() == LayerKind::Group) {
      check_same_tree(a.children(), b.children());
      continue;
    }
    CHECK(a.bounds() == b.bounds());
    CHECK(a.pixels().format() == b.pixels().format());
    const auto left = a.pixels().data();
    const auto right = b.pixels().data();
    CHECK(left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin()));
  }
}

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

bool notices_mention(const std::vector<std::string>& notices, std::string_view text) {
  return std::any_of(notices.begin(), notices.end(),
                     [text](const std::string& notice) { return notice.find(text) != std::string::npos; });
}

std::vector<std::uint8_t> bytes_of(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

// A hand-built archive: the mimetype, the given stack.xml and extra entries.
std::vector<std::uint8_t> archive(std::string_view stack_xml, std::vector<ora::detail::ZipEntry> extra = {},
                                  std::string_view mimetype = "image/openraster") {
  std::vector<ora::detail::ZipEntry> entries;
  if (!mimetype.empty()) {
    entries.push_back({"mimetype", bytes_of(mimetype), false});
  }
  if (!stack_xml.empty()) {
    entries.push_back({"stack.xml", bytes_of(stack_xml), true});
  }
  for (auto& entry : extra) {
    entries.push_back(std::move(entry));
  }
  return ora::detail::zip_archive(entries);
}

ora::detail::ZipEntry png_entry(std::string name, std::int32_t width, std::int32_t height, int seed) {
  return {std::move(name), ora::detail::encode_png(pattern(width, height, seed)), false};
}

bool read_throws(const std::vector<std::uint8_t>& bytes) {
  try {
    (void)ora::DocumentIo::read(bytes);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

void ora_round_trip_keeps_layers_and_pixels() {
  const auto document = structure_document(BitDepth::UInt8);
  std::vector<std::string> write_notices;
  const auto bytes = ora::DocumentIo::write(document, &write_notices);
  CHECK(ora::sniff(bytes));
  std::vector<std::string> notices;
  const auto read = ora::DocumentIo::read(bytes, &notices);
  CHECK(read.width() == 96 && read.height() == 64);
  CHECK(read.color_state().bit_depth == BitDepth::UInt8);
  CHECK(read.print_settings().horizontal_ppi == 150.0 && read.print_settings().vertical_ppi == 144.0);
  check_same_tree(document.layers(), read.layers());
  CHECK(read.active_layer_id().has_value());
  if (read.active_layer_id().has_value()) {
    const auto* selected = read.find_layer(*read.active_layer_id());
    CHECK(selected != nullptr && selected->name() == unicode_name());
  }
  CHECK(notices.empty());
  // Exclusion and the other Photoshop-only modes go under Krita's names; that is the only
  // thing this document loses.
  for (const auto& loss : ora::DocumentIo::lost_content(document)) {
    CHECK(loss.kind == ora::LossKind::BlendModeKrita);
  }
  const auto expected = patchy::flatten_document_rgba8(document);
  const auto actual = patchy::flatten_document_rgba8(read);
  CHECK(std::equal(expected.data().begin(), expected.data().end(), actual.data().begin()));
}

void ora_round_trip_keeps_16_bit_samples() {
  const DeepEditingScope deep(true);
  const auto document = structure_document(BitDepth::UInt16);
  const auto bytes = ora::DocumentIo::write(document);
  const auto read = ora::DocumentIo::read(bytes);
  CHECK(read.color_state().bit_depth == BitDepth::UInt16);
  check_same_tree(document.layers(), read.layers());
  {
    // With deep editing off the same file opens at 8 bits and says so.
    const DeepEditingScope shallow(false);
    std::vector<std::string> notices;
    const auto narrow = ora::DocumentIo::read(bytes, &notices);
    CHECK(narrow.color_state().bit_depth == BitDepth::UInt8);
    CHECK(notices_mention(notices, "16-bit layers were converted"));
  }
  // 32 bits writes 16-bit PNGs and reports it.
  auto linear = document;
  patchy::convert_document_depth(linear, BitDepth::Float32);
  const auto losses = ora::DocumentIo::lost_content(linear);
  CHECK(std::any_of(losses.begin(), losses.end(), [](const ora::Loss& loss) { return loss.kind == ora::LossKind::Depth32; }));
  const DeepEditingScope deep_again(true);  // the inner scope cleared the override
  const auto from_linear = ora::DocumentIo::read(ora::DocumentIo::write(linear));
  CHECK(from_linear.color_state().bit_depth == BitDepth::UInt16);
}

void ora_rewrite_is_byte_identical() {
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16}) {
    const DeepEditingScope deep(true);
    const auto first = ora::DocumentIo::write(structure_document(depth));
    const auto second = ora::DocumentIo::write(ora::DocumentIo::read(first));
    CHECK(first == second);
    // And the same document twice is the same file.
    CHECK(first == ora::DocumentIo::write(structure_document(depth)));
  }
}

void ora_writer_bytes_are_stable() {
  // Cross-platform byte-identical rule: the whole file (zip records, stack.xml, the PNG
  // encoder's filters and deflate, the compositor's merged image and the thumbnail) is
  // pinned by an FNV-1a hash. Re-pin only for a deliberate writer change (the failure
  // prints the new hash).
  Document document(40, 30, PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_layer(pixel_layer(document, "Background", Rect{0, 0, 40, 30}, 1, true));
  Layer group(document.allocate_layer_id(), "Group", LayerKind::Group);
  group.set_opacity(0.5F);
  auto screen = pixel_layer(document, "Screen", Rect{5, 4, 20, 18}, 2);
  screen.set_blend_mode(BlendMode::Screen);
  group.add_child(std::move(screen));
  document.add_layer(std::move(group));
  document.add_layer(pixel_layer(document, "Top", Rect{-3, 12, 16, 10}, 3));
  const auto bytes = ora::DocumentIo::write(document);
  constexpr std::uint64_t kExpected = 0x5129ed6953c0385eULL;
  const auto hash = patchy::test::fnv1a_hash_bytes(bytes);
  if (hash != kExpected) {
    std::cout << "  ora writer hash: 0x" << std::hex << hash << std::dec << " size " << bytes.size() << "\n";
  }
  CHECK(hash == kExpected);
}

void ora_writes_the_spec_layout() {
  const auto bytes = ora::DocumentIo::write(structure_document(BitDepth::UInt8));
  const auto* handler = patchy::builtin_format_registry().find_by_extension("ora");
  CHECK(handler != nullptr && handler->identifier == "patchy.formats.ora" && handler->can_write());
  CHECK(handler != nullptr && handler->sniff != nullptr && handler->sniff(bytes));
  CHECK(!ora::sniff(bytes_of("PK\x03\x04 not openraster at all, just a zip header")));
  // The mimetype is the first entry, stored, so its text sits at byte 38.
  CHECK(std::string(bytes.begin() + 30, bytes.begin() + 54) == "mimetypeimage/openraster");
  const auto sniffed = patchy::formats::sniff_openable_image_format(bytes);
  CHECK(sniffed.has_value() && sniffed->extension == "ora");
  // The thumbnail fits 256 x 256 and the merged image is the canvas size.
  Document wide(600, 100, PixelFormat::rgba8());
  wide.add_layer(pixel_layer(wide, "Wide", Rect{0, 0, 600, 100}, 7, true));
  const auto wide_bytes = ora::DocumentIo::write(wide);
  const auto find = [&](std::string_view name) -> std::optional<std::pair<int, int>> {
    // Walk the local headers (all stored except stack.xml).
    std::size_t at = 0;
    while (at + 30 <= wide_bytes.size() && wide_bytes[at] == 'P' && wide_bytes[at + 1] == 'K' &&
           wide_bytes[at + 2] == 3) {
      const auto size = static_cast<std::size_t>(wide_bytes[at + 18] | (wide_bytes[at + 19] << 8) |
                                                 (wide_bytes[at + 20] << 16) | (wide_bytes[at + 21] << 24));
      const auto name_length = static_cast<std::size_t>(wide_bytes[at + 26] | (wide_bytes[at + 27] << 8));
      const auto extra_length = static_cast<std::size_t>(wide_bytes[at + 28] | (wide_bytes[at + 29] << 8));
      const std::string entry(wide_bytes.begin() + static_cast<std::ptrdiff_t>(at + 30),
                              wide_bytes.begin() + static_cast<std::ptrdiff_t>(at + 30 + name_length));
      const auto data = at + 30 + name_length + extra_length;
      if (entry == name) {
        int width = 0;
        int height = 0;
        int components = 0;
        if (stbi_info_from_memory(wide_bytes.data() + data, static_cast<int>(size), &width, &height, &components) == 0) {
          return std::nullopt;
        }
        return std::make_pair(width, height);
      }
      at = data + size;
    }
    return std::nullopt;
  };
  CHECK(find("Thumbnails/thumbnail.png") == std::make_optional(std::make_pair(256, 43)));
  CHECK(find("mergedimage.png") == std::make_optional(std::make_pair(600, 100)));
}

// Every PNG the writer makes carries the sRGB ICC profile in an iCCP chunk right after
// IHDR: Krita asks for a profile per layer (and its command-line export hangs) on 16-bit
// PNGs without one (docs/ora.md, "Colour profile").
void ora_pngs_carry_an_srgb_profile() {
  const auto& profile = ora::detail::srgb_icc_profile();
  CHECK(profile.size() >= 128U && profile.size() % 4U == 0U);
  const auto be32 = [](const std::uint8_t* at) {
    return (static_cast<std::uint32_t>(at[0]) << 24) | (static_cast<std::uint32_t>(at[1]) << 16) |
           (static_cast<std::uint32_t>(at[2]) << 8) | at[3];
  };
  CHECK(be32(profile.data()) == profile.size());
  CHECK(std::string(profile.begin() + 36, profile.begin() + 40) == "acsp");
  CHECK(std::string(profile.begin() + 12, profile.begin() + 24) == "mntrRGB XYZ ");
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16}) {
    const auto png = ora::detail::encode_png(pattern(5, 3, 1, depth));
    CHECK(std::string(png.begin() + 37, png.begin() + 41) == "iCCP");
    const auto length = be32(png.data() + 33);
    CHECK(std::string(png.begin() + 41, png.begin() + 47) == std::string("sRGB\0\0", 6));
    std::vector<std::uint8_t> unpacked(profile.size() + 16);
    mz_ulong unpacked_size = static_cast<mz_ulong>(unpacked.size());
    CHECK(mz_uncompress(unpacked.data(), &unpacked_size, png.data() + 47, static_cast<mz_ulong>(length - 6)) == MZ_OK);
    unpacked.resize(unpacked_size);
    CHECK(unpacked == profile);
    // The CRC covers the type and the data.
    const auto crc = mz_crc32(MZ_CRC32_INIT, png.data() + 37, length + 4);
    CHECK(be32(png.data() + 41 + length) == crc);
    // And Patchy's reader still decodes it, at its depth.
    const auto read = ora::DocumentIo::read(archive(R"(<image w="5" h="3"><stack><layer src="data/a.png"/></stack></image>)",
                                                    {{"data/a.png", png, false}}));
    CHECK(read.layers().front().pixels().width() == 5);
  }
}

void ora_unicode_path_round_trips() {
  // Every new file-writing entry point gets a Unicode-path test (AGENTS.md).
  const auto dir = patchy::test::unicode_artifact_dir(u8"ora");
  const auto path = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem).concat(".ora");
  const auto document = structure_document(BitDepth::UInt8);
  ora::DocumentIo::write_file(document, path);
  CHECK(std::filesystem::exists(path));
  CHECK(patchy::test::directory_holds_only(dir, {path}));
  const auto read = ora::DocumentIo::read_file(path);
  check_same_tree(document.layers(), read.layers());
}

void ora_reader_refuses_hostile_and_broken_files() {
  const std::string_view minimal = R"(<image w="8" h="6"><stack><layer src="data/a.png" name="A"/></stack></image>)";
  const auto good = archive(minimal, {png_entry("data/a.png", 8, 6, 1)});
  {
    // No mergedimage.png and no thumbnail: still opens.
    std::vector<std::string> notices;
    const auto read = ora::DocumentIo::read(good, &notices);
    CHECK(read.layers().size() == 1U && read.layers().front().pixels().width() == 8);
    CHECK(notices.empty());
  }
  CHECK(read_throws({}));
  CHECK(read_throws(bytes_of("not a zip at all")));
  CHECK(read_throws(archive(minimal, {}, "application/x-krita")));  // another app's zip
  CHECK(read_throws(archive("", {png_entry("data/a.png", 8, 6, 1)})));  // no stack.xml
  CHECK(read_throws(archive(R"(<image w="8" h="6"><stack><layer)")));  // bad XML
  CHECK(read_throws(archive(R"(<svg w="8" h="6"/>)")));
  CHECK(read_throws(archive(R"(<image w="0" h="6"><stack/></image>)")));
  CHECK(read_throws(archive(R"(<image w="400000" h="6"><stack/></image>)")));
  {
    // A PNG header that claims 60000 x 60000 pixels is refused before decoding.
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                     0, 0, 0xEA, 0x60, 0, 0, 0xEA, 0x60, 8, 6, 0, 0, 0, 0, 0, 0, 0};
    CHECK(read_throws(archive(minimal, {{"data/a.png", png, false}})));
  }
  {
    // An entry that claims a far larger unpacked size than its data can hold.
    auto bomb = good;
    constexpr std::array<std::uint8_t, 4> kCentral = {'P', 'K', 1, 2};
    const auto central = std::search(bomb.begin(), bomb.end(), kCentral.begin(), kCentral.end());
    // The second central record is stack.xml (deflated): patch its uncompressed size.
    const auto second = std::search(central + 4, bomb.end(), kCentral.begin(), kCentral.end());
    CHECK(second != bomb.end());
    if (second != bomb.end()) {
      const std::array<std::uint8_t, 4> huge = {0x00, 0x00, 0x00, 0x40};  // 1 GiB
      std::copy(huge.begin(), huge.end(), second + 24);
      CHECK(read_throws(bomb));
    }
  }
  {
    // Every prefix of a real file either opens or throws; none crashes.
    const auto full = ora::DocumentIo::write(structure_document(BitDepth::UInt8));
    for (std::size_t length = 0; length < full.size(); length += std::max<std::size_t>(1, full.size() / 97)) {
      (void)read_throws(std::vector<std::uint8_t>(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length)));
    }
    for (std::size_t at = 0; at < full.size(); at += std::max<std::size_t>(1, full.size() / 211)) {
      auto mutated = full;
      mutated[at] = static_cast<std::uint8_t>(mutated[at] ^ 0x5A);
      (void)read_throws(mutated);
    }
  }
}

void ora_reader_opens_damaged_layers_empty_with_notices() {
  std::vector<ora::detail::ZipEntry> extra = {png_entry("data/a.png", 4, 4, 1), png_entry("../a.png", 4, 4, 2),
                                              png_entry("/abs.png", 4, 4, 3), {"data/bad.png", bytes_of("garbage"), false}};
  const auto bytes = archive(
      R"(<image w="8" h="6"><stack>)"
      R"(<layer src="data/missing.png" name="Missing"/>)"
      R"(<layer src="../a.png" name="Parent"/>)"
      R"(<layer src="/abs.png" name="Absolute"/>)"
      R"(<layer src="C:/a.png" name="Drive"/>)"
      R"(<layer src="data\a.png" name="Backslash"/>)"
      R"(<layer src="data/bad.png" name="Garbage"/>)"
      R"(<text>hello</text>)"
      R"(<layer src="data/a.png" name="Fine" composite-op="foo:bar"/>)"
      R"(</stack></image>)",
      std::move(extra));
  std::vector<std::string> notices;
  const auto read = ora::DocumentIo::read(bytes, &notices);
  CHECK(read.layers().size() == 7U);
  for (const auto* name : {"Missing", "Parent", "Absolute", "Drive", "Backslash", "Garbage"}) {
    const auto* layer = find_named(read.layers(), name);
    CHECK(layer != nullptr && layer->pixels().width() == 0);
  }
  const auto* fine = find_named(read.layers(), "Fine");
  CHECK(fine != nullptr && fine->pixels().width() == 4 && fine->blend_mode() == BlendMode::Normal);
  CHECK(notices_mention(notices, "Missing (data/missing.png)"));
  CHECK(notices_mention(notices, "point outside the file opened empty: Parent, Absolute, Drive, Backslash"));
  CHECK(notices_mention(notices, "Garbage"));
  CHECK(notices_mention(notices, "Fine (foo:bar)"));
  CHECK(notices_mention(notices, "Skipped 1 elements"));
}

void ora_reader_maps_groups_isolation_and_operators() {
  const auto bytes = archive(
      R"(<image w="10" h="10" xres="300" yres="200"><stack>)"
      R"(<layer src="data/a.png" name="Erase" composite-op="svg:dst-out"/>)"
      R"(<layer src="data/a.png" name="Atop over two" composite-op="svg:src-atop"/>)"
      R"(<stack name="Auto" isolation="auto" opacity="0.5" composite-op="svg:multiply">)"
      R"(<layer src="data/a.png" name="Plus" composite-op="svg:plus"/>)"
      R"(</stack>)"
      R"(<stack name="Isolated" composite-op="svg:screen" visibility="hidden" x="3" y="4">)"
      R"(<layer src="data/a.png" name="Clip" composite-op="svg:src-atop" selected="true"/>)"
      R"(<layer src="data/a.png" name="Base" x="1" y="2" composite-op="krita:linear_burn"/>)"
      R"(</stack>)"
      R"(<layer src="data/a.png" name="Bottom"/>)"
      R"(</stack></image>)",
      {png_entry("data/a.png", 4, 4, 1)});
  std::vector<std::string> notices;
  const auto read = ora::DocumentIo::read(bytes, &notices);
  CHECK(read.print_settings().horizontal_ppi == 300.0 && read.print_settings().vertical_ppi == 200.0);
  const auto* group = find_named(read.layers(), "Auto");
  CHECK(group != nullptr && group->blend_mode() == BlendMode::PassThrough && group->opacity() == 0.5F);
  CHECK(find_named(read.layers(), "Plus")->blend_mode() == BlendMode::LinearDodge);
  const auto* isolated = find_named(read.layers(), "Isolated");
  CHECK(isolated != nullptr && isolated->blend_mode() == BlendMode::Screen && !isolated->visible());
  const auto* base = find_named(read.layers(), "Base");
  CHECK(base != nullptr && base->blend_mode() == BlendMode::LinearBurn && base->bounds() == (Rect{4, 6, 4, 4}));
  const auto* clip = find_named(read.layers(), "Clip");
  CHECK(clip != nullptr && clip->clipped() && clip->bounds() == (Rect{3, 4, 4, 4}));
  CHECK(read.active_layer_id() == std::make_optional(clip->id()));
  const auto* atop = find_named(read.layers(), "Atop over two");
  CHECK(atop != nullptr && !atop->clipped());
  CHECK(find_named(read.layers(), "Erase")->blend_mode() == BlendMode::Normal);
  CHECK(notices_mention(notices, "Erase (svg:dst-out)"));
  CHECK(notices_mention(notices, "Atop over two (svg:src-atop)"));
  // Bottom first: Bottom, Isolated, Auto, Atop over two, Erase.
  CHECK(read.layers().size() == 5U && read.layers().front().name() == "Bottom" && read.layers().back().name() == "Erase");
}

// Locks: Lock Image Pixels (alone or in Lock All) is edit-locked on layers and stacks, which
// GIMP 3.2 writes for its content lock and Krita 5.3 reads and writes back; the transparency
// and position locks have no attribute. alpha-preserve is Krita's clipping, not a lock.
Document locks_document() {
  Document document(40, 30, PixelFormat::rgba8());
  document.add_layer(pixel_layer(document, "Background", Rect{0, 0, 40, 30}, 1, true));
  const std::vector<std::pair<const char*, patchy::LayerLockFlags>> layers = {
      {"Pixels locked", patchy::kLayerLockImagePixels},
      {"All locked", patchy::kLayerLockAll},
      {"Transparency locked", patchy::kLayerLockTransparentPixels},
      {"Position locked", patchy::kLayerLockPosition},
  };
  int seed = 2;
  for (const auto& [name, flags] : layers) {
    auto layer = pixel_layer(document, name, Rect{seed * 4, 2, 10, 10}, seed);
    layer.set_lock_flags(flags);
    document.add_layer(std::move(layer));
    ++seed;
  }
  Layer pixels_group(document.allocate_layer_id(), "Group pixels locked", LayerKind::Group);
  pixels_group.add_child(pixel_layer(document, "In pixels group", Rect{2, 14, 10, 10}, 8));
  pixels_group.set_lock_flags(patchy::kLayerLockImagePixels);
  document.add_layer(std::move(pixels_group));
  Layer all_group(document.allocate_layer_id(), "Group all locked", LayerKind::Group);
  all_group.add_child(pixel_layer(document, "In all group", Rect{20, 14, 10, 10}, 9));
  all_group.set_lock_flags(patchy::kLayerLockAll);
  document.add_layer(std::move(all_group));
  return document;
}

void ora_round_trip_keeps_edit_locks() {
  const auto bytes = ora::DocumentIo::write(locks_document());
  const auto read = ora::DocumentIo::read(bytes);
  const auto locks_of = [&](std::string_view name) {
    const auto* layer = find_named(read.layers(), name);
    CHECK(layer != nullptr);
    return layer != nullptr ? layer->lock_flags() : patchy::kLayerLockNone;
  };
  CHECK(locks_of("Pixels locked") == patchy::kLayerLockImagePixels);
  CHECK(locks_of("All locked") == patchy::kLayerLockImagePixels);
  CHECK(locks_of("Transparency locked") == patchy::kLayerLockNone);
  CHECK(locks_of("Position locked") == patchy::kLayerLockNone);
  CHECK(locks_of("Group pixels locked") == patchy::kLayerLockImagePixels);
  CHECK(locks_of("Group all locked") == patchy::kLayerLockImagePixels);
  CHECK(locks_of("In pixels group") == patchy::kLayerLockNone);
  CHECK(locks_of("Background") == patchy::kLayerLockNone);
  // A lock is not content: nothing to warn about.
  CHECK(ora::DocumentIo::lost_content(locks_document()).empty());
  // And the written attribute is the spec's.
  const auto with_attribute = archive(R"(<image w="8" h="6"><stack>)"
                                      R"(<stack name="G" edit-locked="true"><layer src="data/a.png" name="In"/></stack>)"
                                      R"(<layer src="data/a.png" name="One" edit-locked="1"/>)"
                                      R"(<layer src="data/a.png" name="Off" edit-locked="false"/>)"
                                      R"(<layer src="data/a.png" name="Base"/>)"
                                      R"(</stack></image>)",
                                      {png_entry("data/a.png", 4, 4, 1)});
  const auto parsed = ora::DocumentIo::read(with_attribute);
  CHECK(find_named(parsed.layers(), "G")->lock_flags() == patchy::kLayerLockImagePixels);
  CHECK(find_named(parsed.layers(), "One")->lock_flags() == patchy::kLayerLockImagePixels);
  CHECK(find_named(parsed.layers(), "Off")->lock_flags() == patchy::kLayerLockNone);
}

void ora_reader_opens_alpha_preserve_as_clipping() {
  // Krita renders alpha-preserve like source-atop (it even rewrites src-over + alpha-preserve
  // as svg:src-atop), so it opens as a clipped layer keeping its own blend mode.
  const auto bytes = archive(R"(<image w="8" h="6"><stack>)"
                             R"(<layer src="data/a.png" name="Kept" composite-op="svg:multiply" alpha-preserve="true"/>)"
                             R"(<layer src="data/a.png" name="Base" x="2" y="1"/>)"
                             R"(</stack></image>)",
                             {png_entry("data/a.png", 4, 4, 1)});
  const auto read = ora::DocumentIo::read(bytes);
  const auto* kept = find_named(read.layers(), "Kept");
  CHECK(kept != nullptr && kept->clipped() && kept->blend_mode() == BlendMode::Multiply);
  CHECK(kept != nullptr && kept->lock_flags() == patchy::kLayerLockNone);
}

void ora_reader_falls_back_to_the_merged_image() {
  const auto bytes = archive(R"(<image w="5" h="4"><stack></stack></image>)", {png_entry("mergedimage.png", 5, 4, 3)});
  const auto read = ora::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1U && read.layers().front().name() == "Background");
  CHECK(read.layers().front().pixels().width() == 5);
  const auto blank = ora::DocumentIo::read(archive(R"(<image w="5" h="4"><stack/></image>)"));
  CHECK(blank.layers().size() == 1U && blank.layers().front().pixels().width() == 5);
}

void ora_zip64_archives_read_back() {
  // A small file with every Zip64 record forced on reads like any other.
  std::vector<ora::detail::ZipEntry> entries = {
      {"mimetype", bytes_of("image/openraster"), false},
      {"stack.xml", bytes_of(R"(<image w="8" h="6"><stack><layer src="data/a.png" name="A"/></stack></image>)"), true},
      png_entry("data/a.png", 8, 6, 4)};
  const auto zip64 = ora::detail::zip_archive(entries, /*force_zip64*/ true);
  CHECK(ora::sniff(zip64));  // past the mimetype header's Zip64 extra field
  const auto read = ora::DocumentIo::read(zip64);
  CHECK(read.layers().size() == 1U && read.layers().front().pixels().width() == 8);
}

void ora_lost_content_lists_what_a_save_bakes() {
  Document document(48, 40, PixelFormat::rgba8());
  document.add_layer(pixel_layer(document, "Background", Rect{0, 0, 48, 40}, 1, true));
  auto text = pixel_layer(document, "Headline", Rect{2, 2, 20, 8}, 2);
  text.metadata()[patchy::kLayerMetadataText] = "Headline";
  document.add_layer(std::move(text));
  auto shadowed = pixel_layer(document, "Shadowed", Rect{20, 4, 14, 12}, 3);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadowed.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(shadowed));
  auto masked = pixel_layer(document, "Masked", Rect{4, 20, 16, 16}, 4);
  patchy::LayerMask mask;
  mask.bounds = Rect{4, 20, 8, 16};
  mask.pixels = PixelBuffer(8, 16, PixelFormat::gray8());
  mask.pixels.clear(255);
  mask.default_color = 0;
  masked.set_mask(std::move(mask));
  document.add_layer(std::move(masked));
  document.add_layer(pixel_layer(document, "Clip base", Rect{24, 20, 16, 16}, 5));
  auto clipped = pixel_layer(document, "Clipped", Rect{20, 18, 24, 20}, 6);
  clipped.set_clipped(true);
  document.add_layer(std::move(clipped));
  auto faded = pixel_layer(document, "Fill faded", Rect{0, 30, 48, 10}, 7);
  faded.set_fill_opacity(0.5F);
  document.add_layer(std::move(faded));
  auto exclusion = pixel_layer(document, "Exclusion", Rect{30, 0, 18, 18}, 8);
  exclusion.set_blend_mode(BlendMode::Exclusion);
  document.add_layer(std::move(exclusion));
  Layer adjusted(document.allocate_layer_id(), "Adjusted", LayerKind::Group);
  adjusted.add_child(pixel_layer(document, "Under", Rect{10, 10, 20, 20}, 9));
  Layer invert(document.allocate_layer_id(), "Invert", LayerKind::Adjustment);
  invert.set_bounds(Rect::from_size(48, 40));
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Invert;
  patchy::configure_adjustment_layer(invert, settings);
  adjusted.add_child(std::move(invert));
  document.add_layer(std::move(adjusted));
  document.guides().push_back(patchy::DocumentGuide{});

  const auto losses = ora::DocumentIo::lost_content(document);
  const auto has = [&](ora::LossKind kind, std::string_view name) {
    return std::any_of(losses.begin(), losses.end(),
                       [&](const ora::Loss& loss) { return loss.kind == kind && loss.layer_name == name; });
  };
  CHECK(has(ora::LossKind::TextLayer, "Headline"));
  CHECK(has(ora::LossKind::LayerStyle, "Shadowed"));
  CHECK(has(ora::LossKind::LayerMask, "Masked"));
  CHECK(has(ora::LossKind::ClippingGroup, "Clip base"));
  CHECK(has(ora::LossKind::BlendingOptions, "Fill faded"));
  CHECK(has(ora::LossKind::BlendModeKrita, "Exclusion"));
  CHECK(has(ora::LossKind::AdjustmentLayer, "Invert"));
  CHECK(has(ora::LossKind::Guides, ""));
  CHECK(losses.size() == 8U);

  std::vector<std::string> notices;
  const auto read = ora::DocumentIo::read(ora::DocumentIo::write(document, &notices));
  CHECK(notices.size() == losses.size());
  // Baked layers keep their names and places in the stack; the group's contents merged.
  const auto* masked_read = find_named(read.layers(), "Masked");
  CHECK(masked_read != nullptr && !masked_read->mask().has_value() && masked_read->bounds().width <= 8);
  CHECK(find_named(read.layers(), "Clipped") == nullptr && find_named(read.layers(), "Clip base") != nullptr);
  const auto* group = find_named(read.layers(), "Adjusted");
  CHECK(group != nullptr && group->children().size() == 1U && group->children().front().name() == "Merged");
  // The picture survives the baking.
  const auto expected = patchy::flatten_document_rgba8(document);
  const auto actual = patchy::flatten_document_rgba8(read);
  int worst = 0;
  for (std::size_t i = 0; i < expected.data().size(); ++i) {
    worst = std::max(worst, std::abs(static_cast<int>(expected.data()[i]) - static_cast<int>(actual.data()[i])));
  }
  if (worst > 2) {
    std::cout << "  baked ora composite differs by up to " << worst << "\n";
  }
  CHECK(worst <= 2);
}

// The files GIMP 3.2 and Krita 5 wrote (scripts/dev/ora/make_ora_fixtures.py): their layer
// trees open as the apps saved them, and Patchy's composite of Krita's files matches the
// merged image Krita stored.
void ora_reads_gimp_and_krita_files_if_available() {
  const auto fixture = [](std::string_view name) { return patchy::test::local_format_fixture_path("ora", name); };
  patchy::test::skip_unless_exists(fixture("krita-structure.ora"));
  for (const auto* name : {"gimp-structure.ora", "krita-structure.ora", "krita-structure-from-psd.ora"}) {
    std::vector<std::string> notices;
    const auto read = ora::DocumentIo::read_file(fixture(name), &notices);
    CHECK(read.width() == 160 && read.height() == 120);
    CHECK(notices.empty());
    const auto* unicode = find_named(read.layers(), unicode_name());
    CHECK(unicode != nullptr && unicode->bounds().x == -10 && unicode->bounds().y == 80);
    const auto* hidden = find_named(read.layers(), "Hidden layer");
    CHECK(hidden != nullptr && !hidden->visible() && hidden->bounds().x == 100 && hidden->bounds().y == 60);
    const auto* multiply = find_named(read.layers(), "Offset multiply");
    CHECK(multiply != nullptr && multiply->blend_mode() == BlendMode::Multiply && multiply->bounds().x == 20);
    const auto* inner = find_named(read.layers(), "Inner pass-through");
    CHECK(inner != nullptr && inner->kind() == LayerKind::Group);
    // Only Krita's PSD import kept the pass-through group (isolation="auto").
    CHECK(inner == nullptr || (inner->blend_mode() == BlendMode::PassThrough) == (std::string_view(name) == "krita-structure-from-psd.ora"));
  }
  {
    const auto read = ora::DocumentIo::read_file(fixture("krita-modes-from-psd.ora"));
    const std::vector<std::pair<const char*, BlendMode>> expected = {
        {"linear_burn", BlendMode::LinearBurn}, {"addition", BlendMode::LinearDodge}, {"vivid_light", BlendMode::VividLight},
        {"linear_light", BlendMode::LinearLight}, {"pin_light", BlendMode::PinLight}, {"hard_mix", BlendMode::HardMix},
        {"subtract", BlendMode::Subtract}, {"divide", BlendMode::Divide}, {"exclusion", BlendMode::Exclusion},
        {"dissolve", BlendMode::Dissolve}, {"softlight", BlendMode::SoftLight}, {"burn", BlendMode::ColorBurn}};
    for (const auto& [layer_name, mode] : expected) {
      const auto* layer = find_named(read.layers(), layer_name);
      CHECK(layer != nullptr && layer->blend_mode() == mode);
    }
  }
  // The lock files: GIMP's content lock (and its Lock All, which it also writes as
  // edit-locked) and Krita's re-save of GIMP's file.
  for (const auto* name : {"gimp-locks.ora", "krita-locks.ora"}) {
    if (!std::filesystem::exists(fixture(name))) {
      continue;
    }
    const auto read = ora::DocumentIo::read_file(fixture(name));
    for (const auto* locked : {"Lock content", "Lock all", "Group content locked"}) {
      const auto* layer = find_named(read.layers(), locked);
      CHECK(layer != nullptr && layer->lock_flags() == patchy::kLayerLockImagePixels);
    }
    for (const auto* open : {"Plain", "Lock alpha", "Lock position", "Group position locked", "In content group"}) {
      const auto* layer = find_named(read.layers(), open);
      CHECK(layer != nullptr && layer->lock_flags() == patchy::kLayerLockNone);
    }
  }
  // Krita's own merged image against Patchy's composite of the same layers.
  for (const auto* name : {"krita-structure.ora", "krita-structure-from-psd.ora"}) {
    const auto bytes = patchy::test::read_binary_file(fixture(name));
    const auto read = ora::DocumentIo::read(bytes);
    const auto ours = patchy::flatten_document_rgba8(read);
    const auto stem = std::string(name).substr(0, std::string(name).size() - 4);
    const auto png = patchy::test::read_binary_file(fixture(stem + ".png"));
    int width = 0;
    int height = 0;
    int components = 0;
    stbi_uc* theirs = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &components, 4);
    CHECK(theirs != nullptr && width == ours.width() && height == ours.height());
    if (theirs == nullptr || width != ours.width() || height != ours.height()) {
      stbi_image_free(theirs);
      continue;
    }
    double total = 0.0;
    int worst = 0;
    const auto data = ours.data();
    for (std::size_t i = 0; i < data.size(); ++i) {
      // Compare premultiplied, so the color of fully transparent pixels does not count.
      const auto pixel = i / 4U * 4U;
      const int a = data[i] * (i % 4 == 3 ? 255 : data[pixel + 3]) / 255;
      const int b = theirs[i] * (i % 4 == 3 ? 255 : theirs[pixel + 3]) / 255;
      total += std::abs(a - b);
      worst = std::max(worst, std::abs(a - b));
    }
    stbi_image_free(theirs);
    const double mean = total / static_cast<double>(data.size());
    std::cout << "  " << name << " against Krita's export: mean " << mean << ", worst " << worst << "\n";
    CHECK(mean < 0.5 && worst <= 3);
  }
}

const char* blend_mode_label(BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough: return "pass-through";
    case BlendMode::Normal: return "normal";
    case BlendMode::Multiply: return "multiply";
    case BlendMode::Screen: return "screen";
    case BlendMode::Overlay: return "overlay";
    case BlendMode::Darken: return "darken";
    case BlendMode::Lighten: return "lighten";
    case BlendMode::ColorDodge: return "color-dodge";
    case BlendMode::ColorBurn: return "color-burn";
    case BlendMode::HardLight: return "hard-light";
    case BlendMode::SoftLight: return "soft-light";
    case BlendMode::Difference: return "difference";
    case BlendMode::LinearBurn: return "linear-burn";
    case BlendMode::PinLight: return "pin-light";
    case BlendMode::Saturation: return "saturation";
    case BlendMode::Luminosity: return "luminosity";
    case BlendMode::Exclusion: return "exclusion";
    case BlendMode::Hue: return "hue";
    case BlendMode::Color: return "color";
    case BlendMode::LinearDodge: return "linear-dodge";
    case BlendMode::Subtract: return "subtract";
    case BlendMode::Divide: return "divide";
    case BlendMode::VividLight: return "vivid-light";
    case BlendMode::LinearLight: return "linear-light";
    case BlendMode::HardMix: return "hard-mix";
    case BlendMode::DarkerColor: return "darker-color";
    case BlendMode::LighterColor: return "lighter-color";
    case BlendMode::Dissolve: return "dissolve";
    case BlendMode::Reflect: return "reflect";
    case BlendMode::Glow: return "glow";
    case BlendMode::Negation: return "negation";
    case BlendMode::Xor: return "xor";
  }
  return "unknown";
}

// The input of scripts/dev/ora/check_real_apps.py (docs/ora.md, "Interchange"): Patchy's own
// files under test-artifacts/ora/ (one tile per blend mode over a backdrop, the structure
// document at 8 and 16 bits) with a modes.txt that says where each tile is, plus Patchy's
// composite of every app-made fixture under test-artifacts/ora/from-apps/ for comparison with
// the apps' own exports.
void ora_writes_inspection_artifacts() {
  const auto dir = std::filesystem::path("test-artifacts") / "ora";
  std::filesystem::create_directories(dir / "from-apps");
  constexpr int kTile = 32;
  constexpr int kColumns = 7;
  const auto modes = written_blend_modes();
  const int rows = static_cast<int>((modes.size() + kColumns - 1) / kColumns);
  Document document(kTile * kColumns, kTile * rows, PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  {
    PixelBuffer backdrop(document.width(), document.height(), PixelFormat::rgba8());
    for (std::int32_t y = 0; y < backdrop.height(); ++y) {
      for (std::int32_t x = 0; x < backdrop.width(); ++x) {
        auto* px = backdrop.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(x * 255 / (backdrop.width() - 1));
        px[1] = static_cast<std::uint8_t>(64 + (y % kTile) * 4);
        px[2] = static_cast<std::uint8_t>(255 - x * 255 / (backdrop.width() - 1));
        px[3] = 255;
      }
    }
    document.add_pixel_layer("Backdrop", std::move(backdrop));
  }
  std::string listing;
  for (std::size_t index = 0; index < modes.size(); ++index) {
    PixelBuffer tile(kTile, kTile, PixelFormat::rgba8());
    for (std::int32_t y = 0; y < kTile; ++y) {
      for (std::int32_t x = 0; x < kTile; ++x) {
        auto* px = tile.pixel(x, y);
        const int edge = std::min({x, y, kTile - 1 - x, kTile - 1 - y});
        px[0] = static_cast<std::uint8_t>(230 - y * 6);
        px[1] = static_cast<std::uint8_t>(40 + x * 6);
        px[2] = static_cast<std::uint8_t>(100 + (x + y) * 2);
        px[3] = static_cast<std::uint8_t>(std::min(255, 90 + edge * 30));
      }
    }
    const int x = static_cast<int>(index % kColumns) * kTile;
    const int y = static_cast<int>(index / kColumns) * kTile;
    Layer layer(document.allocate_layer_id(), blend_mode_label(modes[index]), std::move(tile));
    layer.set_bounds(Rect{x, y, kTile, kTile});
    layer.set_blend_mode(modes[index]);
    document.add_layer(std::move(layer));
    listing += std::string(blend_mode_label(modes[index])) + " " + std::to_string(x) + " " + std::to_string(y) + " " +
               std::to_string(kTile) + " " + std::to_string(kTile) + "\n";
  }
  ora::DocumentIo::write_file(document, dir / "patchy-modes.ora");
  {
    std::ofstream file(dir / "modes.txt", std::ios::binary);
    file << listing;
  }
  // The blend modes are checked tile by tile in the modes file; the structure files leave
  // them out so a whole-file comparison measures the tree.
  ora::DocumentIo::write_file(structure_document(BitDepth::UInt8, false), dir / "patchy-structure.ora");
  ora::DocumentIo::write_file(locks_document(), dir / "patchy-locks.ora");
  {
    const DeepEditingScope deep(true);
    ora::DocumentIo::write_file(structure_document(BitDepth::UInt16, false), dir / "patchy-structure-16.ora");
  }
  CHECK(std::filesystem::exists(dir / "patchy-modes.ora"));
  {
    // A layered PSD for trying Save As .ora by hand: a group, an Exclusion layer, a drop
    // shadow and a mask, so the save warning has something to name.
    auto layered = structure_document(BitDepth::UInt8, false);
    auto shadowed = pixel_layer(layered, "Drop shadow", Rect{10, 8, 30, 24}, 21, true);
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.distance = 6.0F;
    shadow.size = 4.0F;
    shadowed.layer_style().drop_shadows.push_back(shadow);
    layered.add_layer(std::move(shadowed));
    auto exclusion = pixel_layer(layered, "Exclusion", Rect{50, 20, 30, 30}, 22, true);
    exclusion.set_blend_mode(BlendMode::Exclusion);
    layered.add_layer(std::move(exclusion));
    patchy::psd::DocumentIo::write_layered_rgb8_file(layered, dir / "layered-for-ora.psd", {});
  }

  const auto fixtures = patchy::test::local_format_fixture_path("ora", "");
  if (!std::filesystem::is_directory(fixtures)) {
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator(fixtures)) {
    if (entry.path().extension() != ".ora") {
      continue;
    }
    const auto read = ora::DocumentIo::read_file(entry.path());
    const auto flat = read.color_state().bit_depth == BitDepth::UInt8
                          ? patchy::flatten_document_rgba8(read)
                          : patchy::Compositor{}.flatten_rgba_deep(read);
    const auto png = ora::detail::encode_png(flat);
    std::ofstream file(dir / "from-apps" / entry.path().filename().replace_extension(".png"), std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> ora_tests() {
  return {
      {"ora_round_trip_keeps_layers_and_pixels", ora_round_trip_keeps_layers_and_pixels},
      {"ora_round_trip_keeps_16_bit_samples", ora_round_trip_keeps_16_bit_samples},
      {"ora_rewrite_is_byte_identical", ora_rewrite_is_byte_identical},
      {"ora_writer_bytes_are_stable", ora_writer_bytes_are_stable},
      {"ora_writes_the_spec_layout", ora_writes_the_spec_layout},
      {"ora_pngs_carry_an_srgb_profile", ora_pngs_carry_an_srgb_profile},
      {"ora_unicode_path_round_trips", ora_unicode_path_round_trips},
      {"ora_reader_refuses_hostile_and_broken_files", ora_reader_refuses_hostile_and_broken_files},
      {"ora_reader_opens_damaged_layers_empty_with_notices", ora_reader_opens_damaged_layers_empty_with_notices},
      {"ora_reader_maps_groups_isolation_and_operators", ora_reader_maps_groups_isolation_and_operators},
      {"ora_round_trip_keeps_edit_locks", ora_round_trip_keeps_edit_locks},
      {"ora_reader_opens_alpha_preserve_as_clipping", ora_reader_opens_alpha_preserve_as_clipping},
      {"ora_reader_falls_back_to_the_merged_image", ora_reader_falls_back_to_the_merged_image},
      {"ora_zip64_archives_read_back", ora_zip64_archives_read_back},
      {"ora_lost_content_lists_what_a_save_bakes", ora_lost_content_lists_what_a_save_bakes},
      {"ora_reads_gimp_and_krita_files_if_available", ora_reads_gimp_and_krita_files_if_available},
      {"ora_writes_inspection_artifacts", ora_writes_inspection_artifacts},
  };
}
