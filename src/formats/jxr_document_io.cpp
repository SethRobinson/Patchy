#include "formats/jxr_document_io.hpp"

#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "support/srgb_transfer.hpp"
#include "support/string_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace patchy::jxr {

namespace {

// Highlight rolloff for scRGB HDR: identity below the knee, then a rational curve that
// compresses everything above it into the remaining range.
//
// The alternative, a full filmic curve like Hable's, was rejected: normalized to an HDR
// white point it drags scRGB 1.0 (SDR reference white) down to sRGB 150, so an ordinary
// screenshot opens visibly dark and washed out. Here everything at or below the knee comes
// out byte-identical to a plain SDR conversion, which is most of a game frame, and only the
// top of the range pays for the headroom.
constexpr float kKneeStart = 0.5F;
// scRGB 12.5 is roughly 1000 nits, the usual HDR mastering ceiling and about where NVIDIA's
// captures top out. It maps to exactly 1.0, so the 1.0-to-12.5 highlight range still spreads
// over about 30 code values instead of clipping flat at white.
constexpr float kHdrMaxScrgb = 12.5F;

// Scale that puts kHdrMaxScrgb exactly at 1.0 while keeping slope 1 at the knee, so the two
// halves meet without a visible crease.
constexpr float kRolloffScale = (1.0F - kKneeStart) * (kHdrMaxScrgb - kKneeStart) / (kHdrMaxScrgb - 1.0F);

}  // namespace

const std::vector<std::string>& jxr_extensions() {
  // ".wdp" and ".hdp" are the Windows Media Photo / HD Photo extensions that predate
  // standardization; the same WIC codec decodes and encodes all three.
  static const std::vector<std::string> extensions = {"jxr", "wdp", "hdp"};
  return extensions;
}

bool is_jxr_extension(std::string_view extension) {
  const auto normalized = normalized_extension(extension, false);
  const auto& extensions = jxr_extensions();
  return std::find(extensions.begin(), extensions.end(), normalized) != extensions.end();
}

bool is_available() noexcept {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

bool sniff(std::span<const std::uint8_t> bytes) {
  // JPEG XR files are little-endian only: "II" then 0xBC then the format version (1 for
  // every file in the wild). TIFF shares the "II" mark but has 0x2A 0x00 at offset 2.
  return bytes.size() >= 4 && bytes[0] == 0x49 && bytes[1] == 0x49 && bytes[2] == 0xBC && bytes[3] != 0x00;
}

float highlight_rolloff(float value) {
  if (!(value > 0.0F)) {  // also catches NaN and scRGB's legal negative components
    return 0.0F;
  }
  if (value <= kKneeStart) {
    return value;
  }
  const float above = value - kKneeStart;
  return std::clamp(kKneeStart + kRolloffScale * above / (above + kRolloffScale), 0.0F, 1.0F);
}

std::vector<std::uint8_t> tone_map_scrgb_to_rgba8(std::span<const float> rgba_float, std::int32_t width,
                                                  std::int32_t height) {
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("JPEG XR tone map input has no pixels");
  }
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (rgba_float.size() < pixel_count * 4U) {
    throw std::runtime_error("JPEG XR tone map input buffer is too small");
  }

  std::vector<std::uint8_t> rgba(pixel_count * 4U);
  for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const float* source = rgba_float.data() + pixel * 4U;
    std::uint8_t* destination = rgba.data() + pixel * 4U;
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      destination[channel] = linear_to_srgb8(highlight_rolloff(source[channel]));
    }
    // Alpha is linear coverage, not a color, so it takes neither the tone curve nor the
    // sRGB transfer. NaN sorts to opaque rather than transparent.
    const float alpha = source[3];
    destination[3] = std::isnan(alpha)
                         ? static_cast<std::uint8_t>(255)
                         : static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
  }
  return rgba;
}

std::vector<std::uint8_t> write_jxr(const Document& document, const WriteOptions& options) {
  const auto flattened = flatten_document_rgba8(document);
  if (flattened.empty()) {
    throw std::runtime_error("Cannot write an empty document as JPEG XR");
  }
  const auto pixels = flattened.data();
  // A fully opaque flatten writes a 24bpp frame; only a document that really uses alpha
  // pays for the fourth channel.
  bool has_alpha = false;
  for (std::size_t offset = 3; offset < pixels.size(); offset += 4) {
    if (pixels[offset] != 0xFF) {
      has_alpha = true;
      break;
    }
  }
  // flatten_document_rgba8 packs rows tightly at 4 bytes per pixel, so the buffer is
  // already the layout write_jxr wants.
  const auto& print_settings = document.print_settings();
  return write_jxr(pixels, flattened.width(), flattened.height(), has_alpha, print_settings.horizontal_ppi,
                   print_settings.vertical_ppi, options);
}

std::vector<std::uint8_t> write_jxr(const Document& document) {
  return write_jxr(document, WriteOptions{});
}

void write_jxr_file(const Document& document, const std::filesystem::path& path, const WriteOptions& options) {
  formats::write_file_bytes(path, write_jxr(document, options), "JPEG XR");
}

#ifndef _WIN32

// Decoding and encoding JPEG XR needs the in-box Windows Imaging Component codec, and Qt
// ships no JPEG XR plugin, so the registry-error retry through QImageReader in
// load_document_from_path fails too and the user sees this message. The save filter row is
// gated on is_available(), so write_jxr is only reachable from a hand-typed path.
FormatReadResult read_jxr(std::span<const std::uint8_t> bytes) {
  (void)bytes;
  throw std::runtime_error("JPEG XR images can only be opened on Windows.");
}

std::vector<std::uint8_t> write_jxr(std::span<const std::uint8_t> rgba, std::int32_t width, std::int32_t height,
                                    bool has_alpha, double horizontal_ppi, double vertical_ppi,
                                    const WriteOptions& options) {
  (void)rgba;
  (void)width;
  (void)height;
  (void)has_alpha;
  (void)horizontal_ppi;
  (void)vertical_ppi;
  (void)options;
  throw std::runtime_error("JPEG XR images can only be saved on Windows.");
}

#endif  // !_WIN32

}  // namespace patchy::jxr
