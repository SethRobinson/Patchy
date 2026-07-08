#include "formats/tga_document_io.hpp"

#include "formats/binary_le.hpp"
#include "formats/document_flatten.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace patchy::tga {

namespace {

constexpr std::size_t kHeaderSize = 18;

enum ImageType : std::uint8_t {
  kTypeNone = 0,
  kTypeIndexed = 1,
  kTypeTruecolor = 2,
  kTypeGrayscale = 3,
  kTypeIndexedRle = 9,
  kTypeTruecolorRle = 10,
  kTypeGrayscaleRle = 11,
};

struct Header {
  std::uint8_t id_length{0};
  std::uint8_t color_map_type{0};
  std::uint8_t image_type{0};
  std::uint16_t color_map_first{0};
  std::uint16_t color_map_length{0};
  std::uint8_t color_map_entry_bits{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint8_t pixel_depth{0};
  std::uint8_t descriptor{0};
};

[[nodiscard]] LittleEndianReader tga_reader(std::span<const std::uint8_t> bytes) {
  return LittleEndianReader(bytes, "TGA data ended unexpectedly");
}

[[nodiscard]] Header read_header(LittleEndianReader& reader) {
  Header header;
  header.id_length = reader.read_u8();
  header.color_map_type = reader.read_u8();
  header.image_type = reader.read_u8();
  header.color_map_first = reader.read_u16();
  header.color_map_length = reader.read_u16();
  header.color_map_entry_bits = reader.read_u8();
  reader.skip(4);  // x/y origin
  header.width = reader.read_u16();
  header.height = reader.read_u16();
  header.pixel_depth = reader.read_u8();
  header.descriptor = reader.read_u8();
  return header;
}

[[nodiscard]] bool header_is_plausible(const Header& header) noexcept {
  switch (header.image_type) {
    case kTypeIndexed:
    case kTypeIndexedRle:
    case kTypeTruecolor:
    case kTypeTruecolorRle:
    case kTypeGrayscale:
    case kTypeGrayscaleRle:
      break;
    default:
      return false;
  }
  if (header.color_map_type > 1) {
    return false;
  }
  if (header.width == 0 || header.height == 0) {
    return false;
  }
  switch (header.pixel_depth) {
    case 8:
    case 15:
    case 16:
    case 24:
    case 32:
      return true;
    default:
      return false;
  }
}

struct Rgba {
  std::uint8_t r{0}, g{0}, b{0}, a{255};
};

// Decodes the pixel stream (raw or RLE packets) into bytes-per-pixel groups in file order.
[[nodiscard]] std::vector<std::uint8_t> decode_pixel_stream(LittleEndianReader& reader,
                                                            std::span<const std::uint8_t> bytes, bool rle,
                                                            std::size_t pixel_count, std::size_t bytes_per_pixel) {
  std::vector<std::uint8_t> out;
  out.reserve(pixel_count * bytes_per_pixel);
  if (!rle) {
    const auto total = pixel_count * bytes_per_pixel;
    if (reader.remaining() < total) {
      throw std::runtime_error("TGA data ended unexpectedly");
    }
    const auto offset = reader.position();
    out.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
               bytes.begin() + static_cast<std::ptrdiff_t>(offset + total));
    reader.skip(total);
    return out;
  }
  while (out.size() < pixel_count * bytes_per_pixel) {
    const auto packet = reader.read_u8();
    const std::size_t run = static_cast<std::size_t>(packet & 0x7fU) + 1U;
    if ((packet & 0x80U) != 0) {
      std::array<std::uint8_t, 4> value{};
      for (std::size_t i = 0; i < bytes_per_pixel; ++i) {
        value[i] = reader.read_u8();
      }
      for (std::size_t i = 0; i < run; ++i) {
        out.insert(out.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(bytes_per_pixel));
      }
    } else {
      for (std::size_t i = 0; i < run * bytes_per_pixel; ++i) {
        out.push_back(reader.read_u8());
      }
    }
  }
  out.resize(pixel_count * bytes_per_pixel);
  return out;
}

}  // namespace

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < kHeaderSize) {
    return false;
  }
  auto reader = tga_reader(bytes);
  try {
    return header_is_plausible(read_header(reader));
  } catch (const std::exception&) {
    return false;
  }
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  auto reader = tga_reader(bytes);
  const auto header = read_header(reader);
  if (!header_is_plausible(header)) {
    throw std::runtime_error("File is not a supported TGA image");
  }
  if (header.pixel_depth == 15 || header.pixel_depth == 16) {
    throw std::runtime_error("15/16-bit TGA images are not supported yet; convert to 24-bit or 32-bit");
  }
  reader.skip(header.id_length);

  const bool indexed = header.image_type == kTypeIndexed || header.image_type == kTypeIndexedRle;
  const bool grayscale = header.image_type == kTypeGrayscale || header.image_type == kTypeGrayscaleRle;
  const bool rle = header.image_type >= kTypeIndexedRle;

  std::vector<Rgba> color_map;
  if (header.color_map_type == 1) {
    if (header.color_map_entry_bits != 24 && header.color_map_entry_bits != 32) {
      throw std::runtime_error("TGA color maps must be 24-bit or 32-bit");
    }
    const auto entry_bytes = static_cast<std::size_t>(header.color_map_entry_bits) / 8U;
    color_map.resize(header.color_map_length);
    for (auto& entry : color_map) {
      const auto blue = reader.read_u8();
      const auto green = reader.read_u8();
      const auto red = reader.read_u8();
      entry = Rgba{red, green, blue, entry_bytes == 4 ? reader.read_u8() : std::uint8_t{255}};
    }
  }
  if (indexed && color_map.empty()) {
    throw std::runtime_error("Indexed TGA image is missing its color map");
  }
  if (indexed && header.pixel_depth != 8) {
    throw std::runtime_error("Indexed TGA images must be 8-bit");
  }
  if (grayscale && header.pixel_depth != 8) {
    throw std::runtime_error("Grayscale TGA images must be 8-bit");
  }

  const auto width = static_cast<std::int32_t>(header.width);
  const auto height = static_cast<std::int32_t>(header.height);
  const auto bytes_per_pixel = static_cast<std::size_t>(header.pixel_depth) / 8U;
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto data = decode_pixel_stream(reader, bytes, rle, pixel_count, bytes_per_pixel);

  const bool top_down = (header.descriptor & 0x20U) != 0;
  const bool right_to_left = (header.descriptor & 0x10U) != 0;
  const bool has_alpha = header.pixel_depth == 32 || (indexed && header.color_map_entry_bits == 32);

  PixelBuffer pixels(width, height, has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  bool any_alpha = false;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto file_y = top_down ? y : height - 1 - y;
    for (std::int32_t x = 0; x < width; ++x) {
      const auto file_x = right_to_left ? width - 1 - x : x;
      const auto* src = data.data() + (static_cast<std::size_t>(file_y) * static_cast<std::size_t>(width) +
                                       static_cast<std::size_t>(file_x)) *
                                          bytes_per_pixel;
      auto* dst = pixels.pixel(x, y);
      if (indexed) {
        const auto index = static_cast<std::size_t>(src[0]);
        const auto map_index =
            index >= static_cast<std::size_t>(header.color_map_first) ? index - header.color_map_first : index;
        if (map_index >= color_map.size()) {
          throw std::runtime_error("TGA pixel references a missing color map entry");
        }
        const auto& entry = color_map[map_index];
        dst[0] = entry.r;
        dst[1] = entry.g;
        dst[2] = entry.b;
        if (has_alpha) {
          dst[3] = entry.a;
          any_alpha = any_alpha || entry.a != 0;
        }
      } else if (grayscale) {
        dst[0] = src[0];
        dst[1] = src[0];
        dst[2] = src[0];
      } else {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        if (bytes_per_pixel == 4) {
          dst[3] = src[3];
          any_alpha = any_alpha || src[3] != 0;
        }
      }
    }
  }
  // A 32-bit file whose alpha channel is uniformly zero is a common authoring bug (the
  // attribute bits were never filled in); import it opaque instead of invisible.
  if (has_alpha && !any_alpha) {
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        pixels.pixel(x, y)[3] = 255;
      }
    }
    if (notices != nullptr) {
      notices->push_back("The TGA alpha channel was uniformly zero and was treated as opaque");
    }
  }

  Document document(width, height, has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  document.add_pixel_layer("Background", std::move(pixels));
  if (indexed) {
    std::vector<RgbColor> palette;
    palette.reserve(color_map.size());
    for (const auto& entry : color_map) {
      palette.push_back(RgbColor{entry.r, entry.g, entry.b});
    }
    if (!palette.empty() && palette.size() <= 256) {
      document.indexed_palette() = DocumentIndexedPalette{std::move(palette), 8};
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open TGA file");
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
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error("Cannot write an empty TGA image");
  }
  if (document.width() > 0xffff || document.height() > 0xffff) {
    throw std::runtime_error("TGA images cannot exceed 65535 pixels per side");
  }
  LittleEndianWriter writer;

  if (document.palette_editing().has_value() && !document.palette_editing()->palette.colors.empty()) {
    // Palette mode: 8-bit indexed with the document palette in file order (matches the
    // indexed PNG-8 export semantics; the transparent slot maps to black).
    const auto indexed = indexed_flatten_for_palette_mode(document);
    writer.write_u8(0);  // id length
    writer.write_u8(1);  // color map present
    writer.write_u8(kTypeIndexed);
    writer.write_u16(0);
    writer.write_u16(static_cast<std::uint16_t>(indexed.palette.size()));
    writer.write_u8(24);
    writer.write_u16(0);
    writer.write_u16(0);
    writer.write_u16(static_cast<std::uint16_t>(indexed.width));
    writer.write_u16(static_cast<std::uint16_t>(indexed.height));
    writer.write_u8(8);
    writer.write_u8(0x20);  // top-left origin
    for (const auto& color : indexed.palette) {
      writer.write_u8(color.blue);
      writer.write_u8(color.green);
      writer.write_u8(color.red);
    }
    writer.write_bytes(indexed.indexes);
    return std::move(writer.bytes());
  }

  const auto flattened = flatten_document_rgba8(document);
  writer.write_u8(0);
  writer.write_u8(0);
  writer.write_u8(kTypeTruecolorRle);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u8(0);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u16(static_cast<std::uint16_t>(flattened.width()));
  writer.write_u16(static_cast<std::uint16_t>(flattened.height()));
  writer.write_u8(32);
  writer.write_u8(0x28);  // 8 alpha bits + top-left origin

  // Per-row RLE packets (runs never cross a row boundary, per the spec's recommendation).
  const auto width = flattened.width();
  for (std::int32_t y = 0; y < flattened.height(); ++y) {
    std::int32_t x = 0;
    while (x < width) {
      const auto* first = flattened.pixel(x, y);
      std::int32_t run = 1;
      while (x + run < width && run < 128) {
        const auto* next = flattened.pixel(x + run, y);
        if (next[0] != first[0] || next[1] != first[1] || next[2] != first[2] || next[3] != first[3]) {
          break;
        }
        ++run;
      }
      if (run >= 2) {
        writer.write_u8(static_cast<std::uint8_t>(0x80U | static_cast<unsigned>(run - 1)));
        writer.write_u8(first[2]);
        writer.write_u8(first[1]);
        writer.write_u8(first[0]);
        writer.write_u8(first[3]);
        x += run;
        continue;
      }
      // Literal packet: extend while the following pixel does NOT start a run of >= 2.
      std::int32_t literal = 1;
      while (x + literal < width && literal < 128) {
        if (x + literal + 1 < width) {
          const auto* a = flattened.pixel(x + literal, y);
          const auto* b = flattened.pixel(x + literal + 1, y);
          if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]) {
            break;
          }
        }
        ++literal;
      }
      writer.write_u8(static_cast<std::uint8_t>(literal - 1));
      for (std::int32_t i = 0; i < literal; ++i) {
        const auto* px = flattened.pixel(x + i, y);
        writer.write_u8(px[2]);
        writer.write_u8(px[1]);
        writer.write_u8(px[0]);
        writer.write_u8(px[3]);
      }
      x += literal;
    }
  }
  return std::move(writer.bytes());
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path) {
  const auto bytes = write(document);
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open TGA file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write TGA file");
  }
}

}  // namespace patchy::tga
