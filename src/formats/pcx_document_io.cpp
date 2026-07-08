#include "formats/pcx_document_io.hpp"

#include "formats/binary_le.hpp"
#include "formats/document_flatten.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace patchy::pcx {

namespace {

constexpr std::size_t kHeaderSize = 128;
constexpr std::uint8_t kManufacturer = 0x0A;
constexpr std::uint8_t kEofPaletteMarker = 0x0C;

struct Header {
  std::uint8_t version{0};
  std::uint8_t encoding{0};
  std::uint8_t bits_per_plane{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::uint8_t planes{0};
  std::size_t bytes_per_line{0};
};

[[nodiscard]] Header read_header(std::span<const std::uint8_t> bytes) {
  LittleEndianReader reader(bytes, "PCX data ended unexpectedly");
  if (reader.read_u8() != kManufacturer) {
    throw std::runtime_error("File is not a PCX image");
  }
  Header header;
  header.version = reader.read_u8();
  header.encoding = reader.read_u8();
  header.bits_per_plane = reader.read_u8();
  const auto x_min = reader.read_u16();
  const auto y_min = reader.read_u16();
  const auto x_max = reader.read_u16();
  const auto y_max = reader.read_u16();
  reader.skip(2 + 2);   // dpi
  reader.skip(48);      // 16-color header palette (only used below 8 bpp)
  reader.skip(1);       // reserved
  header.planes = reader.read_u8();
  header.bytes_per_line = reader.read_u16();
  header.width = x_max - x_min + 1;
  header.height = y_max - y_min + 1;
  return header;
}

// PCX RLE: a byte with the top two bits set is a run count (low 6 bits) for the next byte.
[[nodiscard]] std::vector<std::uint8_t> decode_rle(std::span<const std::uint8_t> bytes, std::size_t offset,
                                                   std::size_t decoded_size, bool rle) {
  std::vector<std::uint8_t> out;
  out.reserve(decoded_size);
  std::size_t position = offset;
  while (out.size() < decoded_size && position < bytes.size()) {
    const auto byte = bytes[position++];
    if (rle && (byte & 0xC0U) == 0xC0U) {
      if (position >= bytes.size()) {
        break;
      }
      const auto value = bytes[position++];
      const auto run = static_cast<std::size_t>(byte & 0x3FU);
      out.insert(out.end(), run, value);
    } else {
      out.push_back(byte);
    }
  }
  if (out.size() < decoded_size) {
    throw std::runtime_error("PCX data ended unexpectedly");
  }
  out.resize(decoded_size);
  return out;
}

void encode_rle_row(LittleEndianWriter& writer, std::span<const std::uint8_t> row) {
  std::size_t x = 0;
  while (x < row.size()) {
    const auto value = row[x];
    std::size_t run = 1;
    while (x + run < row.size() && row[x + run] == value && run < 63) {
      ++run;
    }
    if (run > 1 || (value & 0xC0U) == 0xC0U) {
      writer.write_u8(static_cast<std::uint8_t>(0xC0U | run));
      writer.write_u8(value);
    } else {
      writer.write_u8(value);
    }
    x += run;
  }
}

}  // namespace

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < kHeaderSize) {
    return false;
  }
  return bytes[0] == kManufacturer && bytes[2] <= 1 &&
         (bytes[3] == 1 || bytes[3] == 2 || bytes[3] == 4 || bytes[3] == 8);
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  (void)notices;
  if (bytes.size() < kHeaderSize) {
    throw std::runtime_error("PCX file is too short");
  }
  const auto header = read_header(bytes);
  if (header.width <= 0 || header.height <= 0) {
    throw std::runtime_error("PCX image has invalid dimensions");
  }
  if (header.encoding > 1) {
    throw std::runtime_error("PCX encoding is not supported");
  }
  const bool indexed = header.bits_per_plane == 8 && header.planes == 1;
  const bool truecolor = header.bits_per_plane == 8 && header.planes == 3;
  if (!indexed && !truecolor) {
    throw std::runtime_error(
        "Only 8-bit indexed and 24-bit PCX images are supported; convert 16-color PCX files to 256 colors first");
  }
  if (header.bytes_per_line < static_cast<std::size_t>(header.width)) {
    throw std::runtime_error("PCX row stride is smaller than the image width");
  }

  const auto row_bytes = header.bytes_per_line * header.planes;
  const auto data = decode_rle(bytes, kHeaderSize, row_bytes * static_cast<std::size_t>(header.height),
                               header.encoding == 1);

  std::vector<RgbColor> palette;
  if (indexed) {
    // 256-color palette: 769 bytes from EOF, 0x0C marker + 768 RGB bytes.
    if (bytes.size() < kHeaderSize + 769 || bytes[bytes.size() - 769] != kEofPaletteMarker) {
      throw std::runtime_error("PCX file is missing its 256-color palette");
    }
    palette.reserve(256);
    const auto* table = bytes.data() + bytes.size() - 768;
    for (int i = 0; i < 256; ++i) {
      palette.push_back(RgbColor{table[i * 3], table[i * 3 + 1], table[i * 3 + 2]});
    }
  }

  PixelBuffer pixels(header.width, header.height, PixelFormat::rgb8());
  for (std::int32_t y = 0; y < header.height; ++y) {
    const auto* row = data.data() + static_cast<std::size_t>(y) * row_bytes;
    for (std::int32_t x = 0; x < header.width; ++x) {
      auto* dst = pixels.pixel(x, y);
      if (indexed) {
        const auto& color = palette[row[x]];
        dst[0] = color.red;
        dst[1] = color.green;
        dst[2] = color.blue;
      } else {
        dst[0] = row[x];
        dst[1] = row[header.bytes_per_line + static_cast<std::size_t>(x)];
        dst[2] = row[header.bytes_per_line * 2 + static_cast<std::size_t>(x)];
      }
    }
  }

  Document document(header.width, header.height, PixelFormat::rgb8());
  document.add_pixel_layer("Background", std::move(pixels));
  if (indexed) {
    document.indexed_palette() = DocumentIndexedPalette{std::move(palette), 8};
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open PCX file");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  auto document = read(bytes, notices);
  const auto stem = path.stem().string();
  if (!stem.empty() && !document.layers().empty()) {
    document.layers().front().set_name(stem);
  }
  return document;
}

std::vector<std::uint8_t> DocumentIo::write(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0 || document.width() > 0xffff ||
      document.height() > 0xffff) {
    throw std::runtime_error("PCX dimensions must be between 1 and 65535");
  }
  const bool indexed =
      document.palette_editing().has_value() && !document.palette_editing()->palette.colors.empty();

  const auto width = document.width();
  const auto height = document.height();
  // Even stride, per the spec.
  const auto bytes_per_line = static_cast<std::size_t>(width) + (static_cast<std::size_t>(width) % 2);

  LittleEndianWriter writer;
  writer.write_u8(kManufacturer);
  writer.write_u8(5);  // version 5 (256-color capable)
  writer.write_u8(1);  // RLE
  writer.write_u8(8);  // bits per plane
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u16(static_cast<std::uint16_t>(width - 1));
  writer.write_u16(static_cast<std::uint16_t>(height - 1));
  writer.write_u16(300);
  writer.write_u16(300);
  for (int i = 0; i < 48; ++i) {
    writer.write_u8(0);  // 16-color header palette (unused at 8 bpp)
  }
  writer.write_u8(0);
  writer.write_u8(indexed ? 1 : 3);
  writer.write_u16(static_cast<std::uint16_t>(bytes_per_line));
  // Palette info 1 = color palette (2 means GRAYSCALE — Photoshop honors this field and
  // renders indexed files written with 2 as near-black gray levels; found via COM probe).
  writer.write_u16(1);
  writer.write_u16(0);
  writer.write_u16(0);
  while (writer.bytes().size() < kHeaderSize) {
    writer.write_u8(0);
  }

  if (indexed) {
    const auto flat = indexed_flatten_for_palette_mode(document);
    std::vector<std::uint8_t> row(bytes_per_line, 0);
    for (std::int32_t y = 0; y < height; ++y) {
      std::fill(row.begin(), row.end(), 0);
      std::copy_n(flat.indexes.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * width),
                  width, row.begin());
      encode_rle_row(writer, row);
    }
    writer.write_u8(kEofPaletteMarker);
    for (int i = 0; i < 256; ++i) {
      const auto color = i < static_cast<int>(flat.palette.size()) ? flat.palette[static_cast<std::size_t>(i)]
                                                                   : RgbColor{0, 0, 0};
      writer.write_u8(color.red);
      writer.write_u8(color.green);
      writer.write_u8(color.blue);
    }
  } else {
    const auto flattened = flatten_document_rgba8(document);
    std::vector<std::uint8_t> plane(bytes_per_line, 0);
    for (std::int32_t y = 0; y < height; ++y) {
      for (int channel = 0; channel < 3; ++channel) {
        std::fill(plane.begin(), plane.end(), 0);
        for (std::int32_t x = 0; x < width; ++x) {
          plane[static_cast<std::size_t>(x)] = flattened.pixel(x, y)[channel];
        }
        encode_rle_row(writer, plane);
      }
    }
  }
  return std::move(writer.bytes());
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path) {
  const auto bytes = write(document);
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open PCX file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write PCX file");
  }
}

}  // namespace patchy::pcx
