#pragma once

#include "formats/format_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::rttex {

// Proton SDK texture (.rttex), Seth Robinson's engine format. A 100-byte "RTTXTR" header
// records the padded, power-of-two texture size the GPU wanted AND the image's true size,
// then one 24-byte mip header and the pixels: raw RGBA8888/RGB888, RGBA4444, RGB565, or
// an embedded JPEG file. The whole file is usually wrapped in a 32-byte "RTPACK" zlib
// container, which RTPack adds in a second pass. Raw pixels are stored bottom-up with the
// image anchored at the top-left of the padded canvas, so in file order the padding rows
// come first. Full record, RTPack parity table, and the census behind the rules below:
// docs/rttex.md.
//
// Patchy opens a texture at its TRUE size (the padding is cropped, then regenerated on
// save) and writes exactly what RTPack writes: mip count 1, bottom-up raw rows, an
// embedded JPEG only when the image has no transparency (RTPack's own rule), and the
// RTPACK wrapper unless compression is turned off. PVRTC payloads are rejected.

// Session-only document metadata the reader stamps so a re-save keeps the source file's
// settings (MainWindow::image_save_defaults_for_document prefills from these, the same way
// a .cur import prefills its hotspot). The tokens match the saveOptions/rttex* settings
// values. Nothing persists them into any file.
inline constexpr const char* kMetadataEncoding = "patchy.rttex.encoding";      // rgba8|rgba4444|jpeg
inline constexpr const char* kMetadataPowerOfTwo = "patchy.rttex.powerOfTwo";  // pad|none
inline constexpr const char* kMetadataCompressed = "patchy.rttex.compressed";  // 1|0
inline constexpr const char* kMetadataForceAlpha = "patchy.rttex.forceAlpha";  // 1 when RGBA yet fully opaque

// Wire layout, exposed so tests pin it by name. Everything is little-endian.
inline constexpr std::size_t kRtpackHeaderSize = 32;   // "RTPACK", version, reserved, sizes, type, pad
inline constexpr std::size_t kTextureHeaderSize = 100; // "RTTXTR" + fields + 16 reserved ints
inline constexpr std::size_t kMipHeaderSize = 24;      // height, width, dataSize, mipLevel, 2 reserved
inline constexpr std::int32_t kFormatUnsignedByte = 5121;   // GL_UNSIGNED_BYTE: RGB or RGBA by bUsesAlpha
inline constexpr std::int32_t kFormat565 = 33635;           // GL_UNSIGNED_SHORT_5_6_5
inline constexpr std::int32_t kFormat4444 = 32819;          // GL_UNSIGNED_SHORT_4_4_4_4
inline constexpr std::int32_t kFormatPvrtcFirst = 35840;    // GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
inline constexpr std::int32_t kFormatPvrtcLast = 35843;     // GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG
inline constexpr std::int32_t kFormatEmbeddedFile = 20000000;  // RT_FORMAT_EMBEDDED_FILE (a JPEG)

// Lowercase extension (no dot); single source of truth for the registry, the dialog
// filter table, and the writer branch, like jxr::jxr_extensions().
[[nodiscard]] const std::vector<std::string>& rttex_extensions();
[[nodiscard]] bool is_rttex_extension(std::string_view extension);

// "RTPACK" (the compressed wrapper) or a bare "RTTXTR" at offset 0. The wrapper's payload
// cannot be checked without inflating, so an RTPACK-wrapped .rtfont sniffs true and
// read_rttex then reports it as not a texture.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes);

// Decodes mip level 0 into a single "Background" pixel layer at the texture's ORIGINAL
// size (rgb8 when the file carries no alpha, else rgba8). 16-bit formats expand to 8 bits;
// extra mip levels are skipped with a notice. Throws std::runtime_error with a user-facing
// message for PVRTC, unknown formats, non-texture RTPACK payloads, truncation, and absurd
// sizes.
[[nodiscard]] FormatReadResult read_rttex(std::span<const std::uint8_t> bytes);
// File form: reads the bytes and renames the layer to the file stem, the flat-format
// convention shared by TGA and PCX.
[[nodiscard]] FormatReadResult read_rttex_file(const std::filesystem::path& path);

// Rgba8 writes RGBA8888, or RGB888 when the image has no alpha; Rgba4444 writes RGBA4444,
// or RGB565 when the image has no alpha; Jpeg embeds a JPEG at the padded size (alpha-free
// images only, see write_rttex).
enum class Encoding { Rgba8, Rgba4444, Jpeg };
// Pad: RTPack's default, the image sits top-left in a power-of-two canvas and the header
// records the true size. Stretch: resample to the power-of-two size (the recorded original
// size then equals the texture size, as RTPack -stretch does). None: RTPack -nopowerof2.
enum class PowerOfTwo { Pad, Stretch, None };

struct WriteOptions {
  Encoding encoding{Encoding::Rgba8};
  int jpeg_quality{90};  // 1-100, Jpeg only
  PowerOfTwo power_of_two{PowerOfTwo::Pad};
  bool force_square{false};  // RTPack -force_square: both axes become the larger one
  bool force_alpha{false};   // RTPack -force_alpha: keep RGBA even when every pixel is opaque
  bool compress{true};       // wrap in the RTPACK zlib container (RTPack's second pass)
};

// The settings/metadata tokens for the two enums ("rgba8"|"rgba4444"|"jpeg" and
// "pad"|"stretch"|"none"). Compatibility contracts: never renamed.
[[nodiscard]] std::string_view encoding_token(Encoding encoding) noexcept;
[[nodiscard]] std::optional<Encoding> encoding_from_token(std::string_view token) noexcept;
[[nodiscard]] std::string_view power_of_two_token(PowerOfTwo mode) noexcept;
[[nodiscard]] std::optional<PowerOfTwo> power_of_two_from_token(std::string_view token) noexcept;

// The formats library is Qt-free, so the JPEG encoder is injected by the UI layer once at
// startup (ui::install_rttex_jpeg_codec, a capture-free lambda over QImageWriter), the way
// ico::set_png_codec works. Input is tightly packed RGB8, top-down, at the padded texture
// size; the result is a complete JPEG file. With no encoder installed, Encoding::Jpeg
// throws.
struct RgbImage {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> rgb;  // 3 bytes per pixel, row-major, top-down
};
using JpegEncodeFn = std::vector<std::uint8_t> (*)(const RgbImage& image, int quality);
void set_jpeg_encoder(JpegEncodeFn encode);
[[nodiscard]] bool has_jpeg_encoder() noexcept;

// Flattens through flatten_document_rgba8 (so a single masked layer carrying the
// document-alpha marker exports non-destructively) and writes one mip level. Alpha is
// auto-detected like RTPack: any pixel below 255 keeps the channel, otherwise it is dropped
// unless force_alpha. Encoding::Jpeg on an image WITH alpha falls back to lossless RGBA
// (RTPack's ultra-compress rule) and reports why through `notices`. Throws on an empty
// document or a missing JPEG encoder.
[[nodiscard]] std::vector<std::uint8_t> write_rttex(const Document& document, const WriteOptions& options,
                                                    std::vector<std::string>* notices = nullptr);
// The registry's FormatWriteFn: default options.
[[nodiscard]] std::vector<std::uint8_t> write_rttex(const Document& document);
void write_rttex_file(const Document& document, const std::filesystem::path& path,
                      const WriteOptions& options = {}, std::vector<std::string>* notices = nullptr);

// RTPack's rule: the smallest power of two at or above `value` (a power of two stays).
[[nodiscard]] std::int32_t next_power_of_two(std::int32_t value) noexcept;

}  // namespace patchy::rttex
