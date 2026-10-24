#include "formats/image_open_diagnosis.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>

namespace patchy::formats {

namespace {

using Reason = UndecodableImageReason;
// Signatures with embedded NUL bytes need the sv suffix: a plain literal would convert to
// a string_view that stops at the first NUL (and an empty one matches everything).
using namespace std::string_view_literals;

[[nodiscard]] bool bytes_at(std::span<const std::uint8_t> bytes, std::size_t offset, std::string_view literal) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < literal.size()) {
    return false;
  }
  return std::memcmp(bytes.data() + offset, literal.data(), literal.size()) == 0;
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset, bool little_endian) noexcept {
  const auto a = static_cast<std::uint16_t>(bytes[offset]);
  const auto b = static_cast<std::uint16_t>(bytes[offset + 1]);
  return little_endian ? static_cast<std::uint16_t>(a | (b << 8)) : static_cast<std::uint16_t>((a << 8) | b);
}

[[nodiscard]] std::uint64_t read_uint(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t width,
                                      bool little_endian) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < width; ++i) {
    const auto byte = static_cast<std::uint64_t>(bytes[offset + (little_endian ? width - 1 - i : i)]);
    value = (value << 8) | byte;
  }
  return value;
}

[[nodiscard]] std::string lower_ascii(std::string_view text) {
  std::string out(text);
  for (auto& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// Extensions that name the same container share a family, so a .jpeg file with JPEG
// bytes is not "a different format".
[[nodiscard]] std::string extension_family(std::string_view extension) {
  const auto ext = lower_ascii(extension);
  if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif" || ext == "jif") return "jpeg";
  if (ext == "png" || ext == "apng") return "png";
  if (ext == "tif" || ext == "tiff") return "tiff";
  if (ext == "heic" || ext == "heif" || ext == "hif") return "heic";
  if (ext == "avif" || ext == "avifs") return "avif";
  if (ext == "psd" || ext == "psb") return "psd";
  if (ext == "jp2" || ext == "j2k" || ext == "jpx" || ext == "jpf") return "jp2";
  return ext;
}

struct Detected {
  const char* name;       // display name
  const char* extension;  // the extension Patchy opens it under; "" when it cannot open it
  const char* family;     // compared with extension_family()
};

[[nodiscard]] std::optional<Detected> detect_container(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes_at(bytes, 0, "\x89PNG\r\n\x1a\n")) return Detected{"PNG", "png", "png"};
  if (bytes_at(bytes, 0, "GIF87a") || bytes_at(bytes, 0, "GIF89a")) return Detected{"GIF", "gif", "gif"};
  if (bytes_at(bytes, 0, "RIFF") && bytes_at(bytes, 8, "WEBP")) return Detected{"WebP", "webp", "webp"};
  if (bytes_at(bytes, 0, "II*\0"sv) || bytes_at(bytes, 0, "MM\0*"sv) || bytes_at(bytes, 0, "II+\0"sv) ||
      bytes_at(bytes, 0, "MM\0+"sv)) {
    return Detected{"TIFF", "tif", "tiff"};
  }
  if (bytes_at(bytes, 0, "\xff\xd8\xff")) return Detected{"JPEG", "jpg", "jpeg"};
  if (bytes_at(bytes, 4, "ftyp") && bytes.size() >= 12) {
    const std::string_view brand(reinterpret_cast<const char*>(bytes.data() + 8), 4);
    if (brand == "avif" || brand == "avis") return Detected{"AVIF", "avif", "avif"};
    if (brand == "heic" || brand == "heix" || brand == "hevc" || brand == "hevx" || brand == "heim" ||
        brand == "heis" || brand == "mif1" || brand == "msf1") {
      return Detected{"HEIC", "heic", "heic"};
    }
    if (brand == "jxl ") return Detected{"JPEG XL", "jxl", "jxl"};
    if (brand == "jp2 ") return Detected{"JPEG 2000", "", "jp2"};
    return Detected{"video or other ISO media", "", "isobmff"};
  }
  if (bytes_at(bytes, 0, "\xff\x0a") || bytes_at(bytes, 0, "\0\0\0\x0cJXL \r\n\x87\n"sv)) {
    return Detected{"JPEG XL", "jxl", "jxl"};
  }
  if (bytes_at(bytes, 0, "\0\0\0\x0cjP  \r\n\x87\n"sv) || bytes_at(bytes, 0, "\xff\x4f\xff\x51")) {
    return Detected{"JPEG 2000", "", "jp2"};
  }
  if (bytes_at(bytes, 0, "qoif")) return Detected{"QOI", "", "qoi"};
  if (bytes_at(bytes, 0, "#?RADIANCE") || bytes_at(bytes, 0, "#?RGBE")) return Detected{"Radiance HDR", "", "hdr"};
  if (bytes_at(bytes, 0, "\x76\x2f\x31\x01")) return Detected{"OpenEXR", "", "exr"};
  if (bytes_at(bytes, 0, "\x01\xda")) return Detected{"SGI", "", "sgi"};
  if (bytes_at(bytes, 0, "RIFF") && bytes_at(bytes, 8, "ACON")) return Detected{"animated cursor", "", "ani"};
  if (bytes_at(bytes, 0, "8BPS")) return Detected{"Photoshop", "psd", "psd"};
  if (bytes_at(bytes, 0, "PDN3")) return Detected{"Paint.NET", "pdn", "pdn"};
  if (bytes_at(bytes, 0, "%PDF")) return Detected{"PDF", "pdf", "pdf"};
  if (bytes_at(bytes, 0, "DDS ")) return Detected{"DDS", "dds", "dds"};
  if (bytes_at(bytes, 0, "gimp xcf")) return Detected{"GIMP XCF", "", "xcf"};
  if (bytes_at(bytes, 0, "BM") && bytes.size() >= 14) return Detected{"BMP", "bmp", "bmp"};
  // OpenRaster: a zip whose first entry is the stored "mimetype" (docs/ora.md).
  if (bytes_at(bytes, 0, "PK\x03\x04") && bytes_at(bytes, 30, "mimetypeimage/openraster")) {
    return Detected{"OpenRaster", "ora", "ora"};
  }
  if (bytes_at(bytes, 0, "PK\x03\x04")) return Detected{"ZIP archive", "", "zip"};
  if (bytes_at(bytes, 0, "Rar!")) return Detected{"RAR archive", "", "rar"};
  if (bytes_at(bytes, 0, "7z\xbc\xaf")) return Detected{"7-Zip archive", "", "7z"};
  if (bytes_at(bytes, 0, "\x1f\x8b")) return Detected{"gzip archive", "", "gz"};
  if (bytes_at(bytes, 0, "MZ")) return Detected{"Windows program", "", "exe"};
  // SVG: an XML document whose root element is svg, within the first kilobyte.
  const auto head_size = std::min<std::size_t>(bytes.size(), 1024);
  const std::string_view head(reinterpret_cast<const char*>(bytes.data()), head_size);
  if ((head.starts_with("<?xml") || head.starts_with("<svg") || head.starts_with("<!DOCTYPE svg")) &&
      head.find("<svg") != std::string_view::npos) {
    return Detected{"SVG", "svg", "svg"};
  }
  return std::nullopt;
}

// Printable text (ASCII or UTF-8) with no NUL byte in its first 512 bytes.
[[nodiscard]] bool looks_like_text(std::span<const std::uint8_t> bytes) noexcept {
  const auto size = std::min<std::size_t>(bytes.size(), 512);
  std::size_t offset = 0;
  if (bytes_at(bytes, 0, "\xef\xbb\xbf")) {
    offset = 3;
  }
  for (std::size_t i = offset; i < size; ++i) {
    const auto byte = bytes[i];
    if (byte == 0) {
      return false;
    }
    if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f') {
      return false;
    }
  }
  return size > offset;
}

[[nodiscard]] Reason diagnose_jpeg(std::span<const std::uint8_t> bytes) noexcept {
  std::size_t offset = 2;  // past SOI
  bool saw_frame = false;
  while (offset + 1 < bytes.size()) {
    if (bytes[offset] != 0xff) {
      // Not a marker where one is due: the header is damaged.
      return saw_frame ? Reason::JpegTruncated : Reason::JpegNoFrame;
    }
    while (offset + 1 < bytes.size() && bytes[offset + 1] == 0xff) {
      ++offset;  // fill bytes
    }
    if (offset + 1 >= bytes.size()) {
      break;
    }
    const auto marker = bytes[offset + 1];
    if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
      offset += 2;  // standalone markers
      continue;
    }
    if (marker == 0xd9) {
      return saw_frame ? Reason::Unknown : Reason::JpegNoFrame;  // EOI before any scan
    }
    if (marker == 0xda) {
      if (!saw_frame) {
        return Reason::JpegNoFrame;
      }
      // Scan data follows: the stream must still carry an EOI somewhere after it.
      for (std::size_t i = offset + 2; i + 1 < bytes.size(); ++i) {
        if (bytes[i] == 0xff && bytes[i + 1] == 0xd9) {
          return Reason::Unknown;
        }
      }
      return Reason::JpegTruncated;
    }
    if (offset + 3 >= bytes.size()) {
      return Reason::JpegTruncated;
    }
    const auto length = static_cast<std::size_t>(read_u16(bytes, offset + 2, false));
    if (length < 2 || offset + 2 + length > bytes.size()) {
      return Reason::JpegTruncated;
    }
    const bool frame_marker = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
    if (frame_marker) {
      if (marker == 0xc3 || marker == 0xc7 || marker == 0xcb || marker == 0xcf) {
        return Reason::JpegLossless;
      }
      if (marker == 0xc5 || marker == 0xc6 || marker == 0xcd || marker == 0xce) {
        return Reason::JpegHierarchical;
      }
      if (length >= 3 && bytes[offset + 4] != 8) {
        return Reason::Jpeg12Bit;
      }
      saw_frame = true;
    }
    offset += 2 + length;
  }
  return saw_frame ? Reason::JpegTruncated : Reason::JpegNoFrame;
}

struct TiffCodec {
  std::uint16_t code;
  const char* name;
  bool supported;  // by the libtiff Qt bundles, which carries no external codecs
};

constexpr TiffCodec kTiffCodecs[] = {
    {1, "no", true},
    {2, "CCITT RLE", true},
    {3, "CCITT T.4", true},
    {4, "CCITT T.6", true},
    {5, "LZW", true},
    {6, "JPEG", false},  // old-style JPEG (TIFF 6.0 section 22), which nothing decodes any more
    {7, "JPEG", true},   // Patchy decodes these itself (src/ui/tiff_jpeg_reader.cpp)
    {8, "Deflate", true},
    {9, "JBIG (T.85)", false},
    {10, "JBIG (T.43)", false},
    {32766, "NeXT", true},
    {32771, "CCITT RLE", true},
    {32773, "PackBits", true},
    {32809, "ThunderScan", true},
    {32895, "IT8 CT", false},
    {32896, "IT8 LW", false},
    {32897, "IT8 MP", false},
    {32898, "IT8 BL", false},
    {32908, "Pixar Film", false},
    {32909, "Pixar Log", false},
    {32946, "Deflate", true},
    {32947, "Kodak DCS", false},
    {34661, "JBIG", false},
    {34676, "SGI Log", true},
    {34677, "SGI Log 24", true},
    {34712, "JPEG 2000", false},
    {34887, "LERC", false},
    {34925, "LZMA", false},
    {34933, "PNG", false},
    {34934, "JPEG XR", false},
    {50000, "ZSTD", false},
    {50001, "WebP", false},
    {50002, "JPEG XL", false},
};

[[nodiscard]] UndecodableImageDiagnosis diagnose_tiff(std::span<const std::uint8_t> bytes) noexcept {
  UndecodableImageDiagnosis result;
  if (bytes.size() < 8) {
    result.reason = Reason::TiffUnreadable;
    return result;
  }
  const bool little_endian = bytes[0] == 'I';
  const auto magic = read_u16(bytes, 2, little_endian);
  const bool big_tiff = magic == 43;
  if (magic != 42 && !big_tiff) {
    result.reason = Reason::TiffUnreadable;
    return result;
  }
  const std::size_t offset_width = big_tiff ? 8 : 4;
  const std::size_t count_width = big_tiff ? 8 : 2;
  const std::size_t entry_size = big_tiff ? 20 : 12;
  const std::size_t ifd_offset_at = big_tiff ? 8 : 4;
  if (bytes.size() < ifd_offset_at + offset_width) {
    result.reason = Reason::TiffUnreadable;
    return result;
  }
  const auto ifd = read_uint(bytes, ifd_offset_at, offset_width, little_endian);
  if (ifd + count_width > bytes.size()) {
    result.reason = Reason::TiffUnreadable;
    return result;
  }
  const auto entries = read_uint(bytes, static_cast<std::size_t>(ifd), count_width, little_endian);
  if (entries == 0 || entries > 65535 || ifd + count_width + entries * entry_size > bytes.size()) {
    result.reason = Reason::TiffUnreadable;
    return result;
  }
  // The first value of a SHORT or LONG tag, inline or behind the value offset.
  std::uint16_t compression = 1;
  std::uint16_t bits_per_sample = 1;
  std::uint16_t samples_per_pixel = 1;
  std::uint16_t sample_format = 1;
  for (std::uint64_t i = 0; i < entries; ++i) {
    const auto entry = static_cast<std::size_t>(ifd + count_width + i * entry_size);
    const auto tag = read_u16(bytes, entry, little_endian);
    if (tag != 258 && tag != 259 && tag != 277 && tag != 339) {
      continue;
    }
    const auto type = read_u16(bytes, entry + 2, little_endian);
    if (type != 3 && type != 4) {
      continue;
    }
    // Entry layout: tag (2), type (2), count (4, BigTIFF 8), value or offset (4, BigTIFF 8).
    const auto count = read_uint(bytes, entry + 4, offset_width, little_endian);
    const std::size_t value_size = type == 3 ? 2 : 4;
    auto value_at = entry + 4 + offset_width;
    if (count * value_size > offset_width) {
      value_at = static_cast<std::size_t>(read_uint(bytes, value_at, offset_width, little_endian));
    }
    if (count == 0 || value_at + value_size > bytes.size()) {
      continue;
    }
    const auto value = static_cast<std::uint16_t>(read_uint(bytes, value_at, value_size, little_endian) & 0xffffU);
    switch (tag) {
      case 258: bits_per_sample = value; break;
      case 259: compression = value; break;
      case 277: samples_per_pixel = value; break;
      default: sample_format = value; break;
    }
  }
  bool codec_known = false;
  for (const auto& codec : kTiffCodecs) {
    if (codec.code != compression) {
      continue;
    }
    codec_known = true;
    if (!codec.supported) {
      result.reason = (compression == 6 || compression == 7) ? Reason::TiffJpegCompression
                                                              : Reason::TiffUnsupportedCompression;
      result.detail = codec.name;
      return result;
    }
  }
  if (!codec_known) {
    result.reason = Reason::TiffUnsupportedCompression;
    result.detail = "code " + std::to_string(compression);
    return result;
  }
  // Sample layouts, as Qt 6.8's TIFF reader actually behaves on the imagecodecs-samples
  // corpus: 8 and 16-bit integer (any sign, planar or not, with alpha or extra samples),
  // 32-bit only as IEEE float, and 1, 2 and 4-bit only for grayscale. Everything else
  // (odd depths, 64-bit, 32-bit integers, packed color) fails with the generic message.
  const bool depth_supported = bits_per_sample == 1 || bits_per_sample == 2 || bits_per_sample == 4 ||
                               bits_per_sample == 8 || bits_per_sample == 16 || bits_per_sample == 32;
  if (!depth_supported) {
    result.reason = Reason::TiffUnsupportedBitDepth;
    result.detail = std::to_string(bits_per_sample);
    return result;
  }
  if (bits_per_sample == 32 && sample_format != 3) {
    result.reason = Reason::TiffIntegerSamples;
    return result;
  }
  if (bits_per_sample < 8 && samples_per_pixel >= 3) {
    result.reason = Reason::TiffPackedColor;
    result.detail = std::to_string(bits_per_sample);
    return result;
  }
  return result;  // the layout and codec are fine; the failure is elsewhere
}

}  // namespace

UndecodableImageDiagnosis diagnose_undecodable_image(std::span<const std::uint8_t> bytes,
                                                     std::string_view extension) noexcept {
  UndecodableImageDiagnosis result;
  if (bytes.empty()) {
    result.reason = Reason::EmptyFile;
    return result;
  }
  const auto family = extension_family(extension);
  const auto detected = detect_container(bytes);
  if (detected.has_value()) {
    if (family != detected->family) {
      result.reason = Reason::DifferentFormat;
      result.detail = detected->name;
      result.suggested_extension = detected->extension;
      return result;
    }
    if (detected->extension[0] == '\0') {
      // The name is honest; Patchy simply has no decoder for the format.
      result.reason = Reason::UnsupportedFormat;
      result.detail = detected->name;
      return result;
    }
    if (family == "jpeg") {
      result.reason = diagnose_jpeg(bytes);
      return result;
    }
    if (family == "tiff") {
      return diagnose_tiff(bytes);
    }
    return result;
  }
  if (looks_like_text(bytes)) {
    result.reason = Reason::TextContent;
    return result;
  }
  if (family == "jpeg") {
    result.reason = Reason::JpegNoFrame;  // no SOI: not a JPEG stream at all
  } else if (family == "tiff") {
    result.reason = Reason::TiffUnreadable;
  }
  return result;
}

std::optional<SniffedImageFormat> sniff_openable_image_format(std::span<const std::uint8_t> bytes) noexcept {
  const auto detected = detect_container(bytes);
  if (!detected.has_value() || detected->extension[0] == '\0') {
    return std::nullopt;
  }
  return SniffedImageFormat{detected->name, detected->extension};
}

bool extensions_share_format(std::string_view a, std::string_view b) noexcept {
  return extension_family(a) == extension_family(b);
}

}  // namespace patchy::formats
