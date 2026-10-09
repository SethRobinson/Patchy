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

namespace patchy::dds {

// DirectDraw Surface (.dds), the texture container of DirectX and of most game engines. A
// 4-byte "DDS " magic, a 124-byte header (size, flags, height, width, pitch or linear
// size, depth, mip count, a 32-byte pixel format with either a FourCC or bit masks, caps),
// an optional 20-byte DX10 header (DXGI format, resource dimension, array size, alpha mode),
// then the pixels: every image (cubemap face, array element) with its whole mip chain,
// or for a volume texture every slice of each mip level in turn. Full record and the
// alpha rules: docs/dds.md.
//
// Patchy reads mip 0 of every image (faces, slices and elements become layers, the first
// visible), decodes the masked uncompressed formats generically from their bit masks, the
// block-compressed BC1-BC7 formats through the vendored bcdec, and tone maps float and
// BC6H sources through the JPEG XR HDR curve. It writes one 2D texture: uncompressed
// A8R8G8B8, BC1 (DXT1, 1-bit transparency), BC3 (DXT5), BC4 (ATI1, grayscale), BC5 (ATI2,
// red and green) with a legacy header, or BC7 under a DX10 header, each with an optional
// box-filtered mip chain.

// Session-only document metadata the reader stamps so a re-save keeps the source file's
// shape (MainWindow::image_save_defaults_for_document prefills from these, the way the
// .rttex import does). The compression token matches the saveOptions/ddsCompression
// values and is never "auto": the reader maps every source to its nearest export choice.
// Nothing persists them into any file.
inline constexpr const char* kMetadataCompression = "patchy.dds.compression";    // uncompressed|bc1|bc3|bc4|bc5|bc7
inline constexpr const char* kMetadataMipmaps = "patchy.dds.mipmaps";            // 1 when the source had a mip chain
inline constexpr const char* kMetadataSourceFormat = "patchy.dds.sourceFormat";  // informational, e.g. "DXT5"

// Wire layout, exposed so tests pin it by name. Everything is little-endian.
inline constexpr std::uint32_t kMagic = 0x20534444U;  // "DDS "
inline constexpr std::size_t kHeaderSize = 124;
inline constexpr std::size_t kPixelFormatSize = 32;
inline constexpr std::size_t kDx10HeaderSize = 20;
inline constexpr std::size_t kFileHeaderBytes = 4 + kHeaderSize;  // 128; a DX10 header follows at 128
inline constexpr std::size_t kPixelFormatOffset = 76;             // of the pixel format inside the file
inline constexpr std::int32_t kMaxSide = 16384;                   // the Direct3D 11 texture limit
inline constexpr std::uint32_t kMaxArrayOrDepth = 2048;           // Direct3D 11 array and depth limits
inline constexpr std::uint64_t kMaxPixels = 1ULL << 28U;          // per image and summed over every image

// dwFlags
inline constexpr std::uint32_t kFlagCaps = 0x1U;
inline constexpr std::uint32_t kFlagHeight = 0x2U;
inline constexpr std::uint32_t kFlagWidth = 0x4U;
inline constexpr std::uint32_t kFlagPitch = 0x8U;
inline constexpr std::uint32_t kFlagPixelFormat = 0x1000U;
inline constexpr std::uint32_t kFlagMipmapCount = 0x20000U;
inline constexpr std::uint32_t kFlagLinearSize = 0x80000U;
inline constexpr std::uint32_t kFlagDepth = 0x800000U;
// ddspf.dwFlags
inline constexpr std::uint32_t kPixelFormatAlphaPixels = 0x1U;
inline constexpr std::uint32_t kPixelFormatAlpha = 0x2U;
inline constexpr std::uint32_t kPixelFormatFourCc = 0x4U;
inline constexpr std::uint32_t kPixelFormatPaletteIndexed4 = 0x8U;
inline constexpr std::uint32_t kPixelFormatPaletteIndexed8 = 0x20U;
inline constexpr std::uint32_t kPixelFormatRgb = 0x40U;
inline constexpr std::uint32_t kPixelFormatYuv = 0x200U;
inline constexpr std::uint32_t kPixelFormatLuminance = 0x20000U;
inline constexpr std::uint32_t kPixelFormatBumpDuDv = 0x80000U;
// dwCaps / dwCaps2
inline constexpr std::uint32_t kCapsComplex = 0x8U;
inline constexpr std::uint32_t kCapsTexture = 0x1000U;
inline constexpr std::uint32_t kCapsMipmap = 0x400000U;
inline constexpr std::uint32_t kCaps2Cubemap = 0x200U;
inline constexpr std::uint32_t kCaps2CubemapPositiveX = 0x400U;  // the six face bits follow in +X -X +Y -Y +Z -Z order
inline constexpr std::uint32_t kCaps2CubemapAllFaces = 0xFC00U;
inline constexpr std::uint32_t kCaps2Volume = 0x200000U;
// DX10 header
inline constexpr std::uint32_t kResourceDimensionTexture1D = 2;
inline constexpr std::uint32_t kResourceDimensionTexture2D = 3;
inline constexpr std::uint32_t kResourceDimensionTexture3D = 4;
inline constexpr std::uint32_t kMiscFlagTextureCube = 0x4U;
inline constexpr std::uint32_t kAlphaModeMask = 0x7U;  // of miscFlags2

// FourCC codes as little-endian integers ('D','X','T','1' = 0x31545844).
[[nodiscard]] constexpr std::uint32_t fourcc(char a, char b, char c, char d) noexcept {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}
inline constexpr std::uint32_t kFourCcDxt1 = fourcc('D', 'X', 'T', '1');
inline constexpr std::uint32_t kFourCcDxt2 = fourcc('D', 'X', 'T', '2');
inline constexpr std::uint32_t kFourCcDxt3 = fourcc('D', 'X', 'T', '3');
inline constexpr std::uint32_t kFourCcDxt4 = fourcc('D', 'X', 'T', '4');
inline constexpr std::uint32_t kFourCcDxt5 = fourcc('D', 'X', 'T', '5');
inline constexpr std::uint32_t kFourCcAti1 = fourcc('A', 'T', 'I', '1');
inline constexpr std::uint32_t kFourCcAti2 = fourcc('A', 'T', 'I', '2');
inline constexpr std::uint32_t kFourCcBc4U = fourcc('B', 'C', '4', 'U');
inline constexpr std::uint32_t kFourCcBc4S = fourcc('B', 'C', '4', 'S');
inline constexpr std::uint32_t kFourCcBc5U = fourcc('B', 'C', '5', 'U');
inline constexpr std::uint32_t kFourCcBc5S = fourcc('B', 'C', '5', 'S');
inline constexpr std::uint32_t kFourCcDx10 = fourcc('D', 'X', '1', '0');

// DXGI_FORMAT values the reader understands (the enum's numeric values are the file
// contract; everything else is rejected by number).
inline constexpr std::uint32_t kDxgiR32G32B32A32Float = 2;
inline constexpr std::uint32_t kDxgiR32G32B32Float = 6;
inline constexpr std::uint32_t kDxgiR16G16B16A16Float = 10;
inline constexpr std::uint32_t kDxgiR16G16B16A16Unorm = 11;
inline constexpr std::uint32_t kDxgiR10G10B10A2Unorm = 24;
inline constexpr std::uint32_t kDxgiR8G8B8A8Unorm = 28;
inline constexpr std::uint32_t kDxgiR8G8B8A8UnormSrgb = 29;
inline constexpr std::uint32_t kDxgiR8G8Unorm = 49;
inline constexpr std::uint32_t kDxgiR8Unorm = 61;
inline constexpr std::uint32_t kDxgiA8Unorm = 65;
inline constexpr std::uint32_t kDxgiBc1Unorm = 71;
inline constexpr std::uint32_t kDxgiBc1UnormSrgb = 72;
inline constexpr std::uint32_t kDxgiBc2Unorm = 74;
inline constexpr std::uint32_t kDxgiBc2UnormSrgb = 75;
inline constexpr std::uint32_t kDxgiBc3Unorm = 77;
inline constexpr std::uint32_t kDxgiBc3UnormSrgb = 78;
inline constexpr std::uint32_t kDxgiBc4Unorm = 80;
inline constexpr std::uint32_t kDxgiBc4Snorm = 81;
inline constexpr std::uint32_t kDxgiBc5Unorm = 83;
inline constexpr std::uint32_t kDxgiBc5Snorm = 84;
inline constexpr std::uint32_t kDxgiB5G6R5Unorm = 85;
inline constexpr std::uint32_t kDxgiB5G5R5A1Unorm = 86;
inline constexpr std::uint32_t kDxgiB8G8R8A8Unorm = 87;
inline constexpr std::uint32_t kDxgiB8G8R8X8Unorm = 88;
inline constexpr std::uint32_t kDxgiB8G8R8A8UnormSrgb = 91;
inline constexpr std::uint32_t kDxgiB8G8R8X8UnormSrgb = 93;
inline constexpr std::uint32_t kDxgiBc6hUf16 = 95;
inline constexpr std::uint32_t kDxgiBc6hSf16 = 96;
inline constexpr std::uint32_t kDxgiBc7Unorm = 98;
inline constexpr std::uint32_t kDxgiBc7UnormSrgb = 99;
inline constexpr std::uint32_t kDxgiB4G4R4A4Unorm = 115;

// Lowercase extension (no dot); single source of truth for the registry, the dialog
// filter table, and the writer branch, like rttex::rttex_extensions().
[[nodiscard]] const std::vector<std::string>& dds_extensions();
[[nodiscard]] bool is_dds_extension(std::string_view extension);

// "DDS " magic, header size 124 and pixel-format size 32: the three fields every writer
// agrees on.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes);

// The decoded header, field by field (never a native struct), exposed so tests pin the
// parse by name.
struct PixelFormatDescriptor {
  std::uint32_t flags{0};
  std::uint32_t fourcc{0};
  std::uint32_t rgb_bit_count{0};
  std::uint32_t r_mask{0};
  std::uint32_t g_mask{0};
  std::uint32_t b_mask{0};
  std::uint32_t a_mask{0};
};

struct Dx10Header {
  std::uint32_t dxgi_format{0};
  std::uint32_t resource_dimension{0};
  std::uint32_t misc_flag{0};
  std::uint32_t array_size{1};
  std::uint32_t misc_flags2{0};
};

struct Header {
  std::uint32_t flags{0};
  std::uint32_t height{0};
  std::uint32_t width{0};
  std::uint32_t pitch_or_linear_size{0};  // ignored by the reader: legacy writers lie, sizes come from the format
  std::uint32_t depth{1};
  std::uint32_t mipmap_count{1};  // 0 in the file reads as 1
  std::uint32_t caps{0};
  std::uint32_t caps2{0};
  PixelFormatDescriptor pixel_format;
  std::optional<Dx10Header> dx10;
  std::size_t data_offset{kFileHeaderBytes};  // 148 with a DX10 header
};

// Validates the magic, both size fields, the dimensions (1..kMaxSide per side, kMaxPixels
// per image and summed over faces, slices and elements), the mip count, and the depth or
// array size, all before any pixel allocation. Throws std::runtime_error with a user-facing
// message.
[[nodiscard]] Header parse_header(std::span<const std::uint8_t> bytes);

// How the file wants its alpha read (the DX10 header's miscFlags2, or the DXT2/DXT4 FourCC).
enum class AlphaMode { Unknown, Straight, Premultiplied, Opaque, Custom };
enum class TextureLayout { Texture2D, Cubemap, Volume, Array };
[[nodiscard]] TextureLayout texture_layout(const Header& header) noexcept;
[[nodiscard]] AlphaMode alpha_mode(const Header& header) noexcept;
// Faces, slices or elements at mip 0 (1 for a plain 2D texture).
[[nodiscard]] std::uint32_t image_count(const Header& header) noexcept;

// One resolved pixel layout the decoder understands: a legacy masked format, a legacy
// FourCC block format, or a DXGI format.
struct SourceFormat {
  enum class Kind { Masked, Rgba16, Float, Bc1, Bc2, Bc3, Bc4, Bc5, Bc6h, Bc7 };
  Kind kind{Kind::Masked};
  std::string name;              // the kMetadataSourceFormat token, e.g. "A8R8G8B8", "DXT5", "BC7_UNORM"
  std::uint32_t bits_per_pixel{0};  // uncompressed kinds only
  PixelFormatDescriptor masks;      // Masked only (DXGI uncompressed formats resolve to masks too)
  bool has_alpha{false};
  bool is_signed{false};         // BC4/BC5 SNORM, BC6H SF16
  bool premultiplied{false};     // DXT2 / DXT4
  bool luminance{false};         // Masked: the R mask is a luminance plane replicated to RGB
  bool alpha_only{false};        // Masked: no colour masks at all (A8); RGB read as white
  std::uint32_t float_channels{0};  // Float: 3 or 4 components of `float_bits` bits
  std::uint32_t float_bits{0};      // 16 or 32
};
// Throws for formats Patchy cannot decode (YUV, palettized, bump maps, typeless DXGI, ...).
[[nodiscard]] SourceFormat resolve_source_format(const Header& header);
// Bytes one image of `width` x `height` occupies in `format` (block formats round up to
// whole 4x4 blocks); 64-bit so corrupt sizes cannot overflow.
[[nodiscard]] std::uint64_t image_byte_size(const SourceFormat& format, std::uint32_t width, std::uint32_t height) noexcept;
// Bytes of a whole mip chain of `levels` levels starting at `width` x `height`.
[[nodiscard]] std::uint64_t mip_chain_byte_size(const SourceFormat& format, std::uint32_t width, std::uint32_t height,
                                                std::uint32_t levels) noexcept;

// Decodes mip 0 of every image into pixel layers (rgba8 when the format carries alpha,
// rgb8 otherwise, so the UI's document-alpha promotion sees exactly the alpha-bearing
// files). A single 2D texture gives one "Background" layer; cubemap faces are named
// "+X" .. "-Z", volume slices "Slice N" and array elements "Element N", only the first
// visible. Premultiplied sources are converted to straight alpha, float and BC6H sources
// are tone mapped. Throws std::runtime_error with a user-facing message for unsupported
// formats, truncation and absurd sizes.
[[nodiscard]] FormatReadResult read_dds(std::span<const std::uint8_t> bytes);
// File form: reads the bytes and renames the layer to the file stem, the flat-format
// convention shared by TGA, PCX and rttex.
[[nodiscard]] FormatReadResult read_dds_file(const std::filesystem::path& path);

// Automatic: BC1 when every flattened pixel is opaque, BC3 otherwise (what the NVIDIA
// tools pick). Uncompressed: 32-bit A8R8G8B8, lossless. Bc1: DXT1 with 1-bit transparency
// (alpha below 128 cuts the texel out). Bc3: DXT5 with 8-bit interpolated alpha. Bc4: ATI1,
// one channel (the image's luminance), opaque. Bc5: ATI2, the red and green channels only
// (normal maps), opaque. Bc7: a DX10 header with DXGI 98, full RGBA at the highest quality
// (the vendored bc7enc, modes 1 and 6). Append-only.
enum class Compression { Automatic, Uncompressed, Bc1, Bc3, Bc4, Bc5, Bc7 };

struct WriteOptions {
  Compression compression{Compression::Automatic};
  bool generate_mipmaps{false};  // a box-filtered chain down to 1x1
};

// The settings/metadata tokens ("auto"|"uncompressed"|"bc1"|"bc3"|"bc4"|"bc5"|"bc7").
// Compatibility contracts: never renamed.
[[nodiscard]] std::string_view compression_token(Compression compression) noexcept;
[[nodiscard]] std::optional<Compression> compression_from_token(std::string_view token) noexcept;

// Flattens through flatten_document_rgba8 (so a single masked layer carrying the
// document-alpha marker exports non-destructively) and writes one 2D texture with a
// legacy header. `notices` receives the BC1 cut-out count when a translucent image was
// written with 1-bit alpha. Throws on an empty document.
[[nodiscard]] std::vector<std::uint8_t> write_dds(const Document& document, const WriteOptions& options,
                                                  std::vector<std::string>* notices = nullptr);
// The registry's FormatWriteFn: default options.
[[nodiscard]] std::vector<std::uint8_t> write_dds(const Document& document);
void write_dds_file(const Document& document, const std::filesystem::path& path, const WriteOptions& options = {},
                    std::vector<std::string>* notices = nullptr);

// Deterministic building blocks, public so tests pin them directly.

// Straight alpha from premultiplied: a == 0 gives black, otherwise
// c' = min(255, (c * 255 + a / 2) / a).
void unpremultiply_rgba8_in_place(std::span<std::uint8_t> rgba);
// One mip level down: each side halves (never below 1); the last destination column and
// row absorb an odd leftover source column or row. Colours average alpha-weighted
// ((sum c*a + sum a / 2) / sum a, or the plain rounded mean when every alpha is 0), alpha
// averages plainly with rounding.
[[nodiscard]] PixelBuffer box_downsample(const PixelBuffer& rgba8);
// Level 0 (a copy) down to 1x1.
[[nodiscard]] std::vector<PixelBuffer> generate_mip_chain(const PixelBuffer& rgba8);
// 1 + floor(log2(max(width, height))).
[[nodiscard]] std::uint32_t mip_count_for(std::int32_t width, std::int32_t height) noexcept;
// 4x4 RGBA8 texels, row-major, 64 bytes in. `punch_through` selects the 3-colour BC1 mode
// where every texel with alpha below 128 becomes transparent (Patchy's own range-fit
// encoder); otherwise stb_dxt's 4-colour encoder runs and alpha is ignored.
void encode_bc1_block(const std::uint8_t* rgba, bool punch_through, std::uint8_t* out);
// stb_dxt's BC3 block: 8 alpha bytes then the 8-byte colour block.
void encode_bc3_block(const std::uint8_t* rgba, std::uint8_t* out);
// stb_dxt's BC4 block (8 bytes) of the texels' luminance (Rec. 601 integer weights), and its
// BC5 block (16 bytes) of the red then the green channel.
void encode_bc4_block(const std::uint8_t* rgba, std::uint8_t* out);
void encode_bc5_block(const std::uint8_t* rgba, std::uint8_t* out);
// bc7enc's BC7 block (16 bytes): mode 6 for blocks with alpha, modes 1 or 6 otherwise.
void encode_bc7_block(const std::uint8_t* rgba, std::uint8_t* out);
// Rec. 601 luminance with integer weights, (r * 299 + g * 587 + b * 114 + 500) / 1000.
[[nodiscard]] std::uint8_t luminance8(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept;
// True when any of the 16 texels has alpha below 128.
[[nodiscard]] bool bc1_block_has_cutout(const std::uint8_t* rgba) noexcept;
// Bit-exact IEEE binary16 to binary32 (subnormals, infinities and NaN included).
[[nodiscard]] float half_to_float(std::uint16_t half) noexcept;

}  // namespace patchy::dds
