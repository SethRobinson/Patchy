#pragma once

#include "formats/format_registry.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::heif {

// HEIF/HEIC still images (iPhone .heic, Sony/Fujifilm .hif). Decode-only, and the decoder
// always comes from platform codecs -- Patchy must never ship an HEVC decoder (libheif/
// libde265/x265 are off-limits; HEVC patent licensing rides with the OS codec vendor, see
// docs/legal-constraints.md). Per platform:
//   - Windows: read_heif() decodes through WIC (heif_document_io_win.cpp). The codecs are
//     the Microsoft Store "HEIF Image Extensions" + "HEVC Video Extensions" packages,
//     in-box on Windows 11 22H2+; missing-codec errors carry the marker prefixes below so
//     the UI can offer a Store link.
//   - macOS/Linux: read_heif() always throws; the registry-error -> QImageReader fallback
//     in load_document_from_path then decodes via Qt's qmacheif (ImageIO) / the KDE
//     runtime's kimg_heif (libheif + the ffmpeg-full Flatpak extension). The thrown
//     message is what the user sees when Qt cannot decode either.

// Lowercase extensions (no dot) routed to the HEIF reader; single source of truth for the
// registry and the file dialog filter, like raw::camera_raw_extensions().
[[nodiscard]] const std::vector<std::string>& heif_extensions();
[[nodiscard]] bool is_heif_extension(std::string_view extension);

// ISOBMFF ftyp check for HEVC-coded HEIF brands (heic/heix/mif1/msf1, ...). Deliberately
// rejects AVIF (avif/avis): the same container, but Patchy does not route .avif here.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes);

// Decodes the primary image into a single "Background" pixel-layer sRGB document (the
// flat-reader convention). Throws std::runtime_error with a user-facing message on
// failure; on non-Windows platforms it always throws (see above).
[[nodiscard]] FormatReadResult read_heif(std::span<const std::uint8_t> bytes);

// Remaps tightly packed RGBA8 pixels by an EXIF/TIFF orientation value (1-8; orientations
// 5-8 swap the output dimensions, anything out of range is treated as 1). The Windows WIC
// HEIF decoder returns unrotated pixels plus the container orientation as an EXIF-style
// value, so the reader applies this after decoding. Public because the mapping is pinned
// by codec-free unit tests on every platform.
struct OrientedImage {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> rgba;
};
[[nodiscard]] OrientedImage apply_exif_orientation(std::span<const std::uint8_t> rgba, std::int32_t width,
                                                   std::int32_t height, int orientation);

// Marker prefixes on Windows codec-missing errors. The UI strips them from the display
// text and shows an "Open Microsoft Store" button for the matching package.
inline constexpr std::string_view kHeifPackageMissingMarker = "[heif-package-missing]";
inline constexpr std::string_view kHevcPackageMissingMarker = "[hevc-package-missing]";

// Microsoft Store product ids for the packages above (ms-windows-store://pdp/?ProductId=).
inline constexpr std::string_view kHeifStoreProductId = "9PMMSR1CGPWG";  // HEIF Image Extensions, free
inline constexpr std::string_view kHevcStoreProductId = "9NMZLZ57R3T7";  // HEVC Video Extensions, paid

}  // namespace patchy::heif
