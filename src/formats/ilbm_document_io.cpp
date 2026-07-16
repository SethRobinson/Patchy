// IFF ILBM/PBM reader + planar ILBM writer. Big-endian throughout (psd::BigEndianReader /
// BigEndianWriter); BODY compression is ByteRun1, identical to PSD PackBits, so the shared
// psd::decode_packbits / encode_packbits_row do the work. Row planes are word-aligned
// (((w+15)/16)*2 bytes) — the classic corruption bug when missed.

#include "formats/ilbm_document_io.hpp"

#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace patchy::ilbm {

namespace {

constexpr std::uint32_t kCamgEhb = 0x80;
constexpr std::uint32_t kCamgHam = 0x800;

[[nodiscard]] std::size_t plane_row_bytes(std::int32_t width) {
  return (static_cast<std::size_t>(width) + 15U) / 16U * 2U;
}

struct Bmhd {
  std::int32_t width{0};
  std::int32_t height{0};
  std::uint8_t planes{0};
  std::uint8_t masking{0};
  std::uint8_t compression{0};
  std::uint16_t transparent_color{0};
};

[[nodiscard]] std::array<char, 4> read_chunk_id(psd::BigEndianReader& reader) {
  std::array<char, 4> id{};
  for (auto& character : id) {
    character = static_cast<char>(reader.read_u8());
  }
  return id;
}

[[nodiscard]] bool id_equals(const std::array<char, 4>& id, const char* expected) noexcept {
  return id[0] == expected[0] && id[1] == expected[1] && id[2] == expected[2] && id[3] == expected[3];
}

}  // namespace

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < 12) {
    return false;
  }
  const bool form = bytes[0] == 'F' && bytes[1] == 'O' && bytes[2] == 'R' && bytes[3] == 'M';
  const bool ilbm = bytes[8] == 'I' && bytes[9] == 'L' && bytes[10] == 'B' && bytes[11] == 'M';
  const bool pbm = bytes[8] == 'P' && bytes[9] == 'B' && bytes[10] == 'M' && bytes[11] == ' ';
  return form && (ilbm || pbm);
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  if (!can_read(bytes)) {
    throw std::runtime_error("File is not an IFF ILBM image");
  }
  psd::BigEndianReader reader(bytes);
  reader.skip(4);  // FORM
  const auto form_size = reader.read_u32();
  const auto form_end = std::min<std::size_t>(bytes.size(), 8U + form_size);
  const auto form_type = read_chunk_id(reader);
  const bool chunky = id_equals(form_type, "PBM ");

  Bmhd header;
  bool have_header = false;
  std::vector<RgbColor> palette;
  std::uint32_t camg = 0;
  std::vector<std::uint8_t> body;
  bool have_body = false;

  while (reader.position() + 8 <= form_end) {
    const auto chunk_id = read_chunk_id(reader);
    const auto chunk_size = reader.read_u32();
    const auto chunk_start = reader.position();
    if (chunk_start + chunk_size > bytes.size()) {
      break;
    }
    if (id_equals(chunk_id, "BMHD")) {
      header.width = reader.read_u16();
      header.height = reader.read_u16();
      reader.skip(4);  // x, y origin
      header.planes = reader.read_u8();
      header.masking = reader.read_u8();
      header.compression = reader.read_u8();
      reader.skip(1);
      header.transparent_color = reader.read_u16();
      have_header = true;
    } else if (id_equals(chunk_id, "CMAP")) {
      const auto count = chunk_size / 3U;
      palette.clear();
      palette.reserve(count);
      for (std::uint32_t i = 0; i < count && palette.size() < 256; ++i) {
        const auto red = reader.read_u8();
        const auto green = reader.read_u8();
        const auto blue = reader.read_u8();
        palette.push_back(RgbColor{red, green, blue});
      }
    } else if (id_equals(chunk_id, "CAMG")) {
      camg = reader.read_u32();
    } else if (id_equals(chunk_id, "BODY")) {
      body.assign(bytes.begin() + static_cast<std::ptrdiff_t>(chunk_start),
                  bytes.begin() + static_cast<std::ptrdiff_t>(chunk_start + chunk_size));
      have_body = true;
    }
    // Chunks are padded to even sizes; the pad byte is excluded from the length.
    const auto padded = chunk_start + chunk_size + (chunk_size % 2U);
    if (padded > bytes.size() || padded < reader.position()) {
      break;
    }
    reader.skip(padded - reader.position());
  }

  if (!have_header || !have_body) {
    throw std::runtime_error("IFF ILBM file is missing its BMHD or BODY chunk");
  }
  if (header.width <= 0 || header.height <= 0) {
    throw std::runtime_error("IFF ILBM image has invalid dimensions");
  }
  if ((camg & kCamgHam) != 0) {
    throw std::runtime_error("HAM-mode IFF images are not supported yet");
  }
  if (header.compression > 1) {
    throw std::runtime_error("IFF ILBM compression is not supported");
  }
  if (!chunky && (header.planes == 0 || header.planes > 8)) {
    throw std::runtime_error("Only 1-8 bitplane IFF ILBM images are supported (no 24-bit deep ILBM yet)");
  }
  if (chunky && header.planes != 8) {
    throw std::runtime_error("Only 8-bit IFF PBM images are supported");
  }
  if (palette.empty()) {
    throw std::runtime_error("IFF ILBM file is missing its CMAP palette");
  }

  // EHB doubles a 32-color palette with half-brightness copies.
  if ((camg & kCamgEhb) != 0 && palette.size() <= 32) {
    const auto base_size = palette.size();
    for (std::size_t i = 0; i < base_size; ++i) {
      palette.push_back(RgbColor{static_cast<std::uint8_t>(palette[i].red / 2),
                                 static_cast<std::uint8_t>(palette[i].green / 2),
                                 static_cast<std::uint8_t>(palette[i].blue / 2)});
    }
  }

  const bool mask_plane = header.masking == 1;
  const bool transparent_color = header.masking == 2;
  const auto row_bytes = chunky ? ((static_cast<std::size_t>(header.width) + 1U) / 2U * 2U)
                                : plane_row_bytes(header.width);
  const auto rows_per_line = chunky ? std::size_t{1} : static_cast<std::size_t>(header.planes) + (mask_plane ? 1U : 0U);
  const auto decoded_size = row_bytes * rows_per_line * static_cast<std::size_t>(header.height);
  const auto data = header.compression == 1 ? psd::decode_packbits(body, decoded_size) : std::move(body);
  if (data.size() < decoded_size) {
    throw std::runtime_error("IFF ILBM body data ended unexpectedly");
  }

  const bool has_alpha = mask_plane || transparent_color;
  PixelBuffer pixels(header.width, header.height, has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  for (std::int32_t y = 0; y < header.height; ++y) {
    const auto* line = data.data() + static_cast<std::size_t>(y) * row_bytes * rows_per_line;
    for (std::int32_t x = 0; x < header.width; ++x) {
      std::uint32_t index = 0;
      bool masked_visible = true;
      if (chunky) {
        index = line[x];
      } else {
        for (std::uint8_t plane = 0; plane < header.planes; ++plane) {
          const auto byte = line[static_cast<std::size_t>(plane) * row_bytes + static_cast<std::size_t>(x / 8)];
          index |= static_cast<std::uint32_t>((byte >> (7 - (x % 8))) & 1U) << plane;
        }
        if (mask_plane) {
          const auto byte =
              line[static_cast<std::size_t>(header.planes) * row_bytes + static_cast<std::size_t>(x / 8)];
          masked_visible = ((byte >> (7 - (x % 8))) & 1U) != 0;
        }
      }
      auto* dst = pixels.pixel(x, y);
      const auto& color = palette[std::min<std::size_t>(index, palette.size() - 1)];
      dst[0] = color.red;
      dst[1] = color.green;
      dst[2] = color.blue;
      if (has_alpha) {
        const bool transparent = (mask_plane && !masked_visible) ||
                                 (transparent_color && index == header.transparent_color);
        dst[3] = transparent ? 0 : 255;
      }
    }
  }

  Document document(header.width, header.height, has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  document.add_pixel_layer("Background", std::move(pixels));
  if (palette.size() <= 256) {
    document.indexed_palette() = DocumentIndexedPalette{std::move(palette), 8};
  }
  if (notices != nullptr && (camg & kCamgEhb) != 0) {
    notices->push_back("Extra-halfbrite (EHB) palette expanded to 64 colors");
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  auto document = read(formats::read_file_bytes(path, "IFF ILBM"), notices);
  formats::rename_first_layer_to_stem(document, path);
  return document;
}

std::vector<std::uint8_t> DocumentIo::write(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0 || document.width() > 0xffff ||
      document.height() > 0xffff) {
    throw std::runtime_error("IFF ILBM dimensions must be between 1 and 65535");
  }
  const auto indexed =
      document.palette_editing().has_value() && !document.palette_editing()->palette.colors.empty()
          ? indexed_flatten_for_palette_mode(document)
          : indexed_flatten_quantized(document, 256, 128);

  std::uint8_t planes = 1;
  while ((std::size_t{1} << planes) < indexed.palette.size()) {
    ++planes;
  }
  if (planes > 8) {
    throw std::runtime_error("IFF ILBM palettes cannot exceed 256 colors");
  }

  const auto row_bytes = plane_row_bytes(indexed.width);

  psd::BigEndianWriter body;
  std::vector<std::uint8_t> plane_row(row_bytes);
  for (std::int32_t y = 0; y < indexed.height; ++y) {
    const auto* row = indexed.indexes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(indexed.width);
    for (std::uint8_t plane = 0; plane < planes; ++plane) {
      std::fill(plane_row.begin(), plane_row.end(), 0);
      for (std::int32_t x = 0; x < indexed.width; ++x) {
        if ((row[x] >> plane) & 1U) {
          plane_row[static_cast<std::size_t>(x / 8)] |= static_cast<std::uint8_t>(1U << (7 - (x % 8)));
        }
      }
      const auto encoded = psd::encode_packbits_row(plane_row);
      body.write_bytes(encoded);
    }
  }

  psd::BigEndianWriter form;
  form.write_u8('F');
  form.write_u8('O');
  form.write_u8('R');
  form.write_u8('M');
  const auto size_position = form.bytes().size();
  form.write_u32(0);  // patched below
  for (const char c : {'I', 'L', 'B', 'M'}) {
    form.write_u8(static_cast<std::uint8_t>(c));
  }

  // BMHD
  for (const char c : {'B', 'M', 'H', 'D'}) {
    form.write_u8(static_cast<std::uint8_t>(c));
  }
  form.write_u32(20);
  form.write_u16(static_cast<std::uint16_t>(indexed.width));
  form.write_u16(static_cast<std::uint16_t>(indexed.height));
  form.write_u16(0);
  form.write_u16(0);
  form.write_u8(planes);
  form.write_u8(indexed.transparent_index >= 0 ? 2 : 0);  // masking: transparent color
  form.write_u8(1);                                       // ByteRun1
  form.write_u8(0);
  form.write_u16(indexed.transparent_index >= 0 ? static_cast<std::uint16_t>(indexed.transparent_index) : 0);
  form.write_u8(1);   // x aspect
  form.write_u8(1);   // y aspect
  form.write_u16(static_cast<std::uint16_t>(indexed.width));
  form.write_u16(static_cast<std::uint16_t>(indexed.height));

  // CMAP
  for (const char c : {'C', 'M', 'A', 'P'}) {
    form.write_u8(static_cast<std::uint8_t>(c));
  }
  const auto cmap_size = static_cast<std::uint32_t>(indexed.palette.size() * 3U);
  form.write_u32(cmap_size);
  for (const auto& color : indexed.palette) {
    form.write_u8(color.red);
    form.write_u8(color.green);
    form.write_u8(color.blue);
  }
  if (cmap_size % 2U != 0) {
    form.write_u8(0);  // even padding
  }

  // BODY
  for (const char c : {'B', 'O', 'D', 'Y'}) {
    form.write_u8(static_cast<std::uint8_t>(c));
  }
  form.write_u32(static_cast<std::uint32_t>(body.bytes().size()));
  form.write_bytes(body.bytes());
  if (body.bytes().size() % 2U != 0) {
    form.write_u8(0);
  }

  // Patch the FORM size (everything after the size field).
  auto bytes = std::move(form.bytes());
  const auto form_size = static_cast<std::uint32_t>(bytes.size() - 8U);
  bytes[size_position] = static_cast<std::uint8_t>((form_size >> 24U) & 0xffU);
  bytes[size_position + 1] = static_cast<std::uint8_t>((form_size >> 16U) & 0xffU);
  bytes[size_position + 2] = static_cast<std::uint8_t>((form_size >> 8U) & 0xffU);
  bytes[size_position + 3] = static_cast<std::uint8_t>(form_size & 0xffU);
  return bytes;
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path) {
  formats::write_file_bytes(path, write(document), "IFF ILBM");
}

}  // namespace patchy::ilbm
