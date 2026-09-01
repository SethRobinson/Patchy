#pragma once

#include "formats/format_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::jxr {

// JPEG XR (ISO/IEC 29199-2, ITU-T T.832), originally Microsoft's HD Photo / Windows Media
// Photo. TIFF-like container, magic "II" 0xBC + version. Read and write, Windows only:
// both the decoder (CLSID_WICWmpDecoder) and the encoder have shipped in-box in the
// Windows Imaging Component since Vista, so Patchy carries no JPEG XR codec of its own.
// Qt has no JPEG XR plugin, so on macOS, Linux and wasm both entry points always throw and
// the format is hidden from the save/export filters (see docs/legal-constraints.md for the
// rule against vendoring one).
//
// The format's distinguishing feature is float channels, which is why NVIDIA's in-game
// capture (GeForce Experience / NVIDIA App) writes HDR screenshots as .jxr: 32-bit float
// scRGB, or 16-bit half from the Windows Game Bar. Patchy's pipeline is 8-bit, so those
// tone map on import (see tone_map_scrgb_to_rgba8 below); integer files convert straight
// down like every other deep format.

// Lowercase extensions (no dot) routed to the JPEG XR reader; single source of truth for
// the registry, the file dialog filter and the writer branch, like heif::heif_extensions().
// ".wdp"/".hdp" are the pre-standardization HD Photo extensions the same codec handles.
[[nodiscard]] const std::vector<std::string>& jxr_extensions();
[[nodiscard]] bool is_jxr_extension(std::string_view extension);

// True when this build can actually decode and encode JPEG XR (Windows only). The Save As
// and Export filter rows are gated on this, so no platform offers a save that would always
// throw.
[[nodiscard]] bool is_available() noexcept;

// Container check: 'I' 'I' 0xBC then a nonzero version byte. Deliberately rejects TIFF,
// which shares the "II" byte order mark but carries 0x2A 0x00 instead.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes);

// Decodes the primary image into a single "Background" pixel-layer sRGB document (the
// flat-reader convention). Throws std::runtime_error with a user-facing message on failure,
// which on non-Windows platforms is always.
[[nodiscard]] FormatReadResult read_jxr(std::span<const std::uint8_t> bytes);

struct WriteOptions {
  // 1-100, mapped to WIC's ImageQuality (0.0-1.0). Ignored when lossless.
  int quality{90};
  bool lossless{false};
};

// Encodes tightly packed RGBA8 pixels. Alpha is written only when `has_alpha`; otherwise
// the frame is 24bpp BGR and the alpha bytes are ignored. ppi values at or below 0 leave
// the codec's default density. Throws on failure, always on non-Windows platforms.
[[nodiscard]] std::vector<std::uint8_t> write_jxr(std::span<const std::uint8_t> rgba, std::int32_t width,
                                                  std::int32_t height, bool has_alpha, double horizontal_ppi,
                                                  double vertical_ppi, const WriteOptions& options);
// Document overloads. The document flattens through flatten_document_rgba8, so a single
// masked layer carrying the document-alpha marker exports non-destructively like every
// other flat writer, and a fully opaque flatten writes a 24bpp frame instead of 32bpp.
// The one-argument form is the registry's FormatWriteFn (which takes only a Document) and
// uses default options; write_flat_image_file calls write_jxr_file with the user's
// quality and lossless choices instead.
[[nodiscard]] std::vector<std::uint8_t> write_jxr(const Document& document, const WriteOptions& options);
[[nodiscard]] std::vector<std::uint8_t> write_jxr(const Document& document);
void write_jxr_file(const Document& document, const std::filesystem::path& path, const WriteOptions& options = {});

// Bakes linear scRGB float pixels down to Patchy's 8-bit sRGB.
//
// scRGB is linear-light with sRGB primaries where 1.0 is the 80 nit reference white, so an
// HDR capture legitimately carries values far above 1.0 (NVIDIA reaches roughly 12.5 for a
// 1000 nit highlight) and may carry small negative components for out-of-gamut colors.
// Clamping at 1.0, which is what the 32-bit PSD import does, would flatten every highlight
// to pure white, so colors run through highlight_rolloff first. Alpha is plain linear
// coverage and does not.
//
// Public and platform-neutral so codec-free tests pin it on every platform, the way
// heif::apply_exif_orientation is. `rgba_float` is tightly packed RGBA, four floats per
// pixel. Kept as one function with named constants so an interactive develop dialog can
// later turn them into parameters without moving the math.
[[nodiscard]] std::vector<std::uint8_t> tone_map_scrgb_to_rgba8(std::span<const float> rgba_float,
                                                                std::int32_t width, std::int32_t height);

// The scalar tone curve behind the function above, exposed for its unit tests: linear scRGB
// in, display-referred linear 0..1 out, before the sRGB transfer. Identity at and below the
// knee (so ordinary SDR content converts exactly as it would without HDR in the picture),
// then a monotonic rolloff that lands the HDR ceiling on 1.0. Negatives and NaN return 0.
[[nodiscard]] float highlight_rolloff(float value);

}  // namespace patchy::jxr
