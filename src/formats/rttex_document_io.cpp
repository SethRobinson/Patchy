// miniz's zlib-compatibility aliases turn the identifier `compress` into a macro, which
// would rewrite WriteOptions::compress; keep only the mz_* names in this TU.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES

#include "formats/rttex_document_io.hpp"

#include "core/pixel_tools.hpp"
#include "formats/binary_le.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/stb/stb_image.h"
#include "support/string_utils.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace patchy::rttex {

namespace {

constexpr std::size_t kMagicSize = 6;
constexpr char kPackMagic[kMagicSize + 1] = "RTPACK";
constexpr char kTextureMagic[kMagicSize + 1] = "RTTXTR";
constexpr std::uint8_t kCompressionNone = 0;
constexpr std::uint8_t kCompressionZlib = 1;
// The PSB dimension cap every importer shares (af_document_io uses the same numbers).
constexpr std::int32_t kMaxSide = 300000;
constexpr std::uint64_t kMaxPixels = 1ULL << 28U;
// A texture bigger than this is not a texture; keeps a corrupt header from allocating
// gigabytes before the pixel guards run.
constexpr std::uint32_t kMaxDecompressedBytes = 512U * 1024U * 1024U;

JpegEncodeFn g_jpeg_encoder = nullptr;

[[nodiscard]] bool has_magic(std::span<const std::uint8_t> bytes, const char* magic) {
  return bytes.size() >= kMagicSize && std::memcmp(bytes.data(), magic, kMagicSize) == 0;
}

[[nodiscard]] bool is_power_of_two(std::int32_t value) noexcept {
  return value > 0 && (value & (value - 1)) == 0;
}

// Strips the RTPACK container. compressionType 0 is defined by Proton but never written
// (and the engine itself cannot read it); it is accepted as "stored" for leniency.
[[nodiscard]] std::vector<std::uint8_t> unwrap_rtpack(std::span<const std::uint8_t> bytes) {
  LittleEndianReader reader(bytes, "Proton texture is truncated");
  reader.skip(kMagicSize);
  (void)reader.read_u8();  // version, always 0
  (void)reader.read_u8();  // reserved
  const auto compressed_size = reader.read_u32();
  const auto decompressed_size = reader.read_u32();
  const auto compression = reader.read_u8();
  reader.skip(15);
  if (compressed_size > reader.remaining()) {
    throw std::runtime_error("Proton texture is truncated: the RTPACK header promises more data than the file holds");
  }
  if (decompressed_size < kTextureHeaderSize + kMipHeaderSize || decompressed_size > kMaxDecompressedBytes) {
    throw std::runtime_error("Proton texture has an invalid RTPACK payload size");
  }
  const auto payload = bytes.subspan(reader.position(), compressed_size);
  if (compression == kCompressionNone) {
    return std::vector<std::uint8_t>(payload.begin(), payload.end());
  }
  if (compression != kCompressionZlib) {
    throw std::runtime_error("Proton texture uses an unknown RTPACK compression type");
  }
  std::vector<std::uint8_t> inflated(decompressed_size);
  mz_ulong out_length = decompressed_size;
  if (mz_uncompress(inflated.data(), &out_length, payload.data(), static_cast<mz_ulong>(payload.size())) != MZ_OK ||
      out_length != decompressed_size) {
    throw std::runtime_error("Proton texture's zlib payload is damaged");
  }
  return inflated;
}

struct TextureHeader {
  std::int32_t height{0};
  std::int32_t width{0};
  std::int32_t format{0};
  std::int32_t original_height{0};
  std::int32_t original_width{0};
  bool uses_alpha{false};
  bool already_compressed{false};
  std::int32_t mipmap_count{0};
};

struct MipHeader {
  std::int32_t height{0};
  std::int32_t width{0};
  std::int32_t data_size{0};
  std::int32_t mip_level{0};
};

[[nodiscard]] std::uint8_t expand5(std::uint32_t value) noexcept {
  return static_cast<std::uint8_t>((value << 3U) | (value >> 2U));
}

[[nodiscard]] std::uint8_t expand6(std::uint32_t value) noexcept {
  return static_cast<std::uint8_t>((value << 2U) | (value >> 4U));
}

[[nodiscard]] std::uint8_t expand4(std::uint32_t value) noexcept {
  return static_cast<std::uint8_t>(value * 17U);
}

// Rounded quantization to `bits` bits; the reader's bit replication maps 0 and the top
// code back to exactly 0 and 255.
[[nodiscard]] std::uint32_t quantize(std::uint8_t value, unsigned bits) noexcept {
  const std::uint32_t top = (1U << bits) - 1U;
  return (static_cast<std::uint32_t>(value) * top + 127U) / 255U;
}

// Decodes mip 0 into a top-down RGBA8 buffer of the padded texture size. Raw formats are
// stored bottom-up (file row 0 is the bottom screen row), embedded JPEGs top-down.
[[nodiscard]] PixelBuffer decode_padded_rgba(const TextureHeader& header, std::span<const std::uint8_t> payload,
                                             std::vector<std::string>& notices) {
  const auto width = header.width;
  const auto height = header.height;
  PixelBuffer padded(width, height, PixelFormat::rgba8());
  {
    auto bytes = padded.data();
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
  }
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  const auto require_payload = [&](std::size_t bytes_per_pixel) {
    if (payload.size() < pixel_count * bytes_per_pixel) {
      throw std::runtime_error("Proton texture is truncated: the pixel data is shorter than the header promises");
    }
  };

  const auto copy_raw_8bit = [&](std::size_t bytes_per_pixel) {
    require_payload(bytes_per_pixel);
    for (std::int32_t y = 0; y < height; ++y) {
      const auto file_row = static_cast<std::size_t>(height - 1 - y);
      const auto* source = payload.data() + file_row * static_cast<std::size_t>(width) * bytes_per_pixel;
      auto row = padded.row(y);
      for (std::int32_t x = 0; x < width; ++x) {
        auto* destination = row.data() + static_cast<std::size_t>(x) * 4U;
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = bytes_per_pixel == 4 ? source[3] : std::uint8_t{255};
        source += bytes_per_pixel;
      }
    }
  };

  const auto copy_raw_16bit = [&](bool four_four_four_four) {
    require_payload(2);
    for (std::int32_t y = 0; y < height; ++y) {
      const auto file_row = static_cast<std::size_t>(height - 1 - y);
      const auto* source = payload.data() + file_row * static_cast<std::size_t>(width) * 2U;
      auto row = padded.row(y);
      for (std::int32_t x = 0; x < width; ++x) {
        const auto value =
            static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8U);
        auto* destination = row.data() + static_cast<std::size_t>(x) * 4U;
        if (four_four_four_four) {
          destination[0] = expand4(value >> 12U);
          destination[1] = expand4((value >> 8U) & 15U);
          destination[2] = expand4((value >> 4U) & 15U);
          destination[3] = header.uses_alpha ? expand4(value & 15U) : std::uint8_t{255};
        } else {
          destination[0] = expand5(value >> 11U);
          destination[1] = expand6((value >> 5U) & 63U);
          destination[2] = expand5(value & 31U);
          destination[3] = 255;
        }
        source += 2;
      }
    }
  };

  if (header.format >= kFormatPvrtcFirst && header.format <= kFormatPvrtcLast) {
    throw std::runtime_error(
        "PVRTC-compressed Proton textures cannot be opened; re-export the source image with RTPack -8888 first");
  }
  switch (header.format) {
    case kFormatUnsignedByte:
      copy_raw_8bit(header.uses_alpha ? 4U : 3U);
      break;
    case kFormat565:
      copy_raw_16bit(false);
      break;
    case kFormat4444:
      copy_raw_16bit(true);
      break;
    case kFormatEmbeddedFile: {
      const bool jpeg = payload.size() >= 2 && payload[0] == 0xFF && payload[1] == 0xD8;
      if (!jpeg) {
        // The engine's own rule: an "embedded" payload without the JPEG marker is raw RGB.
        if (payload.size() != pixel_count * 3U) {
          throw std::runtime_error("Proton texture's embedded payload is neither a JPEG nor raw RGB pixels");
        }
        copy_raw_8bit(3);
        break;
      }
      int decoded_width = 0;
      int decoded_height = 0;
      int components = 0;
      const auto payload_length = static_cast<int>(std::min<std::size_t>(payload.size(), 0x7fffffffU));
      if (stbi_info_from_memory(payload.data(), payload_length, &decoded_width, &decoded_height, &components) == 0 ||
          decoded_width <= 0 || decoded_height <= 0 || decoded_width > kMaxSide || decoded_height > kMaxSide ||
          static_cast<std::uint64_t>(decoded_width) * static_cast<std::uint64_t>(decoded_height) > kMaxPixels) {
        throw std::runtime_error("Proton texture's embedded JPEG could not be read");
      }
      stbi_uc* decoded =
          stbi_load_from_memory(payload.data(), payload_length, &decoded_width, &decoded_height, &components, 4);
      if (decoded == nullptr) {
        throw std::runtime_error("Proton texture's embedded JPEG could not be decoded");
      }
      // The engine trusts the JPEG's own size; copy the overlap so a mismatch cannot overrun.
      const auto copy_width = std::min(width, decoded_width);
      const auto copy_height = std::min(height, decoded_height);
      for (std::int32_t y = 0; y < copy_height; ++y) {
        std::memcpy(padded.row(y).data(),
                    decoded + static_cast<std::size_t>(y) * static_cast<std::size_t>(decoded_width) * 4U,
                    static_cast<std::size_t>(copy_width) * 4U);
      }
      stbi_image_free(decoded);
      if (decoded_width != width || decoded_height != height) {
        notices.push_back("Embedded JPEG size (" + std::to_string(decoded_width) + "x" +
                          std::to_string(decoded_height) + ") differs from the texture header (" +
                          std::to_string(width) + "x" + std::to_string(height) + ")");
      }
      break;
    }
    default:
      throw std::runtime_error("Unsupported Proton texture pixel format " + std::to_string(header.format));
  }
  return padded;
}

// Bottom-up raw rows of the padded canvas: file row r holds screen row H-1-r, so the
// padding rows (screen rows at or below the original height) come first in the file.
void append_raw_rows(std::vector<std::uint8_t>& payload, const PixelBuffer& padded, std::size_t bytes_per_pixel) {
  const auto width = padded.width();
  const auto height = padded.height();
  payload.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytes_per_pixel);
  auto* destination = payload.data();
  for (std::int32_t file_row = 0; file_row < height; ++file_row) {
    const auto row = padded.row(height - 1 - file_row);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* source = row.data() + static_cast<std::size_t>(x) * 4U;
      std::memcpy(destination, source, bytes_per_pixel);
      destination += bytes_per_pixel;
    }
  }
}

void append_raw_16bit_rows(std::vector<std::uint8_t>& payload, const PixelBuffer& padded, bool four_four_four_four) {
  const auto width = padded.width();
  const auto height = padded.height();
  payload.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2U);
  auto* destination = payload.data();
  for (std::int32_t file_row = 0; file_row < height; ++file_row) {
    const auto row = padded.row(height - 1 - file_row);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* source = row.data() + static_cast<std::size_t>(x) * 4U;
      std::uint32_t value = 0;
      if (four_four_four_four) {
        value = (quantize(source[0], 4) << 12U) | (quantize(source[1], 4) << 8U) | (quantize(source[2], 4) << 4U) |
                quantize(source[3], 4);
      } else {
        value = (quantize(source[0], 5) << 11U) | (quantize(source[1], 6) << 5U) | quantize(source[2], 5);
      }
      destination[0] = static_cast<std::uint8_t>(value & 0xffU);
      destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
      destination += 2;
    }
  }
}

}  // namespace

const std::vector<std::string>& rttex_extensions() {
  static const std::vector<std::string> extensions = {"rttex"};
  return extensions;
}

bool is_rttex_extension(std::string_view extension) {
  const auto normalized = normalized_extension(extension, false);
  const auto& extensions = rttex_extensions();
  return std::find(extensions.begin(), extensions.end(), normalized) != extensions.end();
}

bool sniff(std::span<const std::uint8_t> bytes) {
  return has_magic(bytes, kPackMagic) || has_magic(bytes, kTextureMagic);
}

std::string_view encoding_token(Encoding encoding) noexcept {
  switch (encoding) {
    case Encoding::Rgba4444:
      return "rgba4444";
    case Encoding::Jpeg:
      return "jpeg";
    case Encoding::Rgba8:
      break;
  }
  return "rgba8";
}

std::optional<Encoding> encoding_from_token(std::string_view token) noexcept {
  if (token == "rgba8") {
    return Encoding::Rgba8;
  }
  if (token == "rgba4444") {
    return Encoding::Rgba4444;
  }
  if (token == "jpeg") {
    return Encoding::Jpeg;
  }
  return std::nullopt;
}

std::string_view power_of_two_token(PowerOfTwo mode) noexcept {
  switch (mode) {
    case PowerOfTwo::Stretch:
      return "stretch";
    case PowerOfTwo::None:
      return "none";
    case PowerOfTwo::Pad:
      break;
  }
  return "pad";
}

std::optional<PowerOfTwo> power_of_two_from_token(std::string_view token) noexcept {
  if (token == "pad") {
    return PowerOfTwo::Pad;
  }
  if (token == "stretch") {
    return PowerOfTwo::Stretch;
  }
  if (token == "none") {
    return PowerOfTwo::None;
  }
  return std::nullopt;
}

void set_jpeg_encoder(JpegEncodeFn encode) {
  g_jpeg_encoder = encode;
}

bool has_jpeg_encoder() noexcept {
  return g_jpeg_encoder != nullptr;
}

std::int32_t next_power_of_two(std::int32_t value) noexcept {
  std::int32_t result = 1;
  while (result < value) {
    result <<= 1;
  }
  return result;
}

FormatReadResult read_rttex(std::span<const std::uint8_t> bytes) {
  std::vector<std::uint8_t> unwrapped;
  const bool packed = has_magic(bytes, kPackMagic);
  if (packed) {
    unwrapped = unwrap_rtpack(bytes);
    bytes = unwrapped;
  }
  if (!has_magic(bytes, kTextureMagic)) {
    if (packed) {
      throw std::runtime_error(
          "This RTPACK file is not a Proton texture (an .rtfont or .rtpak package cannot be opened as an image)");
    }
    throw std::runtime_error("Not a Proton texture: the RTTXTR header is missing");
  }

  LittleEndianReader reader(bytes, "Proton texture is truncated");
  reader.skip(kMagicSize);
  (void)reader.read_u8();  // version, always 0
  (void)reader.read_u8();  // reserved
  TextureHeader header;
  header.height = reader.read_i32();
  header.width = reader.read_i32();
  header.format = reader.read_i32();
  header.original_height = reader.read_i32();
  header.original_width = reader.read_i32();
  header.uses_alpha = reader.read_u8() != 0;
  header.already_compressed = reader.read_u8() != 0;
  reader.skip(2);   // reservedFlags
  header.mipmap_count = reader.read_i32();
  reader.skip(16 * 4);  // reserved ints

  if (header.width <= 0 || header.height <= 0 || header.width > kMaxSide || header.height > kMaxSide ||
      static_cast<std::uint64_t>(header.width) * static_cast<std::uint64_t>(header.height) > kMaxPixels) {
    throw std::runtime_error("Proton texture has an invalid size (" + std::to_string(header.width) + "x" +
                             std::to_string(header.height) + ")");
  }
  if (header.mipmap_count < 1) {
    throw std::runtime_error("Proton texture carries no mip levels");
  }

  FormatReadResult result;
  if (header.mipmap_count > 1) {
    result.notices.push_back("Only the first of " + std::to_string(header.mipmap_count) +
                             " mip levels was read; Patchy writes a single level");
  }
  // RTPack always records the true size; 0 or a value past the texture means "unpadded".
  if (header.original_width <= 0 || header.original_width > header.width) {
    result.notices.push_back("Recorded original width was invalid; using the texture width");
    header.original_width = header.width;
  }
  if (header.original_height <= 0 || header.original_height > header.height) {
    result.notices.push_back("Recorded original height was invalid; using the texture height");
    header.original_height = header.height;
  }

  MipHeader mip;
  mip.height = reader.read_i32();
  mip.width = reader.read_i32();
  mip.data_size = reader.read_i32();
  mip.mip_level = reader.read_i32();
  reader.skip(8);
  if (mip.width != header.width || mip.height != header.height) {
    throw std::runtime_error("Proton texture's first mip level does not match the texture size");
  }
  if (mip.data_size < 0 || static_cast<std::size_t>(mip.data_size) > reader.remaining()) {
    throw std::runtime_error("Proton texture is truncated: the pixel data runs past the end of the file");
  }
  const auto payload = bytes.subspan(reader.position(), static_cast<std::size_t>(mip.data_size));

  const auto padded = decode_padded_rgba(header, payload, result.notices);

  // Crop to the true size. The padding is regenerated on save, so nothing is lost.
  const auto original_width = header.original_width;
  const auto original_height = header.original_height;
  const auto format = header.uses_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  const auto channels = static_cast<std::size_t>(format.channels);
  PixelBuffer pixels(original_width, original_height, format);
  bool fully_opaque = true;
  for (std::int32_t y = 0; y < original_height; ++y) {
    const auto source_row = padded.row(y);
    auto destination_row = pixels.row(y);
    for (std::int32_t x = 0; x < original_width; ++x) {
      const auto* source = source_row.data() + static_cast<std::size_t>(x) * 4U;
      std::memcpy(destination_row.data() + static_cast<std::size_t>(x) * channels, source, channels);
      fully_opaque = fully_opaque && source[3] == 255;
    }
  }

  Document document(original_width, original_height, format);
  document.add_pixel_layer("Background", std::move(pixels));
  auto& metadata = document.metadata().values;
  metadata[kMetadataEncoding] = std::string(encoding_token(
      header.format == kFormatEmbeddedFile ? Encoding::Jpeg
      : (header.format == kFormat565 || header.format == kFormat4444) ? Encoding::Rgba4444
                                                                        : Encoding::Rgba8));
  metadata[kMetadataPowerOfTwo] =
      is_power_of_two(header.width) && is_power_of_two(header.height) ? "pad" : "none";
  metadata[kMetadataCompressed] = packed ? "1" : "0";
  if (header.uses_alpha && fully_opaque) {
    metadata[kMetadataForceAlpha] = "1";
  }
  result.document = std::move(document);
  return result;
}

FormatReadResult read_rttex_file(const std::filesystem::path& path) {
  const auto bytes = formats::read_file_bytes(path, "Proton texture");
  auto result = read_rttex(bytes);
  formats::rename_first_layer_to_stem(result.document, path);
  return result;
}

std::vector<std::uint8_t> write_rttex(const Document& document, const WriteOptions& options,
                                      std::vector<std::string>* notices) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error("Cannot write an empty document as a Proton texture");
  }
  const PixelBuffer flat = flatten_document_rgba8(document);
  if (flat.empty()) {
    throw std::runtime_error("Cannot write an empty document as a Proton texture");
  }

  bool has_alpha = options.force_alpha;
  if (!has_alpha) {
    const auto data = flat.data();
    for (std::size_t offset = 3; offset < data.size(); offset += 4) {
      if (data[offset] != 255) {
        has_alpha = true;
        break;
      }
    }
  }

  auto encoding = options.encoding;
  if (encoding == Encoding::Jpeg && has_alpha) {
    // RTPack's ultra-compress rule: JPEG only for images without transparency.
    encoding = Encoding::Rgba8;
    if (notices != nullptr) {
      notices->push_back("Texture uses transparency, so it was written as lossless RGBA instead of JPEG (RTPack's rule)");
    }
  }
  if (encoding == Encoding::Jpeg && g_jpeg_encoder == nullptr) {
    throw std::runtime_error("JPEG-encoded Proton textures need the application's JPEG encoder, which is not installed");
  }

  std::int32_t original_width = flat.width();
  std::int32_t original_height = flat.height();
  std::int32_t width = original_width;
  std::int32_t height = original_height;
  if (options.power_of_two != PowerOfTwo::None) {
    width = next_power_of_two(original_width);
    height = next_power_of_two(original_height);
  }
  if (options.force_square) {
    width = height = std::max(width, height);
  }
  if (width > kMaxSide || height > kMaxSide) {
    throw std::runtime_error("Proton texture would be " + std::to_string(width) + "x" + std::to_string(height) +
                             ", larger than the supported size");
  }

  // The padded canvas: image at the top-left, transparent black elsewhere. Stretch resamples
  // the whole image to the texture size instead and records that size as the original.
  PixelBuffer padded(width, height, PixelFormat::rgba8());
  {
    auto bytes = padded.data();
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
  }
  if (options.power_of_two == PowerOfTwo::Stretch && (width != original_width || height != original_height)) {
    padded = scale_pixels_resampled(flat, width, height);
    original_width = width;
    original_height = height;
  } else {
    for (std::int32_t y = 0; y < original_height; ++y) {
      std::memcpy(padded.row(y).data(), flat.row(y).data(), static_cast<std::size_t>(original_width) * 4U);
    }
  }

  std::int32_t format = kFormatUnsignedByte;
  bool already_compressed = false;
  std::vector<std::uint8_t> payload;
  switch (encoding) {
    case Encoding::Rgba8:
      append_raw_rows(payload, padded, has_alpha ? 4U : 3U);
      break;
    case Encoding::Rgba4444:
      format = has_alpha ? kFormat4444 : kFormat565;
      append_raw_16bit_rows(payload, padded, has_alpha);
      break;
    case Encoding::Jpeg: {
      RgbImage image;
      image.width = width;
      image.height = height;
      image.rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
      auto* destination = image.rgb.data();
      for (std::int32_t y = 0; y < height; ++y) {
        const auto row = padded.row(y);
        for (std::int32_t x = 0; x < width; ++x) {
          std::memcpy(destination, row.data() + static_cast<std::size_t>(x) * 4U, 3);
          destination += 3;
        }
      }
      payload = g_jpeg_encoder(image, std::clamp(options.jpeg_quality, 1, 100));
      if (payload.empty()) {
        throw std::runtime_error("The JPEG encoder produced no data for the Proton texture");
      }
      format = kFormatEmbeddedFile;
      already_compressed = true;
      has_alpha = false;
      break;
    }
  }

  LittleEndianWriter texture;
  texture.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(kTextureMagic), kMagicSize));
  texture.write_u8(0);  // version
  texture.write_u8(0);  // reserved
  texture.write_i32(height);
  texture.write_i32(width);
  texture.write_i32(format);
  texture.write_i32(original_height);
  texture.write_i32(original_width);
  texture.write_u8(has_alpha ? 1 : 0);
  texture.write_u8(already_compressed ? 1 : 0);
  texture.write_u8(0);
  texture.write_u8(0);
  texture.write_i32(1);  // mipmapCount
  for (int index = 0; index < 16; ++index) {
    texture.write_i32(0);
  }
  texture.write_i32(height);
  texture.write_i32(width);
  texture.write_i32(static_cast<std::int32_t>(payload.size()));
  texture.write_i32(0);  // mipLevel
  texture.write_i32(0);
  texture.write_i32(0);
  texture.write_bytes(payload);

  if (!options.compress) {
    return std::move(texture.bytes());
  }

  const auto& raw = texture.bytes();
  mz_ulong compressed_length = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<std::uint8_t> compressed(compressed_length);
  if (mz_compress(compressed.data(), &compressed_length, raw.data(), static_cast<mz_ulong>(raw.size())) != MZ_OK) {
    throw std::runtime_error("Could not compress the Proton texture");
  }
  compressed.resize(compressed_length);

  LittleEndianWriter packed;
  packed.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(kPackMagic), kMagicSize));
  packed.write_u8(0);  // version
  packed.write_u8(0);  // reserved
  packed.write_u32(static_cast<std::uint32_t>(compressed.size()));
  packed.write_u32(static_cast<std::uint32_t>(raw.size()));
  packed.write_u8(kCompressionZlib);
  for (int index = 0; index < 15; ++index) {
    packed.write_u8(0);
  }
  packed.write_bytes(compressed);
  return std::move(packed.bytes());
}

std::vector<std::uint8_t> write_rttex(const Document& document) {
  return write_rttex(document, WriteOptions{});
}

void write_rttex_file(const Document& document, const std::filesystem::path& path, const WriteOptions& options,
                      std::vector<std::string>* notices) {
  formats::write_file_bytes(path, write_rttex(document, options, notices), "Proton texture");
}

}  // namespace patchy::rttex
