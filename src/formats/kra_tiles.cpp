// Krita's tiled pixel data (docs/kra.md "Tiles"). Learned from files Krita 5.3 writes: a
// text header ("VERSION 2", "TILEWIDTH 64", "TILEHEIGHT 64", "PIXELSIZE n", "DATA count"),
// then per tile a "x,y,LZF,size" line and `size` bytes: a flag byte (1 = LZF compressed,
// 0 = stored) and the tile's samples. The samples are planar: every pixel's first byte,
// then every pixel's second byte, and so on (each byte of a multi-byte sample is its own
// plane), in the color space's channel order.

#include "formats/kra_document_io.hpp"
#include "formats/kra_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>

namespace patchy::kra {

namespace detail {

bool lzf_decompress(std::span<const std::uint8_t> in, std::span<std::uint8_t> out) noexcept {
  std::size_t ip = 0;
  std::size_t op = 0;
  while (ip < in.size()) {
    const unsigned control = in[ip++];
    if (control < 32U) {
      // A literal run of control + 1 bytes.
      const std::size_t length = control + 1U;
      if (ip + length > in.size() || op + length > out.size()) {
        return false;
      }
      std::memcpy(out.data() + op, in.data() + ip, length);
      ip += length;
      op += length;
      continue;
    }
    // A back reference: length in the top three bits (7 means a further length byte),
    // distance in the low five bits and the next byte.
    std::size_t length = control >> 5U;
    if (length == 7U) {
      if (ip >= in.size()) {
        return false;
      }
      length += in[ip++];
    }
    length += 2U;
    if (ip >= in.size()) {
      return false;
    }
    const std::size_t distance = ((static_cast<std::size_t>(control) & 0x1FU) << 8U) + in[ip++] + 1U;
    if (distance > op || op + length > out.size()) {
      return false;
    }
    // Byte by byte: the source may overlap what is being written (a repeating run).
    std::size_t from = op - distance;
    for (std::size_t i = 0; i < length; ++i) {
      out[op++] = out[from++];
    }
  }
  return op == out.size();
}

int ColorSpace::channels() const noexcept {
  switch (model) {
    case ColorModel::Rgb:
    case ColorModel::Lab:
      return 4;
    case ColorModel::Gray:
      return 2;
    case ColorModel::Cmyk:
      return 5;
    case ColorModel::Alpha:
      return 1;
    case ColorModel::Unsupported:
      break;
  }
  return 0;
}

int ColorSpace::sample_bytes() const noexcept {
  switch (sample) {
    case SampleType::U8:
      return 1;
    case SampleType::U16:
    case SampleType::F16:
      return 2;
    case SampleType::F32:
      return 4;
  }
  return 1;
}

ColorSpace parse_color_space(std::string_view name) {
  ColorSpace space;
  space.name = std::string(name);
  std::string_view rest;
  const auto starts = [&](std::string_view prefix) {
    if (name.substr(0, prefix.size()) == prefix) {
      rest = name.substr(prefix.size());
      return true;
    }
    return false;
  };
  if (starts("RGBA")) {
    space.model = ColorModel::Rgb;
  } else if (starts("GRAYA")) {
    space.model = ColorModel::Gray;
  } else if (starts("CMYKA") || starts("CMYK")) {
    space.model = ColorModel::Cmyk;
  } else if (starts("LABA")) {
    space.model = ColorModel::Lab;
  } else if (name == "ALPHA") {
    space.model = ColorModel::Alpha;
    rest = {};
  } else {
    return space;
  }
  if (rest.empty() || rest == "U8") {
    space.sample = SampleType::U8;
  } else if (rest == "16" || rest == "U16") {
    space.sample = SampleType::U16;
  } else if (rest == "F16") {
    space.sample = SampleType::F16;
  } else if (rest == "F32") {
    space.sample = SampleType::F32;
  } else {
    space.model = ColorModel::Unsupported;
  }
  // Krita's 8-bit Lab is not a Lab space it writes; Lab is 16-bit or float.
  if (space.model == ColorModel::Lab && space.sample == SampleType::U8 && rest.empty()) {
    space.sample = SampleType::U16;
  }
  return space;
}

float half_to_float(std::uint16_t bits) noexcept {
  const std::uint32_t sign = static_cast<std::uint32_t>(bits & 0x8000U) << 16U;
  std::uint32_t exponent = (bits >> 10U) & 0x1FU;
  std::uint32_t mantissa = bits & 0x3FFU;
  std::uint32_t out = 0;
  if (exponent == 0U) {
    if (mantissa == 0U) {
      out = sign;
    } else {
      // Subnormal: normalize.
      int shift = 0;
      while ((mantissa & 0x400U) == 0U) {
        mantissa <<= 1U;
        ++shift;
      }
      mantissa &= 0x3FFU;
      out = sign | (static_cast<std::uint32_t>(127 - 15 - shift + 1) << 23U) | (mantissa << 13U);
    }
  } else if (exponent == 31U) {
    out = sign | 0x7F800000U | (mantissa << 13U);
  } else {
    out = sign | ((exponent + 127U - 15U) << 23U) | (mantissa << 13U);
  }
  float value = 0.0F;
  std::memcpy(&value, &out, sizeof(value));
  return value;
}

namespace {

// One header or tile line: the bytes up to the next newline. False at the end of the data
// or for a line longer than any real header line.
[[nodiscard]] bool next_line(std::span<const std::uint8_t> data, std::size_t& pos, std::string_view& line) {
  const auto start = pos;
  const auto limit = std::min(data.size(), start + 256U);
  for (auto i = start; i < limit; ++i) {
    if (data[i] == '\n') {
      line = std::string_view(reinterpret_cast<const char*>(data.data() + start), i - start);
      pos = i + 1;
      return true;
    }
  }
  return false;
}

template <typename T>
[[nodiscard]] bool parse_int(std::string_view text, T& value) {
  while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) {
    text.remove_suffix(1);
  }
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  const auto* first = text.data();
  const auto* last = text.data() + text.size();
  const auto result = std::from_chars(first, last, value);
  return result.ec == std::errc() && result.ptr == last;
}

struct TileRecord {
  std::int64_t x{0};
  std::int64_t y{0};
  std::size_t offset{0};
  std::size_t size{0};
};

}  // namespace

std::optional<RawImage> read_tiles(std::span<const std::uint8_t> file, int expected_pixel_bytes,
                                   std::vector<std::uint8_t> default_pixel, std::int32_t offset_x,
                                   std::int32_t offset_y, const TileReadLimits& limits, std::uint64_t& pixel_budget,
                                   std::string& reason) {
  std::size_t pos = 0;
  int tile_width = 64;
  int tile_height = 64;
  int pixel_bytes = expected_pixel_bytes;
  std::int64_t count = -1;
  std::string_view line;
  int header_lines = 0;
  while (count < 0) {
    if (!next_line(file, pos, line) || ++header_lines > 16) {
      reason = "a damaged tile header";
      return std::nullopt;
    }
    const auto space = line.find(' ');
    const auto key = line.substr(0, space);
    const auto value = space == std::string_view::npos ? std::string_view{} : line.substr(space + 1);
    bool ok = true;
    if (key == "VERSION") {
      int version = 0;
      ok = parse_int(value, version) && version == 2;
      if (!ok) {
        reason = "an unknown tile format version";
        return std::nullopt;
      }
    } else if (key == "TILEWIDTH") {
      ok = parse_int(value, tile_width);
    } else if (key == "TILEHEIGHT") {
      ok = parse_int(value, tile_height);
    } else if (key == "PIXELSIZE") {
      ok = parse_int(value, pixel_bytes);
    } else if (key == "DATA") {
      ok = parse_int(value, count);
    }
    if (!ok) {
      reason = "a damaged tile header";
      return std::nullopt;
    }
  }
  if (tile_width <= 0 || tile_height <= 0 || tile_width > 4096 || tile_height > 4096 ||
      pixel_bytes != expected_pixel_bytes || count > 50'000'000) {
    reason = "an unexpected tile layout";
    return std::nullopt;
  }
  if (default_pixel.size() != static_cast<std::size_t>(pixel_bytes)) {
    default_pixel.assign(static_cast<std::size_t>(pixel_bytes), 0);
  }

  std::vector<TileRecord> records;
  records.reserve(static_cast<std::size_t>(count));
  std::int64_t left = std::numeric_limits<std::int64_t>::max();
  std::int64_t top = left;
  std::int64_t right = std::numeric_limits<std::int64_t>::min();
  std::int64_t bottom = right;
  for (std::int64_t index = 0; index < count; ++index) {
    if (!next_line(file, pos, line)) {
      reason = "a damaged tile list";
      return std::nullopt;
    }
    // x,y,COMPRESSION,size
    std::string_view parts[4];
    std::size_t field = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size() && field < 4; ++i) {
      if (i == line.size() || line[i] == ',') {
        parts[field++] = line.substr(start, i - start);
        start = i + 1;
      }
    }
    TileRecord record;
    std::uint64_t size = 0;
    if (field != 4 || !parse_int(parts[0], record.x) || !parse_int(parts[1], record.y) ||
        !parse_int(parts[3], size) || size == 0 || size > file.size() - pos) {
      reason = "a damaged tile list";
      return std::nullopt;
    }
    if (parts[2] != "LZF") {
      reason = "an unknown tile compression";
      return std::nullopt;
    }
    record.x += offset_x;
    record.y += offset_y;
    if (std::abs(record.x) > limits.max_side || std::abs(record.y) > limits.max_side) {
      reason = "a tile far outside the canvas";
      return std::nullopt;
    }
    record.offset = pos;
    record.size = static_cast<std::size_t>(size);
    pos += record.size;
    left = std::min(left, record.x);
    top = std::min(top, record.y);
    right = std::max(right, record.x + tile_width);
    bottom = std::max(bottom, record.y + tile_height);
    records.push_back(record);
  }

  RawImage image;
  image.pixel_bytes = pixel_bytes;
  image.default_pixel = default_pixel;
  image.default_is_transparent = std::all_of(default_pixel.begin(), default_pixel.end(),
                                             [](std::uint8_t byte) { return byte == 0; });
  if (records.empty()) {
    return image;
  }
  const auto width = right - left;
  const auto height = bottom - top;
  const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (width > limits.max_side || height > limits.max_side || pixels > limits.max_pixels || pixels > pixel_budget) {
    reason = "too large";
    return std::nullopt;
  }
  pixel_budget -= pixels;
  image.bounds = Rect{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                      static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)};
  const auto pb = static_cast<std::size_t>(pixel_bytes);
  image.data.resize(static_cast<std::size_t>(pixels) * pb);
  // Fill with the default pixel first: gaps between stored tiles show it.
  if (image.default_is_transparent) {
    std::fill(image.data.begin(), image.data.end(), std::uint8_t{0});
  } else {
    for (std::size_t i = 0; i < static_cast<std::size_t>(pixels); ++i) {
      std::memcpy(image.data.data() + i * pb, default_pixel.data(), pb);
    }
  }
  const auto tile_pixels = static_cast<std::size_t>(tile_width) * static_cast<std::size_t>(tile_height);
  std::vector<std::uint8_t> planar(tile_pixels * pb);
  const auto row_bytes = static_cast<std::size_t>(width) * pb;
  for (const auto& record : records) {
    const auto blob = file.subspan(record.offset, record.size);
    const auto payload = blob.subspan(1);
    if (blob[0] == 1) {
      if (!lzf_decompress(payload, planar)) {
        reason = "a damaged tile";
        return std::nullopt;
      }
    } else if (blob[0] == 0 && payload.size() == planar.size()) {
      std::memcpy(planar.data(), payload.data(), planar.size());
    } else {
      reason = "a damaged tile";
      return std::nullopt;
    }
    const auto origin_x = static_cast<std::size_t>(record.x - left);
    const auto origin_y = static_cast<std::size_t>(record.y - top);
    for (int ty = 0; ty < tile_height; ++ty) {
      auto* row = image.data.data() + (origin_y + static_cast<std::size_t>(ty)) * row_bytes + origin_x * pb;
      const auto plane_row = static_cast<std::size_t>(ty) * static_cast<std::size_t>(tile_width);
      for (std::size_t plane = 0; plane < pb; ++plane) {
        const auto* source = planar.data() + plane * tile_pixels + plane_row;
        auto* target = row + plane;
        for (int tx = 0; tx < tile_width; ++tx) {
          target[static_cast<std::size_t>(tx) * pb] = source[tx];
        }
      }
    }
  }
  return image;
}

}  // namespace detail

}  // namespace patchy::kra
