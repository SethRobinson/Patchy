// Flat TIFF fallback decoder (docs/tiff.md, "Flat fallback decoder"): synthetic files in
// every layout it reads, written by tests/flat_tiff_fixture.hpp in either byte order.

#include "core/document.hpp"
#include "core/document_channel.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "psd/psd_tiff.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include "flat_tiff_fixture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::BitDepth;
using patchy::Document;
using patchy::PixelBuffer;

using namespace patchy::test::flat_tiff;

// A stored sample of a document buffer at its own scale.
double stored(const PixelBuffer& buffer, std::int32_t x, std::int32_t y, std::size_t channel) {
  const auto channels = static_cast<std::size_t>(buffer.format().channels);
  const auto index = static_cast<std::size_t>(x) * channels + channel;
  const auto row = buffer.row(y);
  switch (buffer.format().bit_depth) {
    case BitDepth::UInt8: return row[index];
    case BitDepth::UInt16: {
      std::uint16_t value = 0;
      std::memcpy(&value, row.data() + index * 2U, 2U);
      return value;
    }
    case BitDepth::Float32: {
      float value = 0;
      std::memcpy(&value, row.data() + index * 4U, 4U);
      return value;
    }
  }
  return 0;
}

const PixelBuffer& layer_pixels(const Document& document) {
  CHECK(document.layers().size() == 1U);
  return document.layers().front().pixels();
}

// 0..255 or 0..65535 test pattern that differs per pixel and sample.
double pattern(std::uint32_t x, std::uint32_t y, std::uint32_t sample, double max) {
  return std::fmod(x * 37.0 + y * 101.0 + sample * 59.0 + (x * y) * 7.0, max + 1.0);
}

// Pseudo-random bytes so LZW reaches its table limit and Clear codes (splitmix64).
double noise(std::uint32_t x, std::uint32_t y, std::uint32_t sample, double max) {
  std::uint64_t z = (static_cast<std::uint64_t>(y) << 40U) ^ (static_cast<std::uint64_t>(x) << 8U) ^ sample;
  z += 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  z ^= z >> 31U;
  return static_cast<double>(z % (static_cast<std::uint64_t>(max) + 1U));
}

// The #623 layout: little-endian, uncompressed, RGB with seven 32-bit float samples, four of
// them ExtraSamples 0 (unspecified), as in tlnagy/exampletiffs shapes_hyper.tif.
void tiff_flat_float_rgb_with_unspecified_extras_opens() {
  Spec spec;
  spec.kind = Kind::F32;
  spec.samples = 7;
  spec.extra = std::vector<std::uint16_t>{0, 0, 0, 0};
  spec.width = 16;
  spec.height = 9;
  spec.value = [](std::uint32_t x, std::uint32_t y, std::uint32_t sample) {
    if (sample >= 3U) {
      return std::array<double, 4>{0.1, 0.2, 0.3, 0.5}[sample - 3U];
    }
    return (x * 3U + y * 5U + sample * 11U) % 17U / 16.0;
  };
  const auto document = patchy::psd::read_flat_tiff(write_tiff(spec));
  CHECK(document.width() == 16 && document.height() == 9);
  CHECK(document.color_state().bit_depth == BitDepth::Float32);
  const auto& pixels = layer_pixels(document);
  CHECK(pixels.format().channels == 3U);
  CHECK(pixels.format().bit_depth == BitDepth::Float32);
  bool same = true;
  for (std::uint32_t y = 0; y < 9U; ++y) {
    for (std::uint32_t x = 0; x < 16U; ++x) {
      for (std::uint32_t c = 0; c < 3U; ++c) {
        same = same && stored(pixels, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), c) ==
                           static_cast<float>(spec.value(x, y, c));
      }
    }
  }
  CHECK(same);
  CHECK(document.channels().size() == 4U);
  const std::array<float, 4> extras{0.1F, 0.2F, 0.3F, 0.5F};
  for (std::size_t index = 0; index < document.channels().size(); ++index) {
    const auto& channel = document.channels()[index];
    CHECK(channel.name() == "Alpha " + std::to_string(index + 1U));
    CHECK(channel.kind() == patchy::DocumentChannelKind::Alpha);
    CHECK(channel.pixels().format().bit_depth == BitDepth::Float32);
    CHECK(stored(channel.pixels(), 5, 4, 0) == extras[index]);
  }
  CHECK(document.print_settings().horizontal_ppi == 300.0);
}

// Every structural variant decodes to the same pixels: both byte orders, chunky and planar,
// strips and tiles, each compression with its predictors, 8 and 16-bit RGB with alpha.
void tiff_flat_layouts_and_compressions_decode_identically() {
  int cases = 0;
  for (const auto kind : {Kind::U8, Kind::U16}) {
    const double max = kind == Kind::U8 ? 255.0 : 65535.0;
    for (const bool little : {true, false}) {
      for (const bool planar : {false, true}) {
        for (const std::uint32_t tile : {0U, 16U}) {
          for (const std::uint16_t compression : std::array<std::uint16_t, 4>{1, 5, 8, 32773}) {
            for (const std::uint16_t predictor : std::array<std::uint16_t, 2>{1, 2}) {
              if (predictor == 2U && (compression == 1U || compression == 32773U)) {
                continue;
              }
              Spec spec;
              spec.little = little;
              spec.kind = kind;
              spec.samples = 4;
              spec.extra = std::vector<std::uint16_t>{2};
              spec.width = 37;
              spec.height = 29;
              spec.planar = planar;
              spec.tile = tile;
              spec.rows_per_strip = 5;
              spec.compression = compression;
              spec.predictor = predictor;
              const bool use_noise = compression == 5U;
              spec.value = [max, use_noise](std::uint32_t x, std::uint32_t y, std::uint32_t sample) {
                return use_noise ? noise(x, y, sample, max) : pattern(x, y, sample, max);
              };
              if (use_noise) {
                spec.width = 97;  // past 4094 LZW codes, so the decoder meets a Clear mid-stream
                spec.height = 61;
              }
              const auto document = patchy::psd::read_flat_tiff(write_tiff(spec));
              CHECK(document.color_state().bit_depth == (kind == Kind::U8 ? BitDepth::UInt8 : BitDepth::UInt16));
              const auto& pixels = layer_pixels(document);
              CHECK(pixels.format().channels == 4U);
              bool same = true;
              for (std::uint32_t y = 0; y < spec.height; ++y) {
                for (std::uint32_t x = 0; x < spec.width; ++x) {
                  for (std::uint32_t c = 0; c < 4U; ++c) {
                    same = same && stored(pixels, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), c) ==
                                       spec.value(x, y, c);
                  }
                }
              }
              if (!same) {
                std::cerr << "layout mismatch: kind " << static_cast<int>(kind) << " little " << little << " planar "
                          << planar << " tile " << tile << " compression " << compression << " predictor "
                          << predictor << '\n';
              }
              CHECK(same);
              CHECK(document.channels().empty());
              ++cases;
            }
          }
        }
      }
    }
  }
  CHECK(cases == 96);
}

// Float data with predictor 3 (and 2), every float width, in both byte orders.
void tiff_flat_float_depths_and_predictors() {
  for (const auto kind : {Kind::F16, Kind::F32, Kind::F64}) {
    for (const bool little : {true, false}) {
      for (const std::uint16_t predictor : std::array<std::uint16_t, 2>{1, 3}) {
        for (const bool planar : {false, true}) {
          Spec spec;
          spec.little = little;
          spec.kind = kind;
          spec.planar = planar;
          spec.compression = 8;
          spec.predictor = predictor;
          spec.width = 21;
          spec.height = 11;
          spec.value = [](std::uint32_t x, std::uint32_t y, std::uint32_t sample) {
            return static_cast<double>((x * 13U + y * 7U + sample * 29U) % 257U) / 256.0;
          };
          const auto document = patchy::psd::read_flat_tiff(write_tiff(spec));
          CHECK(document.color_state().bit_depth == BitDepth::Float32);
          const auto& pixels = layer_pixels(document);
          bool same = true;
          for (std::uint32_t y = 0; y < spec.height; ++y) {
            for (std::uint32_t x = 0; x < spec.width; ++x) {
              for (std::uint32_t c = 0; c < 3U; ++c) {
                same = same && stored(pixels, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), c) ==
                                   static_cast<float>(spec.value(x, y, c));
              }
            }
          }
          CHECK(same);
        }
      }
    }
  }
}

// 32-bit unsigned integers open as a 16-bit document (the top 16 bits, rounded).
void tiff_flat_32_bit_integers_open_at_16_bits() {
  Spec spec;
  spec.kind = Kind::U32;
  spec.little = false;
  spec.value = [](std::uint32_t x, std::uint32_t y, std::uint32_t sample) {
    return pattern(x, y, sample, 65535.0) * 65537.0;
  };
  const auto document = patchy::psd::read_flat_tiff(write_tiff(spec));
  CHECK(document.color_state().bit_depth == BitDepth::UInt16);
  const auto& pixels = layer_pixels(document);
  CHECK(stored(pixels, 3, 2, 1) == pattern(3, 2, 1, 65535.0));
  CHECK(stored(pixels, 8, 6, 2) == pattern(8, 6, 2, 65535.0));
}

// Associated alpha is unpremultiplied; a second alpha and unspecified extras become channels.
void tiff_flat_associated_alpha_and_extra_channels() {
  Spec spec;
  spec.samples = 6;
  spec.extra = std::vector<std::uint16_t>{1, 0, 2};
  spec.width = 4;
  spec.height = 1;
  spec.value = [](std::uint32_t x, std::uint32_t, std::uint32_t sample) {
    const std::array<double, 4> alpha{255.0, 128.0, 64.0, 0.0};
    const std::array<double, 3> color{200.0, 100.0, 50.0};
    if (sample < 3U) {
      return std::round(color[sample] * alpha[x] / 255.0);
    }
    return sample == 3U ? alpha[x] : sample == 4U ? 77.0 : 99.0;
  };
  const auto document = patchy::psd::read_flat_tiff(write_tiff(spec));
  const auto& pixels = layer_pixels(document);
  CHECK(pixels.format().channels == 4U);
  CHECK(stored(pixels, 0, 0, 0) == 200.0 && stored(pixels, 0, 0, 3) == 255.0);
  CHECK(std::abs(stored(pixels, 1, 0, 0) - 200.0) <= 1.0 && stored(pixels, 1, 0, 3) == 128.0);
  CHECK(std::abs(stored(pixels, 2, 0, 1) - 100.0) <= 2.0 && stored(pixels, 2, 0, 3) == 64.0);
  CHECK(stored(pixels, 3, 0, 0) == 0.0 && stored(pixels, 3, 0, 3) == 0.0);
  CHECK(document.channels().size() == 2U);
  CHECK(stored(document.channels()[0].pixels(), 2, 0, 0) == 77.0);
  CHECK(stored(document.channels()[1].pixels(), 2, 0, 0) == 99.0);
}

// Gray (both polarities) opens as RGB with the value in every channel; palette indexes map
// through the color map.
void tiff_flat_gray_and_palette_open_as_rgb() {
  for (const std::uint16_t photometric : std::array<std::uint16_t, 2>{0, 1}) {
    Spec spec;
    spec.photometric = photometric;
    spec.samples = 2;
    spec.extra = std::vector<std::uint16_t>{0};
    spec.kind = Kind::U16;
    spec.value = [](std::uint32_t x, std::uint32_t y, std::uint32_t sample) {
      return sample == 0U ? pattern(x, y, 0, 65535.0) : 1234.0;
    };
    const auto document = patchy::psd::read_flat_tiff(write_tiff(spec));
    const auto& pixels = layer_pixels(document);
    const auto expected = photometric == 1U ? pattern(4, 3, 0, 65535.0) : 65535.0 - pattern(4, 3, 0, 65535.0);
    CHECK(pixels.format().channels == 3U);
    CHECK(stored(pixels, 4, 3, 0) == expected && stored(pixels, 4, 3, 1) == expected &&
          stored(pixels, 4, 3, 2) == expected);
    CHECK(document.channels().size() == 1U && stored(document.channels()[0].pixels(), 4, 3, 0) == 1234.0);
  }
  Spec palette;
  palette.photometric = 3;
  palette.samples = 1;
  palette.color_map.resize(768);
  for (std::uint32_t index = 0; index < 256U; ++index) {
    palette.color_map[index] = static_cast<std::uint16_t>(index * 257U);
    palette.color_map[256U + index] = static_cast<std::uint16_t>((255U - index) * 257U);
    palette.color_map[512U + index] = static_cast<std::uint16_t>(0x8080U);
  }
  palette.value = [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<double>((x * 20U + y) % 256U); };
  const auto document = patchy::psd::read_flat_tiff(write_tiff(palette));
  const auto& pixels = layer_pixels(document);
  CHECK(document.color_state().bit_depth == BitDepth::UInt8);
  CHECK(stored(pixels, 5, 2, 0) == 102.0 && stored(pixels, 5, 2, 1) == 153.0 && stored(pixels, 5, 2, 2) == 128.0);
}

// Layouts outside the decoder's set are refused with an error, never guessed.
void tiff_flat_unsupported_layouts_throw() {
  const auto refuses = [](std::vector<std::uint8_t> bytes) {
    try {
      (void)patchy::psd::read_flat_tiff(bytes);
    } catch (const std::runtime_error&) {
      return true;
    }
    return false;
  };
  Spec spec;
  spec.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0; };
  auto bytes = write_tiff(spec);
  CHECK(!refuses(bytes));
  // Patch single entries: CMYK, JPEG compression, signed samples, 10-bit samples.
  const auto patch = [&](std::uint16_t tag, std::uint16_t value) {
    auto copy = bytes;
    const auto directory = copy[4] | (copy[5] << 8U) | (copy[6] << 16U) | (copy[7] << 24U);
    const auto count = copy[directory] | (copy[directory + 1] << 8U);
    for (int index = 0; index < count; ++index) {
      const auto at = static_cast<std::size_t>(directory + 2 + index * 12);
      if ((copy[at] | (copy[at + 1] << 8U)) == tag) {
        if (copy[at + 4] == 1U) {
          copy[at + 8] = static_cast<std::uint8_t>(value & 0xFFU);
          copy[at + 9] = static_cast<std::uint8_t>(value >> 8U);
        } else {
          // BitsPerSample/SampleFormat with three values live out of line.
          const auto values = copy[at + 8] | (copy[at + 9] << 8U) | (copy[at + 10] << 16U) | (copy[at + 11] << 24U);
          for (int sample = 0; sample < 3; ++sample) {
            copy[values + sample * 2] = static_cast<std::uint8_t>(value & 0xFFU);
            copy[values + sample * 2 + 1] = static_cast<std::uint8_t>(value >> 8U);
          }
        }
      }
    }
    return copy;
  };
  CHECK(refuses(patch(262, 5)));
  CHECK(refuses(patch(259, 7)));
  CHECK(refuses(patch(339, 2)));
  CHECK(refuses(patch(258, 10)));
  CHECK(refuses({}));
  CHECK(refuses({'I', 'I', 43, 0, 8, 0, 0, 0}));  // BigTIFF
}

// Truncation and byte flips: the read throws or opens, never reads past the buffer.
void tiff_flat_damaged_data_throws_or_opens() {
  Spec spec;
  spec.kind = Kind::U16;
  spec.samples = 4;
  spec.extra = std::vector<std::uint16_t>{0};
  spec.compression = 5;
  spec.predictor = 2;
  spec.rows_per_strip = 2;
  spec.value = [](std::uint32_t x, std::uint32_t y, std::uint32_t sample) { return pattern(x, y, sample, 65535.0); };
  const auto bytes = write_tiff(spec);
  std::size_t opened = 0;
  std::size_t attempts = 0;
  for (std::size_t length = 0; length < bytes.size(); length += 3U) {
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
    try {
      (void)patchy::psd::read_flat_tiff(cut);
      ++opened;
    } catch (const std::exception&) {
    }
    ++attempts;
  }
  for (std::size_t at = 0; at < bytes.size(); ++at) {
    for (const std::uint8_t flip : std::array<std::uint8_t, 3>{0x01, 0x80, 0xFF}) {
      auto mutated = bytes;
      mutated[at] ^= flip;
      try {
        (void)patchy::psd::read_flat_tiff(mutated);
      } catch (const std::exception&) {
      }
      ++attempts;
    }
  }
  CHECK(attempts > 1000U);
  static_cast<void>(opened);

  // A file cut inside its image data (directory first) opens with a notice.
  Spec uncompressed = spec;
  uncompressed.compression = 1;
  uncompressed.predictor = 1;
  auto whole = write_tiff(uncompressed);
  const auto directory = static_cast<std::size_t>(whole[4] | (whole[5] << 8U) | (whole[6] << 16U) | (whole[7] << 24U));
  std::vector<std::string> notices;
  patchy::psd::FlatTiffReadOptions options;
  options.notices = &notices;
  auto damaged = whole;
  // Point the first strip's offset past the end of the file.
  const auto count = damaged[directory] | (damaged[directory + 1] << 8U);
  for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
    const auto at = directory + 2U + index * 12U;
    if ((damaged[at] | (damaged[at + 1] << 8U)) == 273) {
      const auto values = static_cast<std::size_t>(damaged[at + 8] | (damaged[at + 9] << 8U) | (damaged[at + 10] << 16U) |
                                                   (damaged[at + 11] << 24U));
      damaged[values] = 0xF0;
      damaged[values + 1] = 0xFF;
      damaged[values + 2] = 0xFF;
      damaged[values + 3] = 0x00;
    }
  }
  const auto document = patchy::psd::read_flat_tiff(damaged, options);
  CHECK(document.width() == static_cast<std::int32_t>(spec.width));
  CHECK(notices.size() == 1U);
  CHECK(stored(layer_pixels(document), 0, 0, 0) == 0.0);
  CHECK(stored(layer_pixels(document), 0, 3, 0) == pattern(0, 3, 0, 65535.0));
}

// 12-bit samples (packed most significant bit first, each row on a byte) open at 16 bits,
// scaled to the full 16-bit range; chunky and planar, uncompressed and LZW, both orders.
// Qt's TIFF plug-in refuses them (GitHub issue 625's shapes_multi_color.tif, page 4).
void tiff_flat_12_bit_samples_open_at_16_bits() {
  const auto value = [](std::uint32_t x, std::uint32_t y, std::uint32_t sample) {
    return static_cast<double>((x * 397U + y * 61U + sample * 1201U) % 4096U);
  };
  for (const bool little : {true, false}) {
    for (const bool planar : {false, true}) {
      for (const std::uint16_t compression : {std::uint16_t{1}, std::uint16_t{5}}) {
        Spec spec;
        spec.little = little;
        spec.planar = planar;
        spec.compression = compression;
        spec.kind = Kind::U12;
        spec.width = 9;  // odd: 27 samples, so chunky rows end on half a byte
        spec.rows_per_strip = 3;
        spec.value = value;
        const auto document = patchy::psd::read_flat_tiff(write_tiff(spec));
        CHECK(document.color_state().bit_depth == BitDepth::UInt16);
        const auto& pixels = layer_pixels(document);
        bool exact = true;
        for (std::uint32_t y = 0; y < spec.height; ++y) {
          for (std::uint32_t x = 0; x < spec.width; ++x) {
            for (std::uint32_t sample = 0; sample < 3U; ++sample) {
              const auto expected = std::floor((value(x, y, sample) * 65535.0 + 2047.0) / 4095.0);
              exact = exact && stored(pixels, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), sample) == expected;
            }
          }
        }
        CHECK(exact);
      }
    }
  }
  // Differencing is not defined at 12 bits.
  Spec predicted;
  predicted.kind = Kind::U12;
  predicted.compression = 5;
  predicted.predictor = 2;
  predicted.value = value;
  bool threw = false;
  try {
    (void)patchy::psd::read_flat_tiff(write_tiff(predicted));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

// Pages decode by directory offset (multi-page TIFF, docs/tiff.md).
void tiff_flat_reads_any_page_by_directory_offset() {
  Spec first;
  first.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 10.0; };
  Spec second = first;
  second.width = 5;
  second.height = 3;
  second.kind = Kind::F32;
  second.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.75; };
  const auto bytes = write_tiff(std::vector<Spec>{first, second});
  const auto pages = patchy::psd::tiff_page_directory_offsets(bytes);
  CHECK(pages.size() == 2U);
  CHECK(patchy::psd::tiff_page_count(bytes) == 2U);
  const auto page_one = patchy::psd::read_flat_tiff(bytes);
  CHECK(page_one.width() == 9 && stored(layer_pixels(page_one), 0, 0, 0) == 10.0);
  patchy::psd::FlatTiffReadOptions options;
  options.directory_offset = pages[1];
  const auto page_two = patchy::psd::read_flat_tiff(bytes, options);
  CHECK(page_two.width() == 5 && page_two.color_state().bit_depth == BitDepth::Float32);
  CHECK(stored(layer_pixels(page_two), 4, 2, 2) == 0.75);
}

// The page walk (docs/tiff.md, "Multi-page TIFF"): reduced-resolution directories are not
// pages but still count in the chain index Qt's jumpToImage takes; PageName is read.
void tiff_pages_lists_pages_names_and_chain_indexes() {
  for (const bool little : {true, false}) {
    Spec first;
    first.little = little;
    first.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 10.0; };
    Spec preview = first;
    preview.width = 3;
    preview.height = 2;
    preview.subfile_type = 1;
    Spec named = first;
    named.width = 12;
    named.height = 4;
    named.page_name = "Scan B \x01 ";
    Spec short_name = first;
    short_name.page_name = "abc";  // fits the entry inline
    const auto bytes = write_tiff(std::vector<Spec>{first, preview, named, short_name});
    const auto pages = patchy::psd::tiff_pages(bytes);
    CHECK(pages.size() == 3U);
    if (pages.size() != 3U) {
      continue;
    }
    CHECK(pages[0].directory_index == 0U && pages[0].name.empty());
    CHECK(pages[0].width == 9U && pages[0].height == 7U);
    CHECK(pages[1].directory_index == 2U && pages[1].name == "Scan B");
    CHECK(pages[1].width == 12U && pages[1].height == 4U);
    CHECK(pages[2].directory_index == 3U && pages[2].name == "abc");
    const auto offsets = patchy::psd::tiff_page_directory_offsets(bytes);
    CHECK(offsets.size() == 3U && offsets[1] == pages[1].directory_offset);
  }
  CHECK(patchy::psd::tiff_pages(std::vector<std::uint8_t>{'I', 'I', 42, 0}).empty());
}

// Assembly: the largest canvas, the highest depth, page 1 on top, every page at the top
// left, extra channels left out with one notice.
void tiff_pages_as_layers_stacks_pages_at_the_highest_depth() {
  Spec small;
  small.width = 4;
  small.height = 3;
  small.value = [](std::uint32_t, std::uint32_t, std::uint32_t sample) { return sample == 0U ? 255.0 : 0.0; };
  Spec wide;
  wide.kind = Kind::U16;
  wide.width = 10;
  wide.height = 2;
  wide.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 65535.0; };
  Spec deep;
  deep.kind = Kind::F32;
  deep.width = 5;
  deep.height = 8;
  deep.samples = 4;
  deep.extra = std::vector<std::uint16_t>{0};
  deep.value = [](std::uint32_t, std::uint32_t, std::uint32_t sample) { return sample == 3U ? 0.5 : 1.0; };
  const auto bytes = write_tiff(std::vector<Spec>{small, wide, deep});
  const auto offsets = patchy::psd::tiff_page_directory_offsets(bytes);
  CHECK(offsets.size() == 3U);
  if (offsets.size() != 3U) {
    return;
  }
  std::vector<patchy::psd::TiffPageDocument> pages;
  for (std::size_t index = 0; index < offsets.size(); ++index) {
    patchy::psd::FlatTiffReadOptions options;
    options.directory_offset = offsets[index];
    pages.push_back({patchy::psd::read_flat_tiff(bytes, options), "Page " + std::to_string(index + 1U)});
  }
  CHECK(std::as_const(pages[2].document).channels().size() == 1U);
  std::vector<std::string> notices;
  const auto document = patchy::psd::tiff_pages_as_layers(std::move(pages), &notices);
  CHECK(document.width() == 10 && document.height() == 8);
  CHECK(document.color_state().bit_depth == BitDepth::Float32);
  CHECK(document.format().bit_depth == BitDepth::Float32);
  CHECK(document.channels().empty());
  CHECK(notices.size() == 1U);
  const auto& layers = document.layers();
  CHECK(layers.size() == 3U);
  if (layers.size() != 3U) {
    return;
  }
  // Bottom to top: page 3, page 2, page 1; page 1 is the active layer.
  CHECK(layers[0].name() == "Page 3" && layers[1].name() == "Page 2" && layers[2].name() == "Page 1");
  CHECK(document.active_layer_id() == layers[2].id());
  for (const auto& layer : layers) {
    CHECK(layer.visible());
    CHECK(layer.bounds().x == 0 && layer.bounds().y == 0);
    CHECK(layer.pixels().format().channels == 4U);
    CHECK(layer.pixels().format().bit_depth == BitDepth::Float32);
  }
  CHECK(layers[2].bounds().width == 4 && layers[2].bounds().height == 3);
  CHECK(layers[1].bounds().width == 10 && layers[1].bounds().height == 2);
  // The 8-bit red decodes to linear 1.0 (255 on the linear deep scale), opaque.
  std::vector<float> row(4U * 4U);
  patchy::load_rgba_row(layers[2].pixels(), 0, 0, 4, patchy::DeepDomain::Linear, row);
  CHECK(std::fabs(row[0] - 255.0F) < 0.01F && std::fabs(row[1]) < 0.01F && std::fabs(row[3] - 255.0F) < 0.01F);
  CHECK(patchy::document_depth_problems(document).empty());
}

void tiff_pages_as_layers_of_one_depth_keeps_it() {
  Spec page;
  page.value = [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<double>(x * 20U); };
  Spec second = page;
  second.width = 3;
  const auto bytes = write_tiff(std::vector<Spec>{page, second});
  const auto offsets = patchy::psd::tiff_page_directory_offsets(bytes);
  std::vector<patchy::psd::TiffPageDocument> pages;
  for (const auto offset : offsets) {
    patchy::psd::FlatTiffReadOptions options;
    options.directory_offset = offset;
    pages.push_back({patchy::psd::read_flat_tiff(bytes, options), "Page"});
  }
  std::vector<std::string> notices;
  const auto document = patchy::psd::tiff_pages_as_layers(std::move(pages), &notices);
  CHECK(document.color_state().bit_depth == BitDepth::UInt8);
  CHECK(notices.empty());
  CHECK(document.layers().size() == 2U);
  if (document.layers().size() == 2U) {
    CHECK(stored(document.layers()[1].pixels(), 5, 0, 0) == 100.0);
    CHECK(stored(document.layers()[1].pixels(), 5, 0, 3) == 255.0);
  }
  bool threw = false;
  try {
    (void)patchy::psd::tiff_pages_as_layers({});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void tiff_flat_unicode_path_opens() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"flat-tiff");
  Spec spec;
  spec.kind = Kind::F32;
  spec.samples = 5;
  spec.extra = std::vector<std::uint16_t>{0, 0};
  spec.value = [](std::uint32_t, std::uint32_t, std::uint32_t sample) { return sample * 0.25; };
  const auto bytes = write_tiff(spec);
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto path = dir / patchy::test::unicode_path_piece(std::u8string(stem) + u8".tif");
    {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto document = patchy::psd::read_flat_tiff_file(path);
    CHECK(document.channels().size() == 2U);
    CHECK(stored(layer_pixels(document), 1, 1, 2) == 0.5);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> tiff_flat_tests() {
  return {
      {"tiff_flat_float_rgb_with_unspecified_extras_opens", tiff_flat_float_rgb_with_unspecified_extras_opens},
      {"tiff_flat_layouts_and_compressions_decode_identically", tiff_flat_layouts_and_compressions_decode_identically},
      {"tiff_flat_float_depths_and_predictors", tiff_flat_float_depths_and_predictors},
      {"tiff_flat_32_bit_integers_open_at_16_bits", tiff_flat_32_bit_integers_open_at_16_bits},
      {"tiff_flat_associated_alpha_and_extra_channels", tiff_flat_associated_alpha_and_extra_channels},
      {"tiff_flat_gray_and_palette_open_as_rgb", tiff_flat_gray_and_palette_open_as_rgb},
      {"tiff_flat_unsupported_layouts_throw", tiff_flat_unsupported_layouts_throw},
      {"tiff_flat_damaged_data_throws_or_opens", tiff_flat_damaged_data_throws_or_opens},
      {"tiff_flat_12_bit_samples_open_at_16_bits", tiff_flat_12_bit_samples_open_at_16_bits},
      {"tiff_flat_reads_any_page_by_directory_offset", tiff_flat_reads_any_page_by_directory_offset},
      {"tiff_flat_unicode_path_opens", tiff_flat_unicode_path_opens},
      {"tiff_pages_lists_pages_names_and_chain_indexes", tiff_pages_lists_pages_names_and_chain_indexes},
      {"tiff_pages_as_layers_stacks_pages_at_the_highest_depth", tiff_pages_as_layers_stacks_pages_at_the_highest_depth},
      {"tiff_pages_as_layers_of_one_depth_keeps_it", tiff_pages_as_layers_of_one_depth_keeps_it},
  };
}
