// 16/32-bit PSD/PSB reading and writing at the file's depth (docs/high-bit-depth.md,
// Phase 2): the reader keeps the samples, the writer emits them back, and the 8-bit
// reading of the same file is exactly the deep reading narrowed.

#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_depth.hpp"
#include "local_psd_fixtures.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace patchy;

bool same_bytes(const PixelBuffer& a, const PixelBuffer& b) {
  return a.format() == b.format() && a.width() == b.width() && a.height() == b.height() &&
         std::memcmp(a.data().data(), b.data().data(), a.byte_size()) == 0;
}

std::vector<const Layer*> flat_layers(const Document& document) {
  std::vector<const Layer*> out;
  const std::function<void(const std::vector<Layer>&)> walk = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      out.push_back(&layer);
      walk(layer.children());
    }
  };
  walk(document.layers());
  return out;
}

// A 16 or 32-bit document whose samples are not representable at 8 bits: a fine
// gradient layer with a gradient mask over a background, plus a saved channel.
Document make_deep_document(BitDepth depth) {
  Document document(64, 32, PixelFormat::rgb8());
  PixelBuffer background(64, 32, PixelFormat::rgba8());
  PixelBuffer top(64, 32, PixelFormat::rgba8());
  document.add_pixel_layer("Background", background);
  document.add_pixel_layer("Top", top);
  convert_document_depth(document, depth);
  const auto domain = deep_domain_for(depth);
  std::vector<float> row(64 * 4);
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      auto* p = &row[static_cast<std::size_t>(x) * 4U];
      p[0] = 3.3F + x * 3.9F;
      p[1] = 250.0F - y * 7.1F;
      p[2] = 17.25F + (x + y) * 1.3F;
      p[3] = 255.0F;
    }
    store_rgba_row(document.layers()[0].pixels(), y, 0, 64, domain, row);
    for (int x = 0; x < 64; ++x) {
      row[static_cast<std::size_t>(x) * 4U + 3U] = 40.5F + x * 2.7F;
    }
    store_rgba_row(document.layers()[1].pixels(), y, 0, 64, domain, row);
  }
  LayerMask mask;
  mask.bounds = Rect::from_size(64, 32);
  mask.pixels = PixelBuffer(64, 32, with_bit_depth(PixelFormat::gray8(), depth));
  std::vector<float> coverage(64);
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      coverage[static_cast<std::size_t>(x)] = x * 3.99F + 0.37F;
    }
    store_coverage_row(mask.pixels, y, 0, 64, coverage);
  }
  document.layers()[1].set_mask(mask);
  PixelBuffer saved(64, 32, with_bit_depth(PixelFormat::gray8(), depth));
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      coverage[static_cast<std::size_t>(x)] = 255.0F - x * 1.11F - y;
    }
    store_coverage_row(saved, y, 0, 64, coverage);
  }
  document.add_channel(DocumentChannel(document.allocate_channel_id(), "Alpha 1", DocumentChannelKind::Alpha, saved));
  return document;
}

std::uint16_t header_depth(const std::vector<std::uint8_t>& bytes) {
  return static_cast<std::uint16_t>((bytes[22] << 8) | bytes[23]);
}

void deep_round_trip(BitDepth depth, bool large_document) {
  const auto original = make_deep_document(depth);
  CHECK(document_depth_problems(original).empty());
  const auto bytes = psd::DocumentIo::write_layered_rgb8(original, psd::WriteOptions{large_document});
  CHECK(header_depth(bytes) == (depth == BitDepth::Float32 ? 32 : 16));
  psd::ReadOptions options;
  options.keep_bit_depth = true;
  const auto read = psd::DocumentIo::read(bytes, options);
  CHECK(document_bit_depth(read) == depth);
  CHECK(document_depth_problems(read).empty());
  CHECK(read.layers().size() == 2);
  for (std::size_t i = 0; i < 2; ++i) {
    CHECK(same_bytes(read.layers()[i].pixels(), original.layers()[i].pixels()));
  }
  CHECK(read.layers()[1].mask().has_value());
  CHECK(same_bytes(read.layers()[1].mask()->pixels, original.layers()[1].mask()->pixels));
  CHECK(read.channels().size() == 1);
  CHECK(same_bytes(read.channels()[0].pixels(), original.channels()[0].pixels()));

  // Read the same bytes the old way: 8 bits, and exactly the deep pixels narrowed.
  psd::ReadOptions shallow_options;
  shallow_options.keep_bit_depth = false;
  const auto shallow = psd::DocumentIo::read(bytes, shallow_options);
  CHECK(document_bit_depth(shallow) == BitDepth::UInt8);
  for (std::size_t i = 0; i < 2; ++i) {
    CHECK(same_bytes(shallow.layers()[i].pixels(),
                     convert_pixel_buffer_depth(read.layers()[i].pixels(), BitDepth::UInt8, SampleKind::Color)));
  }
}

void psd_deep_16_bit_document_round_trips_exactly() {
  deep_round_trip(BitDepth::UInt16, false);
  deep_round_trip(BitDepth::UInt16, true);
}

void psd_deep_32_bit_document_round_trips_exactly() {
  deep_round_trip(BitDepth::Float32, false);
  deep_round_trip(BitDepth::Float32, true);
}

void psd_deep_gate_off_reads_8_bit_as_before() {
  const auto bytes = psd::DocumentIo::write_layered_rgb8(make_deep_document(BitDepth::UInt16));
  set_deep_editing_override(false);
  const auto document = psd::DocumentIo::read(bytes);
  set_deep_editing_override(std::nullopt);
  CHECK(document_bit_depth(document) == BitDepth::UInt8);
  CHECK(document.layers()[0].pixels().format().bit_depth == BitDepth::UInt8);
  set_deep_editing_override(true);
  const auto deep = psd::DocumentIo::read(bytes);
  set_deep_editing_override(std::nullopt);
  CHECK(document_bit_depth(deep) == BitDepth::UInt16);
}

// Photoshop-made files from scripts/dev/deep/make_deep_fixtures.py: every scene and
// depth the corpus has. The deep reading narrows to exactly the 8-bit reading (which
// the existing suites pin against Photoshop), keeps samples 8 bits cannot hold, and
// survives Patchy's own deep write.
void psd_deep_photoshop_corpus_reads_and_round_trips_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "deep";
  if (!std::filesystem::exists(root / "manifest.json")) {
    std::cout << "[SKIP] deep fixture corpus missing: " << root.string() << '\n';
    return;
  }
  int checked = 0;
  for (const auto& scene : std::filesystem::directory_iterator(root)) {
    if (!scene.is_directory()) {
      continue;
    }
    for (const auto* depth_name : {"16", "32"}) {
      const auto path = scene.path() / depth_name / (scene.path().filename().string() + "-" + depth_name + ".psd");
      if (!std::filesystem::exists(path)) {
        continue;
      }
      const auto depth = std::string(depth_name) == "32" ? BitDepth::Float32 : BitDepth::UInt16;
      psd::ReadOptions deep_options;
      deep_options.keep_bit_depth = true;
      const auto deep = psd::DocumentIo::read_file(path, deep_options);
      psd::ReadOptions shallow_options;
      shallow_options.keep_bit_depth = false;
      const auto shallow = psd::DocumentIo::read_file(path, shallow_options);
      CHECK(document_bit_depth(deep) == depth);
      if (!document_depth_problems(deep).empty()) {
        std::cerr << path.string() << ": " << document_depth_problems(deep).front() << '\n';
        CHECK(false);
      }
      const auto deep_layers = flat_layers(deep);
      const auto shallow_layers = flat_layers(shallow);
      CHECK(deep_layers.size() == shallow_layers.size());
      for (std::size_t i = 0; i < deep_layers.size(); ++i) {
        const auto& deep_pixels = deep_layers[i]->pixels();
        const auto& shallow_pixels = shallow_layers[i]->pixels();
        if (deep_pixels.empty() || shallow_pixels.format().bit_depth != BitDepth::UInt8) {
          continue;
        }
        const auto narrowed = convert_pixel_buffer_depth(deep_pixels, BitDepth::UInt8, SampleKind::Color);
        if (!same_bytes(narrowed, shallow_pixels)) {
          std::cerr << path.string() << ": layer '" << deep_layers[i]->name() << "' narrows differently\n";
          CHECK(false);
        }
      }
      const auto bytes = psd::DocumentIo::write_layered_rgb8(deep);
      CHECK(header_depth(bytes) == (depth == BitDepth::Float32 ? 32 : 16));
      const auto reread = psd::DocumentIo::read(bytes, deep_options);
      const auto reread_layers = flat_layers(reread);
      CHECK(reread_layers.size() == deep_layers.size());
      for (std::size_t i = 0; i < deep_layers.size() && i < reread_layers.size(); ++i) {
        if (deep_layers[i]->kind() == LayerKind::Pixel && !deep_layers[i]->pixels().empty() &&
            deep_layers[i]->vector_shape() == nullptr &&
            !same_bytes(reread_layers[i]->pixels(), deep_layers[i]->pixels())) {
          std::cerr << path.string() << ": layer '" << deep_layers[i]->name() << "' changed on resave\n";
          CHECK(false);
        }
      }
      ++checked;
    }
  }
  std::cout << "  deep corpus documents checked: " << checked << '\n';
  CHECK(checked > 0);
}

}  // namespace

std::vector<patchy::test::TestCase> psd_deep_io_tests() {
  return {
      {"psd_deep_16_bit_document_round_trips_exactly", psd_deep_16_bit_document_round_trips_exactly},
      {"psd_deep_32_bit_document_round_trips_exactly", psd_deep_32_bit_document_round_trips_exactly},
      {"psd_deep_gate_off_reads_8_bit_as_before", psd_deep_gate_off_reads_8_bit_as_before},
      {"psd_deep_photoshop_corpus_reads_and_round_trips_if_available",
       psd_deep_photoshop_corpus_reads_and_round_trips_if_available},
  };
}
