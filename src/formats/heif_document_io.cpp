#include "formats/heif_document_io.hpp"

#include "support/string_utils.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace patchy::heif {

namespace {

// HEVC-coded HEIF brands (ISO/IEC 23008-12 + vendor practice). iPhone stills are "heic",
// Sony/Fujifilm .hif use "heix"/"msf1", generic single/multi-image files use "mif1"/"msf1".
// AVIF brands are deliberately absent.
constexpr std::array<std::string_view, 10> kHeifBrands = {
    "heic", "heix", "hevc", "hevx", "heim", "heis", "hevm", "hevs", "mif1", "msf1",
};

[[nodiscard]] bool is_heif_brand(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    return false;
  }
  const std::string_view brand(reinterpret_cast<const char*>(bytes.data() + offset), 4);
  return std::find(kHeifBrands.begin(), kHeifBrands.end(), brand) != kHeifBrands.end();
}

}  // namespace

const std::vector<std::string>& heif_extensions() {
  // ".hif" is the Sony/Fujifilm in-camera HEIF extension; the same decoders handle it.
  static const std::vector<std::string> extensions = {"heic", "heif", "hif"};
  return extensions;
}

bool is_heif_extension(std::string_view extension) {
  const auto normalized = normalized_extension(extension, false);
  const auto& extensions = heif_extensions();
  return std::find(extensions.begin(), extensions.end(), normalized) != extensions.end();
}

bool sniff(std::span<const std::uint8_t> bytes) {
  // ISOBMFF: u32 BE box size, "ftyp", 4-byte major brand, u32 minor version, then 4-byte
  // compatible brands to the end of the box. Accept when the major brand or any
  // compatible brand is an HEVC-coded HEIF brand.
  if (bytes.size() < 16 || std::memcmp(bytes.data() + 4, "ftyp", 4) != 0) {
    return false;
  }
  const std::size_t box_size = (static_cast<std::size_t>(bytes[0]) << 24U) |
                               (static_cast<std::size_t>(bytes[1]) << 16U) |
                               (static_cast<std::size_t>(bytes[2]) << 8U) |
                               static_cast<std::size_t>(bytes[3]);
  if (box_size < 16 || (box_size % 4) != 0) {
    return false;
  }
  if (is_heif_brand(bytes, 8)) {
    return true;
  }
  const auto brands_end = std::min<std::size_t>(box_size, bytes.size());
  for (std::size_t offset = 16; offset + 4 <= brands_end; offset += 4) {
    if (is_heif_brand(bytes, offset)) {
      return true;
    }
  }
  return false;
}

OrientedImage apply_exif_orientation(std::span<const std::uint8_t> rgba, std::int32_t width,
                                     std::int32_t height, int orientation) {
  if (width <= 0 || height <= 0 ||
      rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
    throw std::runtime_error("HEIF orientation input buffer is too small");
  }
  if (orientation < 1 || orientation > 8) {
    orientation = 1;
  }
  const bool swaps_axes = orientation >= 5;
  OrientedImage out;
  out.width = swaps_axes ? height : width;
  out.height = swaps_axes ? width : height;
  out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4U);

  // EXIF semantics: the value describes where the stored 0th row/column appear visually.
  // For each output (display) pixel, compute the source pixel that belongs there.
  for (std::int32_t oy = 0; oy < out.height; ++oy) {
    auto* destination = out.rgba.data() + static_cast<std::size_t>(oy) * static_cast<std::size_t>(out.width) * 4U;
    for (std::int32_t ox = 0; ox < out.width; ++ox, destination += 4) {
      std::int32_t sx = ox;
      std::int32_t sy = oy;
      switch (orientation) {
        case 2:  // mirrored horizontally
          sx = width - 1 - ox;
          break;
        case 3:  // rotated 180
          sx = width - 1 - ox;
          sy = height - 1 - oy;
          break;
        case 4:  // mirrored vertically
          sy = height - 1 - oy;
          break;
        case 5:  // transposed (stored row 0 = visual left, column 0 = visual top)
          sx = oy;
          sy = ox;
          break;
        case 6:  // rotate stored image 90 CW to display
          sx = oy;
          sy = height - 1 - ox;
          break;
        case 7:  // transverse (stored row 0 = visual right, column 0 = visual bottom)
          sx = width - 1 - oy;
          sy = height - 1 - ox;
          break;
        case 8:  // rotate stored image 90 CCW to display
          sx = width - 1 - oy;
          sy = ox;
          break;
        default:
          break;
      }
      const auto* source =
          rgba.data() + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)) * 4U;
      std::memcpy(destination, source, 4U);
    }
  }
  return out;
}

#ifndef _WIN32

// Non-Windows read: always defer to the Qt fallback (load_document_from_path retries a
// failed registry read through QImageReader, which has qmacheif on macOS and the KDE
// runtime's kimg_heif on Linux). This message therefore surfaces only when the platform
// codec is missing or the file is broken, so it doubles as the how-to-fix text.
FormatReadResult read_heif(std::span<const std::uint8_t> bytes) {
  (void)bytes;
#ifdef __APPLE__
  throw std::runtime_error("Unable to decode this HEIC image with the system codec.");
#else
  throw std::runtime_error(
      "Unable to decode this HEIC image. HEIC decoding needs the Flatpak codec extension; "
      "install it with: flatpak install flathub org.freedesktop.Platform.ffmpeg-full//24.08");
#endif
}

#endif  // !_WIN32

}  // namespace patchy::heif
