// GIMP .xcf import (docs/xcf.md). Committed tests build their bytes in code
// (tests/xcf_test_builder.hpp); GIMP-made fixtures and the public corpus are optional
// local fixtures (local-test-fixtures/xcf, local-test-fixtures/xcf-corpus).

#include "core/blend_math.hpp"
#include "core/blend_math_deep.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "formats/layered_blend_support.hpp"
#include "psd/psd_document_io.hpp"
#include "core_test_support.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using patchy::BlendMode;
using patchy::Document;

// GIMP 3.2's renders of its legacy grain modes over 8-bit samples (modes-legacy.xcf in
// local-test-fixtures/xcf matched these kernels on every opaque pixel).
void blend_math_gimp_grain_modes_match_gimp_renders() {
  struct Sample {
    std::uint8_t s;
    std::uint8_t d;
    std::uint8_t extract;
    std::uint8_t merge;
  };
  constexpr std::array kSamples{
      Sample{0, 0, 128, 0},      Sample{255, 255, 128, 255}, Sample{200, 100, 28, 172},
      Sample{100, 200, 228, 172}, Sample{0, 255, 255, 127},   Sample{255, 0, 0, 127},
      Sample{128, 128, 128, 128}, Sample{10, 240, 255, 122},
  };
  for (const auto& sample : kSamples) {
    CHECK(patchy::blend_rgb({sample.s, sample.s, sample.s}, {sample.d, sample.d, sample.d},
                            BlendMode::GrainExtract)[0] == sample.extract);
    CHECK(patchy::blend_rgb({sample.s, sample.s, sample.s}, {sample.d, sample.d, sample.d},
                            BlendMode::GrainMerge)[0] == sample.merge);
  }
  // The 16/32-bit kernels agree with the byte kernels within a level.
  for (int a = 0; a < 256; a += 3) {
    for (int b = 0; b < 256; b += 5) {
      const auto s = static_cast<std::uint8_t>(a);
      const auto d = static_cast<std::uint8_t>(b);
      for (const auto mode : {BlendMode::GrainExtract, BlendMode::GrainMerge}) {
        const auto byte = patchy::blend_rgb({s, s, s}, {d, d, d}, mode)[0];
        const auto deep = patchy::blend_rgb_deep({static_cast<float>(a), static_cast<float>(a), static_cast<float>(a)},
                                                 {static_cast<float>(b), static_cast<float>(b), static_cast<float>(b)},
                                                 mode, patchy::DeepDomain::Encoded)[0];
        CHECK(std::abs(deep - static_cast<float>(byte)) <= 1.0F);
      }
    }
  }
  CHECK(patchy::blend_mode_is_gimp_only(BlendMode::GrainMerge));
  CHECK(!patchy::blend_mode_is_gimp_only(BlendMode::Xor));
  CHECK(patchy::blend_mode_is_non_photoshop(BlendMode::GrainExtract));
  CHECK(patchy::blend_mode_is_non_photoshop(BlendMode::Reflect));
  CHECK(!patchy::blend_mode_is_non_photoshop(BlendMode::LinearLight));
  CHECK(!patchy::blend_mode_supported_at_depth(BlendMode::GrainExtract, patchy::BitDepth::Float32));
  CHECK(patchy::blend_mode_supported_at_depth(BlendMode::GrainMerge, patchy::BitDepth::UInt16));
}

void psd_saves_gimp_blend_modes_as_normal() {
  Document document(4, 2, patchy::PixelFormat::rgba8());
  const std::array modes{BlendMode::GrainExtract, BlendMode::GrainMerge, BlendMode::Multiply};
  for (const auto mode : modes) {
    auto& layer = document.add_pixel_layer("layer", patchy::test::solid_rgba(4, 2, 100, 150, 200, 255));
    layer.set_blend_mode(mode);
  }
  CHECK(patchy::layers_use_gimp_blend_modes(document.layers()));
  const auto unsupported = patchy::find_unsupported_blend_modes(document, patchy::LayeredSaveFormat::Photoshop);
  CHECK((unsupported.modes == std::vector<BlendMode>{BlendMode::GrainExtract, BlendMode::GrainMerge}));
  CHECK(!patchy::layered_format_stores_blend_mode(patchy::LayeredSaveFormat::Aseprite, BlendMode::GrainMerge, false,
                                                  patchy::BitDepth::UInt8));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  CHECK(reread.layers().size() == modes.size());
  CHECK(reread.layers()[0].blend_mode() == BlendMode::Normal);
  CHECK(reread.layers()[1].blend_mode() == BlendMode::Normal);
  CHECK(reread.layers()[2].blend_mode() == BlendMode::Multiply);
}

}  // namespace

std::vector<patchy::test::TestCase> xcf_tests() {
  return {
      {"blend_math_gimp_grain_modes_match_gimp_renders", blend_math_gimp_grain_modes_match_gimp_renders},
      {"psd_saves_gimp_blend_modes_as_normal", psd_saves_gimp_blend_modes_as_normal},
  };
}
