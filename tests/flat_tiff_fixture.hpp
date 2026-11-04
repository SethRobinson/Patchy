#pragma once

// Synthetic flat TIFFs (docs/tiff.md, "Flat fallback decoder") for the core and UI suites:
// either byte order, chunky or planar, strips or tiles, no compression, LZW (libtiff's
// encoder rules), Deflate or PackBits, predictors 2 and 3, any sample kind the decoder
// reads, extra samples, a color map, and several pages.

// miniz's zlib-compatibility aliases would rename identifiers; keep only the mz_* names.
#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif
#include "formats/miniz/miniz.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::test::flat_tiff {

// U12: 12-bit samples packed most significant bit first (no predictor).
enum class Kind { U8, U12, U16, U32, F16, F32, F64 };

inline std::size_t kind_bytes(Kind kind) {
  switch (kind) {
    case Kind::U8: return 1;
    case Kind::U12:
    case Kind::U16:
    case Kind::F16: return 2;
    case Kind::U32:
    case Kind::F32: return 4;
    case Kind::F64: return 8;
  }
  return 1;
}

inline bool is_float(Kind kind) { return kind == Kind::F16 || kind == Kind::F32 || kind == Kind::F64; }

// Exact for the k/256 values the tests use.
inline std::uint16_t float_to_half(double value) {
  if (value == 0.0) {
    return 0;
  }
  const int exponent = static_cast<int>(std::floor(std::log2(value)));
  const auto mantissa = static_cast<std::uint16_t>(std::lround((value / std::ldexp(1.0, exponent) - 1.0) * 1024.0));
  return static_cast<std::uint16_t>(((exponent + 15) << 10) | mantissa);
}

struct Spec {
  bool little{true};
  std::uint32_t width{9};
  std::uint32_t height{7};
  std::uint16_t photometric{2};
  Kind kind{Kind::U8};
  std::uint16_t samples{3};
  std::vector<std::uint16_t> extra;  // ExtraSamples values (omitted when empty)
  bool planar{false};
  std::uint16_t compression{1};
  std::uint16_t predictor{1};
  std::uint32_t rows_per_strip{0};  // 0: one strip
  std::uint32_t tile{0};            // tile side, 0: strips
  std::vector<std::uint16_t> color_map;
  std::string page_name;            // PageName (285), omitted when empty
  std::uint32_t subfile_type{0};    // NewSubfileType (254), omitted when 0
  // The sample value: integers at the file's scale, floats as they are.
  std::function<double(std::uint32_t x, std::uint32_t y, std::uint32_t sample)> value;
};

struct Writer {
  bool little{true};
  std::vector<std::uint8_t> bytes;
  void uint(std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
      const auto shift = little ? 8U * index : 8U * (width - 1U - index);
      bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
  void put_u32_at(std::size_t at, std::uint32_t value) {
    for (std::size_t index = 0; index < 4U; ++index) {
      const auto shift = little ? 8U * index : 8U * (3U - index);
      bytes[at + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
  }
};

inline void encode_sample(std::vector<std::uint8_t>& out, Kind kind, double value, bool little) {
  std::uint64_t raw = 0;
  switch (kind) {
    case Kind::U8:
    case Kind::U12:
    case Kind::U16:
    case Kind::U32: raw = static_cast<std::uint64_t>(std::llround(value)); break;
    case Kind::F16: raw = float_to_half(value); break;
    case Kind::F32: raw = std::bit_cast<std::uint32_t>(static_cast<float>(value)); break;
    case Kind::F64: raw = std::bit_cast<std::uint64_t>(value); break;
  }
  const auto width = kind_bytes(kind);
  for (std::size_t index = 0; index < width; ++index) {
    const auto shift = little ? 8U * index : 8U * (width - 1U - index);
    out.push_back(static_cast<std::uint8_t>((raw >> shift) & 0xFFU));
  }
}

inline void apply_predictor(std::vector<std::uint8_t>& data, std::size_t row_bytes, std::size_t stride, const Spec& spec) {
  const auto width = kind_bytes(spec.kind);
  for (std::size_t start = 0; start + row_bytes <= data.size(); start += row_bytes) {
    auto* row = data.data() + start;
    if (spec.predictor == 2U) {
      const auto get = [&](std::size_t index) {
        std::uint64_t value = 0;
        for (std::size_t byte = 0; byte < width; ++byte) {
          const auto shift = spec.little ? 8U * byte : 8U * (width - 1U - byte);
          value |= static_cast<std::uint64_t>(row[index * width + byte]) << shift;
        }
        return value;
      };
      const auto put = [&](std::size_t index, std::uint64_t value) {
        for (std::size_t byte = 0; byte < width; ++byte) {
          const auto shift = spec.little ? 8U * byte : 8U * (width - 1U - byte);
          row[index * width + byte] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        }
      };
      for (auto index = row_bytes / width; index-- > stride;) {
        put(index, get(index) - get(index - stride));
      }
    } else if (spec.predictor == 3U) {
      // Byte planes, most significant first, then byte differencing.
      const auto values = row_bytes / width;
      std::vector<std::uint8_t> planes(row_bytes);
      for (std::size_t index = 0; index < values; ++index) {
        for (std::size_t byte = 0; byte < width; ++byte) {
          const auto plane = spec.little ? width - 1U - byte : byte;
          planes[plane * values + index] = row[index * width + byte];
        }
      }
      for (auto index = row_bytes; index-- > stride;) {
        planes[index] = static_cast<std::uint8_t>(planes[index] - planes[index - stride]);
      }
      std::memcpy(row, planes.data(), row_bytes);
    }
  }
}

inline std::vector<std::uint8_t> pack_bits(const std::vector<std::uint8_t>& in) {
  std::vector<std::uint8_t> out;
  std::size_t at = 0;
  while (at < in.size()) {
    std::size_t run = 1;
    while (at + run < in.size() && run < 128U && in[at + run] == in[at]) {
      ++run;
    }
    if (run >= 3U) {
      out.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(1 - static_cast<int>(run))));
      out.push_back(in[at]);
      at += run;
      continue;
    }
    std::size_t literal = 0;
    while (at + literal < in.size() && literal < 128U &&
           !(at + literal + 2U < in.size() && in[at + literal] == in[at + literal + 1U] &&
             in[at + literal] == in[at + literal + 2U])) {
      ++literal;
    }
    out.push_back(static_cast<std::uint8_t>(literal - 1U));
    out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(at),
               in.begin() + static_cast<std::ptrdiff_t>(at + literal));
    at += literal;
  }
  return out;
}

// libtiff's LZW encoder rules: Clear first, the width grows once the next free code passes
// the current maximum, Clear again at 4094 codes.
inline std::vector<std::uint8_t> lzw(const std::vector<std::uint8_t>& in) {
  std::vector<std::uint8_t> out;
  std::uint64_t accumulator = 0;
  int pending = 0;
  int width = 9;
  const auto put = [&](std::uint32_t code) {
    accumulator = (accumulator << width) | code;
    pending += width;
    while (pending >= 8) {
      out.push_back(static_cast<std::uint8_t>((accumulator >> (pending - 8)) & 0xFFU));
      pending -= 8;
    }
  };
  std::map<std::pair<std::uint32_t, std::uint8_t>, std::uint32_t> table;
  std::uint32_t next = 258;
  put(256);
  if (!in.empty()) {
    std::uint32_t current = in[0];
    const auto grow = [&] {
      if (next == 4094U) {
        put(256);
        table.clear();
        next = 258;
        width = 9;
      } else if (next > (1U << width) - 1U) {
        ++width;
      }
    };
    for (std::size_t index = 1; index < in.size(); ++index) {
      const auto key = std::make_pair(current, in[index]);
      if (const auto found = table.find(key); found != table.end()) {
        current = found->second;
        continue;
      }
      put(current);
      table[key] = next++;
      grow();
      current = in[index];
    }
    put(current);
    ++next;
    grow();
  }
  put(257);
  if (pending > 0) {
    out.push_back(static_cast<std::uint8_t>((accumulator << (8 - pending)) & 0xFFU));
  }
  return out;
}

inline std::vector<std::uint8_t> compress(const std::vector<std::uint8_t>& data, std::uint16_t compression) {
  switch (compression) {
    case 5: return lzw(data);
    case 8: {
      auto bound = mz_compressBound(static_cast<mz_ulong>(data.size()));
      std::vector<std::uint8_t> out(static_cast<std::size_t>(bound));
      if (mz_compress2(out.data(), &bound, data.data(), static_cast<mz_ulong>(data.size()), 6) != MZ_OK) {
        throw std::runtime_error("deflate failed");
      }
      out.resize(static_cast<std::size_t>(bound));
      return out;
    }
    case 32773: return pack_bits(data);
    default: return data;
  }
}

struct Entry {
  std::uint16_t tag;
  std::uint16_t type;
  std::vector<std::uint32_t> values;
  std::string text{};  // type 2 (ASCII) values, written with their NUL
};

// One page's data segments and directory entries, appended to `writer`.
inline std::vector<Entry> append_page(Writer& writer, const Spec& spec) {
  const auto planes = spec.planar ? spec.samples : 1U;
  const auto per_segment_samples = spec.planar ? 1U : spec.samples;
  const auto segment_width = spec.tile != 0U ? spec.tile : spec.width;
  const auto segment_height = spec.tile != 0U ? spec.tile : (spec.rows_per_strip != 0U ? spec.rows_per_strip : spec.height);
  const auto across = (spec.width + segment_width - 1U) / segment_width;
  const auto down = (spec.height + segment_height - 1U) / segment_height;
  std::vector<std::uint32_t> offsets;
  std::vector<std::uint32_t> counts;
  for (std::uint32_t plane = 0; plane < planes; ++plane) {
    for (std::uint32_t row = 0; row < down; ++row) {
      for (std::uint32_t column = 0; column < across; ++column) {
        const auto top = row * segment_height;
        const auto rows = spec.tile != 0U ? segment_height : std::min(segment_height, spec.height - top);
        std::vector<std::uint8_t> data;
        for (std::uint32_t y = top; y < top + rows; ++y) {
          std::vector<std::uint16_t> packed_row;
          for (std::uint32_t x = column * segment_width; x < column * segment_width + segment_width; ++x) {
            for (std::uint32_t sample = 0; sample < per_segment_samples; ++sample) {
              const auto which = spec.planar ? plane : sample;
              const bool inside = x < spec.width && y < spec.height;
              const double value = inside ? spec.value(x, y, which) : 0.0;
              if (spec.kind == Kind::U12) {
                packed_row.push_back(static_cast<std::uint16_t>(std::llround(value)) & 0x0FFFU);
              } else {
                encode_sample(data, spec.kind, value, spec.little);
              }
            }
          }
          // 12-bit rows: most significant bit first, each row starting on a byte.
          for (std::size_t index = 0; index < packed_row.size(); index += 2U) {
            const auto first = packed_row[index];
            const auto second = index + 1U < packed_row.size() ? packed_row[index + 1U] : std::uint16_t{0};
            data.push_back(static_cast<std::uint8_t>(first >> 4U));
            data.push_back(static_cast<std::uint8_t>(((first & 0x0FU) << 4U) | (second >> 8U)));
            if (index + 1U < packed_row.size()) {
              data.push_back(static_cast<std::uint8_t>(second & 0xFFU));
            }
          }
        }
        apply_predictor(data, static_cast<std::size_t>(segment_width) * per_segment_samples * kind_bytes(spec.kind),
                        per_segment_samples, spec);
        const auto encoded = compress(data, spec.compression);
        offsets.push_back(static_cast<std::uint32_t>(writer.bytes.size()));
        counts.push_back(static_cast<std::uint32_t>(encoded.size()));
        writer.bytes.insert(writer.bytes.end(), encoded.begin(), encoded.end());
        if (writer.bytes.size() % 2U != 0U) {
          writer.bytes.push_back(0);
        }
      }
    }
  }
  const auto bits = spec.kind == Kind::U12 ? 12U : static_cast<std::uint32_t>(kind_bytes(spec.kind) * 8U);
  const std::uint32_t format = is_float(spec.kind) ? 3U : 1U;
  std::vector<Entry> entries{
      {256, 4, {spec.width}},
      {257, 4, {spec.height}},
      {258, 3, std::vector<std::uint32_t>(spec.samples, bits)},
      {259, 3, {spec.compression}},
      {262, 3, {spec.photometric}},
      {277, 3, {spec.samples}},
      {282, 5, {300, 1}},
      {283, 5, {300, 1}},
      {284, 3, {spec.planar ? 2U : 1U}},
      {296, 3, {2}},
      {339, 3, std::vector<std::uint32_t>(spec.samples, format)},
  };
  if (spec.tile != 0U) {
    entries.push_back({322, 3, {spec.tile}});
    entries.push_back({323, 3, {spec.tile}});
    entries.push_back({324, 4, offsets});
    entries.push_back({325, 4, counts});
  } else {
    entries.push_back({273, 4, offsets});
    entries.push_back({278, 4, {segment_height}});
    entries.push_back({279, 4, counts});
  }
  if (spec.predictor != 1U) {
    entries.push_back({317, 3, {spec.predictor}});
  }
  if (!spec.color_map.empty()) {
    entries.push_back({320, 3, std::vector<std::uint32_t>(spec.color_map.begin(), spec.color_map.end())});
  }
  if (!spec.extra.empty()) {
    entries.push_back({338, 3, std::vector<std::uint32_t>(spec.extra.begin(), spec.extra.end())});
  }
  if (!spec.page_name.empty()) {
    entries.push_back({285, 2, {}, spec.page_name});
  }
  if (spec.subfile_type != 0U) {
    entries.push_back({254, 4, {spec.subfile_type}});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.tag < b.tag; });
  return entries;
}

// Writes a directory after its out-of-line values; returns {directory offset, offset of its
// next-directory field}.
inline std::pair<std::size_t, std::size_t> append_directory(Writer& writer, const std::vector<Entry>& entries) {
  // Out-of-line values first.
  std::vector<std::uint32_t> value_offsets;
  for (const auto& entry : entries) {
    const auto width = entry.type == 3 ? 2U : entry.type == 5 ? 8U : 4U;
    const auto size = entry.type == 2   ? entry.text.size() + 1U
                      : entry.type == 5 ? 8U * (entry.values.size() / 2U)
                                        : width * entry.values.size();
    if (size <= 4U) {
      value_offsets.push_back(0);
      continue;
    }
    value_offsets.push_back(static_cast<std::uint32_t>(writer.bytes.size()));
    if (entry.type == 2) {
      writer.bytes.insert(writer.bytes.end(), entry.text.begin(), entry.text.end());
      writer.bytes.push_back(0);
      if (writer.bytes.size() % 2U != 0U) {
        writer.bytes.push_back(0);
      }
      continue;
    }
    for (const auto value : entry.values) {
      writer.uint(value, entry.type == 3 ? 2U : 4U);
    }
  }
  const auto at = writer.bytes.size();
  writer.uint(entries.size(), 2);
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    writer.uint(entry.tag, 2);
    writer.uint(entry.type, 2);
    writer.uint(entry.type == 2   ? entry.text.size() + 1U
                : entry.type == 5 ? entry.values.size() / 2U
                                  : entry.values.size(),
                4);
    if (value_offsets[index] != 0U) {
      writer.uint(value_offsets[index], 4);
      continue;
    }
    std::size_t written = 0;
    if (entry.type == 2) {
      writer.bytes.insert(writer.bytes.end(), entry.text.begin(), entry.text.end());
      written = entry.text.size();
    }
    for (const auto value : entry.values) {
      writer.uint(value, entry.type == 3 ? 2U : 4U);
      written += entry.type == 3 ? 2U : 4U;
    }
    for (; written < 4U; ++written) {
      writer.bytes.push_back(0);
    }
  }
  const auto next = writer.bytes.size();
  writer.uint(0, 4);
  return {at, next};
}

inline std::vector<std::uint8_t> write_tiff(const std::vector<Spec>& pages) {
  Writer writer;
  writer.little = pages.front().little;
  writer.bytes = {static_cast<std::uint8_t>(writer.little ? 'I' : 'M'), static_cast<std::uint8_t>(writer.little ? 'I' : 'M')};
  writer.uint(42, 2);
  writer.uint(0, 4);
  std::size_t link = 4;
  for (const auto& page : pages) {
    const auto entries = append_page(writer, page);
    if (writer.bytes.size() % 2U != 0U) {
      writer.bytes.push_back(0);
    }
    const auto [directory_at, next_field] = append_directory(writer, entries);
    writer.put_u32_at(link, static_cast<std::uint32_t>(directory_at));
    link = next_field;
  }
  return writer.bytes;
}

inline std::vector<std::uint8_t> write_tiff(const Spec& spec) { return write_tiff(std::vector<Spec>{spec}); }

}  // namespace patchy::test::flat_tiff
