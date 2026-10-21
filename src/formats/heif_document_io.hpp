#pragma once

#include "formats/format_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::heif {

// HEIF/HEIC still images (iPhone .heic, Sony/Fujifilm .hif). Decode-only. Patchy must
// never ship an HEVC decoder or encoder; compressed pictures always go to a decoder
// supplied by the platform or browser (see docs/legal-constraints.md). Per platform:
//   - Windows: read_heif() decodes through WIC (heif_document_io_win.cpp). The codecs are
//     the Microsoft Store "HEIF Image Extensions" + "HEVC Video Extensions" packages,
//     in-box on Windows 11 22H2+; missing-codec errors carry the marker prefixes below so
//     the UI can offer a Store link.
//   - WebAssembly browser app: a WASM-only libheif build parses the container and invokes
//     the browser's HEVC VideoDecoder through WebCodecs. No software HEVC codec or encoder
//     is compiled into the application.
//   - Linux (native builds and the AppImage): read_heif() loads the distribution's own
//     libheif with dlopen at run time (heif_document_io_linux.cpp); nothing is linked or
//     bundled, and the HEVC decoder is the distro's plug-in package. The thrown message
//     names the package to install when the library or its HEVC plug-in is missing.
//   - macOS: read_heif() decodes through ImageIO (heif_document_io_mac.cpp) only files that
//     need more than 8 bits (10-bit captures, PQ and HLG HDR) while deep editing is on; it
//     throws for everything else, so 8-bit files keep the QImageReader fallback below.
//   - Linux inside the Flatpak: read_heif() always throws; the registry-error ->
//     QImageReader fallback in load_document_from_path then decodes via Qt's qmacheif
//     (ImageIO, macOS) / the KDE runtime's kimg_heif (libheif + the runtime's codecs-extra
//     extension). The thrown message is what the user sees when Qt cannot decode either.

// Lowercase extensions (no dot) routed to the HEIF reader; single source of truth for the
// registry and the file dialog filter, like raw::camera_raw_extensions().
[[nodiscard]] const std::vector<std::string>& heif_extensions();
[[nodiscard]] bool is_heif_extension(std::string_view extension);

// ISOBMFF ftyp check for HEVC-coded HEIF brands (heic/heix/mif1/msf1, ...). Deliberately
// rejects AVIF (avif/avis): the same container, but Patchy does not route .avif here.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes);

// Decodes the primary image into a single "Background" pixel-layer sRGB document (the
// flat-reader convention). Throws std::runtime_error with a user-facing message on
// failure; on macOS/Linux and the node-only wasm-core build it always throws (see above).
[[nodiscard]] FormatReadResult read_heif(std::span<const std::uint8_t> bytes);

// The transfer_characteristics code (ISO/IEC 23091-2: 16 = PQ, 18 = HLG) of the first nclx
// colour box inside the file's top-level meta box, or nothing when there is none. Windows
// needs it because WIC reports HDR frames only as float and maps HLG its own way.
[[nodiscard]] std::optional<std::uint16_t> nclx_transfer_characteristics(std::span<const std::uint8_t> bytes);

// Remaps tightly packed pixels (RGBA8 by default, any `bytes_per_pixel` for deeper ones) by
// an EXIF/TIFF orientation value (1-8; orientations 5-8 swap the output dimensions,
// anything out of range is treated as 1). The Windows WIC HEIF decoder returns unrotated
// pixels plus the container orientation as an EXIF-style value, and so does ImageIO on
// macOS, so the readers apply this after decoding. Public because the mapping is pinned by
// codec-free unit tests on every platform.
struct OrientedImage {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> rgba;  // tightly packed pixels of the input's size
};
[[nodiscard]] OrientedImage apply_exif_orientation(std::span<const std::uint8_t> rgba, std::int32_t width,
                                                   std::int32_t height, int orientation,
                                                   std::size_t bytes_per_pixel = 4);

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
// Linux outside the Flatpak: whether the distribution's libheif loaded and exposes the
// symbols read_heif needs (heif_document_io_linux.cpp). The Open dialog's HEIF row keys
// on it. The PATCHY_HEIF_LIBRARY environment variable replaces the library name or path
// the loader tries ("libheif.so.1", then "libheif.so"); tests point it at a missing file.
[[nodiscard]] bool host_libheif_available();
// Empty when the library loaded; otherwise the user-facing message read_heif throws
// (which package to install, or that the library is too old).
[[nodiscard]] std::string host_libheif_load_error();
#endif

// Marker prefixes on Windows codec-missing errors. The UI strips them from the display
// text and shows an "Open Microsoft Store" button for the matching package.
inline constexpr std::string_view kHeifPackageMissingMarker = "[heif-package-missing]";
inline constexpr std::string_view kHevcPackageMissingMarker = "[hevc-package-missing]";
inline constexpr std::string_view kBrowserHevcUnavailableMarker = "[browser-hevc-unavailable]";
inline constexpr std::string_view kBrowserHeifDecodeFailedMarker = "[browser-heif-decode-failed]";

// Microsoft Store product ids for the packages above (ms-windows-store://pdp/?ProductId=).
inline constexpr std::string_view kHeifStoreProductId = "9PMMSR1CGPWG";  // HEIF Image Extensions, free
inline constexpr std::string_view kHevcStoreProductId = "9NMZLZ57R3T7";  // HEVC Video Extensions, paid

}  // namespace patchy::heif
