#include "formats/ico_document_io.hpp"

#include "formats/binary_le.hpp"
#include "formats/document_flatten.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace patchy::ico {

namespace {

constexpr std::size_t kIconDirSize = 6;
constexpr std::size_t kIconDirEntrySize = 16;
constexpr std::uint32_t kInfoHeaderSize = 40;
constexpr std::uint16_t kTypeIcon = 1;
constexpr std::uint16_t kTypeCursor = 2;
constexpr int kMaxIconSize = 256;
constexpr std::array<std::uint8_t, 8> kPngSignature = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

PngDecodeFn png_decoder = nullptr;
PngEncodeFn png_encoder = nullptr;

[[nodiscard]] LittleEndianReader ico_reader(std::span<const std::uint8_t> bytes) {
  return LittleEndianReader(bytes, "ICO data ended unexpectedly");
}

[[nodiscard]] bool has_png_signature(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= kPngSignature.size() &&
         std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin());
}

[[nodiscard]] std::size_t stride_32bit(std::int32_t width, int bits_per_pixel) {
  return (static_cast<std::size_t>(width) * static_cast<std::size_t>(bits_per_pixel) + 31U) / 32U * 4U;
}

struct DirectoryEntry {
  std::uint16_t hotspot_x{0};  // planes for ICO
  std::uint16_t hotspot_y{0};  // bit count for ICO
  std::uint32_t byte_count{0};
  std::uint32_t offset{0};
};

// Decodes a classic BMP-style entry (BITMAPINFOHEADER with doubled height, XOR pixels,
// then a 1-bpp AND transparency mask) into straight-alpha RGBA.
[[nodiscard]] RgbaImage decode_bmp_entry(std::span<const std::uint8_t> bytes) {
  auto reader = ico_reader(bytes);
  const auto header_size = reader.read_u32();
  if (header_size < kInfoHeaderSize) {
    throw std::runtime_error("ICO entry has an unsupported bitmap header");
  }
  const auto width = reader.read_i32();
  const auto doubled_height = reader.read_i32();
  reader.skip(2);  // planes
  const auto bit_count = reader.read_u16();
  const auto compression = reader.read_u32();
  reader.skip(4 * 3);  // image size, x/y pixels per meter
  auto colors_used = reader.read_u32();
  reader.skip(4);  // important colors
  reader.skip(header_size - kInfoHeaderSize);

  if (compression != 0) {
    throw std::runtime_error("ICO entry uses an unsupported compression");
  }
  if (width <= 0 || width > kMaxIconSize) {
    throw std::runtime_error("ICO entry has an invalid width");
  }
  // The stored height covers the XOR pixels plus the AND mask.
  const bool has_mask = doubled_height > 0 && doubled_height % 2 == 0;
  const auto height = has_mask ? doubled_height / 2 : doubled_height;
  if (height <= 0 || height > kMaxIconSize) {
    throw std::runtime_error("ICO entry has an invalid height");
  }

  std::vector<std::array<std::uint8_t, 4>> palette;
  if (bit_count <= 8) {
    if (colors_used == 0) {
      colors_used = 1U << bit_count;
    }
    if (colors_used > 256) {
      throw std::runtime_error("ICO entry palette is too large");
    }
    palette.reserve(colors_used);
    for (std::uint32_t i = 0; i < colors_used; ++i) {
      const auto blue = reader.read_u8();
      const auto green = reader.read_u8();
      const auto red = reader.read_u8();
      reader.skip(1);
      palette.push_back({red, green, blue, 255});
    }
  } else if (bit_count != 24 && bit_count != 32) {
    throw std::runtime_error("ICO entry has an unsupported bit depth");
  }

  const auto xor_stride = stride_32bit(width, bit_count);
  const auto and_stride = stride_32bit(width, 1);
  if (reader.remaining() < xor_stride * static_cast<std::size_t>(height)) {
    throw std::runtime_error("ICO data ended unexpectedly");
  }
  const auto xor_offset = reader.position();
  const auto and_offset = xor_offset + xor_stride * static_cast<std::size_t>(height);
  const bool mask_available = has_mask && bytes.size() >= and_offset + and_stride * static_cast<std::size_t>(height);

  RgbaImage image;
  image.width = width;
  image.height = height;
  image.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);

  const auto mask_bit = [&](std::int32_t x, std::int32_t y) -> bool {
    if (!mask_available) {
      return false;
    }
    const auto row = and_offset + and_stride * static_cast<std::size_t>(height - 1 - y);
    const auto byte = bytes[row + static_cast<std::size_t>(x / 8)];
    return (byte >> (7 - (x % 8))) & 1U;
  };

  bool any_alpha = false;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto row = xor_offset + xor_stride * static_cast<std::size_t>(height - 1 - y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* out = image.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                       static_cast<std::size_t>(x)) *
                                          4U;
      if (bit_count == 32) {
        const auto* px = bytes.data() + row + static_cast<std::size_t>(x) * 4U;
        out[0] = px[2];
        out[1] = px[1];
        out[2] = px[0];
        out[3] = px[3];
        any_alpha = any_alpha || px[3] != 0;
      } else if (bit_count == 24) {
        const auto* px = bytes.data() + row + static_cast<std::size_t>(x) * 3U;
        out[0] = px[2];
        out[1] = px[1];
        out[2] = px[0];
        out[3] = 255;
      } else {
        std::uint32_t index = 0;
        const auto* px_row = bytes.data() + row;
        switch (bit_count) {
          case 8:
            index = px_row[x];
            break;
          case 4:
            index = (px_row[x / 2] >> ((x % 2 == 0) ? 4 : 0)) & 0x0fU;
            break;
          case 2:
            index = (px_row[x / 4] >> (6 - 2 * (x % 4))) & 0x03U;
            break;
          case 1:
            index = (px_row[x / 8] >> (7 - (x % 8))) & 0x01U;
            break;
          default:
            throw std::runtime_error("ICO entry has an unsupported bit depth");
        }
        if (index >= palette.size()) {
          throw std::runtime_error("ICO entry references a missing palette color");
        }
        const auto& color = palette[index];
        out[0] = color[0];
        out[1] = color[1];
        out[2] = color[2];
        out[3] = 255;
      }
    }
  }

  // Apply the AND mask for depths without an alpha channel, and as the fallback for 32-bit
  // entries whose alpha channel is uniformly zero (a common real-world authoring bug: the
  // mask is the only transparency such files carry).
  const bool use_mask = bit_count != 32 || !any_alpha;
  if (use_mask && mask_available) {
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        auto* out = image.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x)) *
                                            4U;
        out[3] = mask_bit(x, y) ? 0 : 255;
      }
    }
  } else if (bit_count == 32 && !any_alpha && !mask_available) {
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
      image.rgba[i] = 255;
    }
  }
  return image;
}

[[nodiscard]] PixelBuffer pixels_from_rgba(const RgbaImage& image) {
  PixelBuffer pixels(image.width, image.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < image.height; ++y) {
    const auto* src = image.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4U;
    auto row = pixels.row(y);
    std::copy(src, src + static_cast<std::size_t>(image.width) * 4U, row.begin());
  }
  return pixels;
}

[[nodiscard]] RgbaImage rgba_from_pixels(const PixelBuffer& pixels) {
  RgbaImage image;
  image.width = pixels.width();
  image.height = pixels.height();
  image.rgba.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4U);
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < image.height; ++y) {
    for (std::int32_t x = 0; x < image.width; ++x) {
      const auto* src = pixels.pixel(x, y);
      auto* dst = image.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                                       static_cast<std::size_t>(x)) *
                                          4U;
      dst[0] = src[0];
      dst[1] = channels >= 2 ? src[1] : src[0];
      dst[2] = channels >= 3 ? src[2] : src[0];
      dst[3] = channels >= 4 ? src[3] : 255;
    }
  }
  return image;
}

// Aspect-preserving resample onto a transparent square. Smooth uses an alpha-weighted area
// average (premultiplied accumulation avoids dark fringes); nearest samples pixel centers.
// Deterministic double math only, per the cross-platform byte-identical rule.
[[nodiscard]] RgbaImage resample_to_square(const RgbaImage& source, int size, bool nearest) {
  RgbaImage out;
  out.width = size;
  out.height = size;
  out.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4U, 0);

  const auto max_side = std::max(source.width, source.height);
  const double scale = static_cast<double>(size) / static_cast<double>(max_side);
  const auto target_w = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(source.width * scale)));
  const auto target_h = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(source.height * scale)));
  const auto offset_x = (size - target_w) / 2;
  const auto offset_y = (size - target_h) / 2;

  const auto src_at = [&](std::int32_t x, std::int32_t y) {
    return source.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) +
                                 static_cast<std::size_t>(x)) *
                                    4U;
  };
  const auto out_at = [&](std::int32_t x, std::int32_t y) {
    return out.rgba.data() +
           (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4U;
  };

  for (std::int32_t dy = 0; dy < target_h; ++dy) {
    for (std::int32_t dx = 0; dx < target_w; ++dx) {
      auto* dst = out_at(dx + offset_x, dy + offset_y);
      if (nearest) {
        const auto sx = std::clamp<std::int32_t>(
            static_cast<std::int32_t>((dx + 0.5) * source.width / target_w), 0, source.width - 1);
        const auto sy = std::clamp<std::int32_t>(
            static_cast<std::int32_t>((dy + 0.5) * source.height / target_h), 0, source.height - 1);
        const auto* src = src_at(sx, sy);
        std::copy(src, src + 4, dst);
        continue;
      }
      const double x0 = static_cast<double>(dx) * source.width / target_w;
      const double x1 = static_cast<double>(dx + 1) * source.width / target_w;
      const double y0 = static_cast<double>(dy) * source.height / target_h;
      const double y1 = static_cast<double>(dy + 1) * source.height / target_h;
      const auto ix0 = static_cast<std::int32_t>(std::floor(x0));
      const auto iy0 = static_cast<std::int32_t>(std::floor(y0));
      const auto ix1 = std::min<std::int32_t>(source.width, static_cast<std::int32_t>(std::ceil(x1)));
      const auto iy1 = std::min<std::int32_t>(source.height, static_cast<std::int32_t>(std::ceil(y1)));
      double sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0, area = 0;
      for (std::int32_t sy = iy0; sy < iy1; ++sy) {
        const double hy = std::min<double>(y1, sy + 1) - std::max<double>(y0, sy);
        for (std::int32_t sx = ix0; sx < ix1; ++sx) {
          const double wx = std::min<double>(x1, sx + 1) - std::max<double>(x0, sx);
          const double weight = wx * hy;
          const auto* src = src_at(sx, sy);
          const double alpha = src[3];
          sum_r += src[0] * alpha * weight;
          sum_g += src[1] * alpha * weight;
          sum_b += src[2] * alpha * weight;
          sum_a += alpha * weight;
          area += weight;
        }
      }
      if (area <= 0) {
        continue;
      }
      const double alpha = sum_a / area;
      if (alpha <= 0) {
        continue;
      }
      dst[0] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(sum_r / sum_a), 0, 255));
      dst[1] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(sum_g / sum_a), 0, 255));
      dst[2] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(sum_b / sum_a), 0, 255));
      dst[3] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(alpha), 0, 255));
    }
  }
  return out;
}

// Finds a pixel layer named exactly "WxH" with matching dimensions anywhere in the layer
// tree (visibility does not matter: imported non-largest sizes are hidden on purpose).
[[nodiscard]] const Layer* find_exact_size_layer(const std::vector<Layer>& layers, int size) {
  const auto expected = std::to_string(size) + "x" + std::to_string(size);
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Pixel && layer.name() == expected && layer.pixels().width() == size &&
        layer.pixels().height() == size) {
      return &layer;
    }
    if (!layer.children().empty()) {
      if (const auto* found = find_exact_size_layer(layer.children(), size); found != nullptr) {
        return found;
      }
    }
  }
  return nullptr;
}

[[nodiscard]] std::vector<std::uint8_t> encode_bmp_entry(const RgbaImage& image) {
  LittleEndianWriter writer;
  const auto width = image.width;
  const auto height = image.height;
  const auto xor_stride = stride_32bit(width, 32);
  const auto and_stride = stride_32bit(width, 1);
  const auto image_size = static_cast<std::uint32_t>((xor_stride + and_stride) * static_cast<std::size_t>(height));

  writer.write_u32(kInfoHeaderSize);
  writer.write_i32(width);
  writer.write_i32(height * 2);
  writer.write_u16(1);
  writer.write_u16(32);
  writer.write_u32(0);  // BI_RGB
  writer.write_u32(image_size);
  writer.write_i32(0);
  writer.write_i32(0);
  writer.write_u32(0);
  writer.write_u32(0);

  const auto src_at = [&](std::int32_t x, std::int32_t y) {
    return image.rgba.data() +
           (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
  };
  for (std::int32_t y = height - 1; y >= 0; --y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* px = src_at(x, y);
      writer.write_u8(px[2]);
      writer.write_u8(px[1]);
      writer.write_u8(px[0]);
      writer.write_u8(px[3]);
    }
  }
  for (std::int32_t y = height - 1; y >= 0; --y) {
    std::uint8_t byte = 0;
    int bit = 0;
    std::size_t written = 0;
    for (std::int32_t x = 0; x < width; ++x) {
      byte = static_cast<std::uint8_t>(byte << 1U);
      if (src_at(x, y)[3] < 128) {
        byte |= 1U;
      }
      if (++bit == 8) {
        writer.write_u8(byte);
        ++written;
        byte = 0;
        bit = 0;
      }
    }
    if (bit != 0) {
      writer.write_u8(static_cast<std::uint8_t>(byte << (8U - static_cast<unsigned>(bit))));
      ++written;
    }
    while (written < and_stride) {
      writer.write_u8(0);
      ++written;
    }
  }
  return std::move(writer.bytes());
}

}  // namespace

void set_png_codec(PngDecodeFn decode, PngEncodeFn encode) {
  png_decoder = decode;
  png_encoder = encode;
}

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < kIconDirSize) {
    return false;
  }
  const auto reserved = static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8U));
  const auto type = static_cast<std::uint16_t>(bytes[2] | (bytes[3] << 8U));
  const auto count = static_cast<std::uint16_t>(bytes[4] | (bytes[5] << 8U));
  return reserved == 0 && (type == kTypeIcon || type == kTypeCursor) && count >= 1;
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  auto reader = ico_reader(bytes);
  const auto reserved = reader.read_u16();
  const auto type = reader.read_u16();
  const auto count = reader.read_u16();
  if (reserved != 0 || (type != kTypeIcon && type != kTypeCursor) || count == 0) {
    throw std::runtime_error("File is not an ICO or CUR image");
  }
  const bool is_cursor = type == kTypeCursor;

  std::vector<DirectoryEntry> entries;
  entries.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    DirectoryEntry entry;
    reader.skip(4);  // width/height/color count/reserved bytes: the bitmap header is authoritative
    entry.hotspot_x = reader.read_u16();
    entry.hotspot_y = reader.read_u16();
    entry.byte_count = reader.read_u32();
    entry.offset = reader.read_u32();
    entries.push_back(entry);
  }

  struct DecodedEntry {
    RgbaImage image;
    std::uint16_t hotspot_x{0};
    std::uint16_t hotspot_y{0};
  };
  std::vector<DecodedEntry> decoded;
  int skipped_png = 0;
  int skipped_bad = 0;
  for (const auto& entry : entries) {
    if (entry.offset > bytes.size() || entry.byte_count > bytes.size() - entry.offset) {
      ++skipped_bad;
      continue;
    }
    const auto payload = bytes.subspan(entry.offset, entry.byte_count);
    try {
      if (has_png_signature(payload)) {
        if (png_decoder == nullptr) {
          ++skipped_png;
          continue;
        }
        auto image = png_decoder(payload);
        if (image.width <= 0 || image.height <= 0 ||
            image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4U) {
          ++skipped_bad;
          continue;
        }
        decoded.push_back({std::move(image), entry.hotspot_x, entry.hotspot_y});
      } else {
        decoded.push_back({decode_bmp_entry(payload), entry.hotspot_x, entry.hotspot_y});
      }
    } catch (const std::exception&) {
      ++skipped_bad;
    }
  }
  if (decoded.empty()) {
    throw std::runtime_error("ICO file contains no readable images");
  }

  // Canvas covers the largest entry; every entry becomes a layer named "WxH" with only the
  // largest visible, ordered so the largest sits on top of the layer stack.
  std::stable_sort(decoded.begin(), decoded.end(), [](const DecodedEntry& a, const DecodedEntry& b) {
    return static_cast<std::int64_t>(a.image.width) * a.image.height <
           static_cast<std::int64_t>(b.image.width) * b.image.height;
  });
  const auto& largest = decoded.back();

  Document document(largest.image.width, largest.image.height, PixelFormat::rgba8());
  std::set<std::string> used_names;
  for (std::size_t i = 0; i < decoded.size(); ++i) {
    const auto& entry = decoded[i];
    auto name = std::to_string(entry.image.width) + "x" + std::to_string(entry.image.height);
    if (!used_names.insert(name).second) {
      int suffix = 2;
      while (!used_names.insert(name + " (" + std::to_string(suffix) + ")").second) {
        ++suffix;
      }
      name += " (" + std::to_string(suffix) + ")";
    }
    Layer layer(document.allocate_layer_id(), std::move(name), pixels_from_rgba(entry.image));
    layer.set_visible(i + 1 == decoded.size());
    if (is_cursor) {
      layer.metadata()[kLayerMetadataCursorHotspot] =
          std::to_string(entry.hotspot_x) + "," + std::to_string(entry.hotspot_y);
    }
    document.add_layer(std::move(layer));
  }

  if (notices != nullptr) {
    if (skipped_png > 0) {
      notices->push_back("Skipped " + std::to_string(skipped_png) + " PNG-compressed icon entries (no PNG decoder)");
    }
    if (skipped_bad > 0) {
      notices->push_back("Skipped " + std::to_string(skipped_bad) + " damaged icon entries");
    }
    if (decoded.size() > 1) {
      notices->push_back("Each icon size imported as its own layer; only the largest is visible");
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open ICO file");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return read(bytes, notices);
}

std::vector<std::uint8_t> DocumentIo::write(const Document& document, WriteOptions options) {
  std::vector<int> sizes;
  for (const auto size : options.sizes) {
    if (size >= 1 && size <= kMaxIconSize &&
        std::find(sizes.begin(), sizes.end(), size) == sizes.end()) {
      sizes.push_back(size);
    }
  }
  std::sort(sizes.begin(), sizes.end());
  if (sizes.empty()) {
    throw std::runtime_error("No icon sizes selected");
  }

  std::optional<RgbaImage> flattened;
  const auto source_for_size = [&](int size) -> RgbaImage {
    if (const auto* exact = find_exact_size_layer(document.layers(), size); exact != nullptr) {
      return rgba_from_pixels(exact->pixels());
    }
    if (!flattened.has_value()) {
      flattened = rgba_from_pixels(flatten_document_rgba8(document));
    }
    if (flattened->width == size && flattened->height == size) {
      return *flattened;
    }
    return resample_to_square(*flattened, size, options.nearest_neighbor);
  };

  const auto largest_size = sizes.back();
  struct PendingEntry {
    int size{0};
    std::vector<std::uint8_t> payload;
  };
  std::vector<PendingEntry> pending;
  pending.reserve(sizes.size());
  for (const auto size : sizes) {
    const auto image = source_for_size(size);
    // The Vista+ convention stores the 256 px entry PNG-compressed; everything smaller is a
    // classic 32-bit BMP entry with an AND mask.
    if (size >= kMaxIconSize && png_encoder != nullptr) {
      pending.push_back({size, png_encoder(image)});
    } else {
      pending.push_back({size, encode_bmp_entry(image)});
    }
  }

  LittleEndianWriter writer;
  writer.write_u16(0);
  writer.write_u16(options.as_cursor ? kTypeCursor : kTypeIcon);
  writer.write_u16(static_cast<std::uint16_t>(pending.size()));
  auto offset = static_cast<std::uint32_t>(kIconDirSize + kIconDirEntrySize * pending.size());
  for (const auto& entry : pending) {
    writer.write_u8(entry.size >= kMaxIconSize ? 0 : static_cast<std::uint8_t>(entry.size));
    writer.write_u8(entry.size >= kMaxIconSize ? 0 : static_cast<std::uint8_t>(entry.size));
    writer.write_u8(0);
    writer.write_u8(0);
    if (options.as_cursor) {
      const auto scale_hotspot = [&](int value) {
        const auto scaled = static_cast<int>(std::lround(static_cast<double>(value) * entry.size / largest_size));
        return static_cast<std::uint16_t>(std::clamp(scaled, 0, entry.size - 1));
      };
      writer.write_u16(scale_hotspot(options.hotspot_x));
      writer.write_u16(scale_hotspot(options.hotspot_y));
    } else {
      writer.write_u16(1);
      writer.write_u16(32);
    }
    writer.write_u32(static_cast<std::uint32_t>(entry.payload.size()));
    writer.write_u32(offset);
    offset += static_cast<std::uint32_t>(entry.payload.size());
  }
  for (const auto& entry : pending) {
    writer.write_bytes(entry.payload);
  }
  return std::move(writer.bytes());
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path, WriteOptions options) {
  const auto bytes = write(document, options);
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open ICO file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write ICO file");
  }
}

}  // namespace patchy::ico
