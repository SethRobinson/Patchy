#pragma once

// Builds MS-NRBF streams and Paint.NET .pdn files byte by byte for the pdn_ tests
// (core and UI suites). The graph mirrors what Paint.NET 4.x writes (the record
// types, member names and order observed in sample files; docs/pdn-format.md), so the
// importer meets the same shapes without any third-party file.

#include "formats/miniz/miniz.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::test::pdn_build {

// MS-NRBF writer: just the records the tests need, written exactly as the spec lays
// them out.
class NrbfWriter {
public:
  std::vector<std::uint8_t> bytes;

  void u8(std::uint8_t value) { bytes.push_back(value); }
  void i32(std::int32_t value) {
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) >> (8 * i)) & 0xFFU));
    }
  }
  void i64(std::int64_t value) {
    for (int i = 0; i < 8; ++i) {
      bytes.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) >> (8 * i)) & 0xFFU));
    }
  }
  void string(std::string_view text) {
    auto length = static_cast<std::uint32_t>(text.size());
    do {
      std::uint8_t byte = length & 0x7FU;
      length >>= 7U;
      if (length != 0) {
        byte |= 0x80U;
      }
      bytes.push_back(byte);
    } while (length != 0);
    bytes.insert(bytes.end(), text.begin(), text.end());
  }

  void header(std::int32_t root_id = 1) {
    u8(0);
    i32(root_id);
    i32(-1);
    i32(1);
    i32(0);
  }
  void library(std::int32_t id, std::string_view name) {
    u8(12);
    i32(id);
    string(name);
  }
  void message_end() { u8(11); }
  void reference(std::int32_t id) {
    u8(9);
    i32(id);
  }
  void null() { u8(10); }
  void null_run(std::uint8_t count) {
    u8(13);
    u8(count);
  }
  void string_object(std::int32_t id, std::string_view text) {
    u8(6);
    i32(id);
    string(text);
  }

  // Member type for ClassWithMembersAndTypes: binary type + additional info.
  struct Member {
    std::string name;
    std::uint8_t binary_type;  // 0 primitive, 1 string, 2 object, 3 system class, 4 class
    std::uint8_t primitive{0};
    std::string class_name{};
    std::int32_t library{0};
  };
  static Member prim(std::string name, std::uint8_t primitive) { return {std::move(name), 0, primitive}; }
  static Member str(std::string name) { return {std::move(name), 1}; }
  static Member obj(std::string name) { return {std::move(name), 2}; }
  static Member sys(std::string name, std::string class_name) { return {std::move(name), 3, 0, std::move(class_name)}; }
  static Member cls(std::string name, std::string class_name, std::int32_t library) {
    return {std::move(name), 4, 0, std::move(class_name), library};
  }

  // Record 5 (or 4 when library is 0): the caller writes the member values next.
  void class_with_types(std::int32_t id, std::string_view name, const std::vector<Member>& members,
                        std::int32_t library) {
    u8(library == 0 ? 4 : 5);
    i32(id);
    string(name);
    i32(static_cast<std::int32_t>(members.size()));
    for (const auto& member : members) {
      string(member.name);
    }
    for (const auto& member : members) {
      u8(member.binary_type);
    }
    for (const auto& member : members) {
      if (member.binary_type == 0 || member.binary_type == 7) {
        u8(member.primitive);
      } else if (member.binary_type == 3) {
        string(member.class_name);
      } else if (member.binary_type == 4) {
        string(member.class_name);
        i32(member.library);
      }
    }
    if (library != 0) {
      i32(library);
    }
  }
  void class_with_id(std::int32_t id, std::int32_t metadata_id) {
    u8(1);
    i32(id);
    i32(metadata_id);
  }
  void object_array(std::int32_t id, std::int32_t count) {
    u8(16);
    i32(id);
    i32(count);
  }
  void byte_array(std::int32_t id, const std::vector<std::uint8_t>& data) {
    u8(15);
    i32(id);
    i32(static_cast<std::int32_t>(data.size()));
    u8(2);
    bytes.insert(bytes.end(), data.begin(), data.end());
  }
};

enum PrimitiveCode : std::uint8_t { kBool = 1, kByte = 2, kInt32 = 8, kInt64 = 9 };

struct LayerSpec {
  std::string name{"Layer"};
  bool visible{true};
  bool is_background{false};
  std::uint8_t opacity{255};
  int blend{0};                  // LayerBlendMode value__ (written when >= 0)
  std::string blend_op{"Normal"};  // the BitmapLayerProperties blendOp class suffix
  std::vector<std::uint8_t> bgra;  // stride * height bytes
  std::int32_t stride{0};          // 0: width * 4
};

struct PdnSpec {
  std::int32_t width{4};
  std::int32_t height{3};
  std::vector<LayerSpec> layers;
  std::vector<std::pair<std::string, std::string>> metadata;
  std::uint8_t chunk_format{0};    // 0 gzip, 1 raw
  std::uint32_t chunk_size{256 * 1024};
  bool reverse_chunks{false};
  bool share_first_block{false};   // every layer's surface points at layer 0's block
  std::vector<std::uint8_t> thumbnail_png;
  bool gzip_whole_graph{false};    // PDN3 header + 1F 8B payload
  bool omit_header{false};         // Paint.NET 1.x/2.x: only the gzipped graph
  bool write_blend_mode{true};     // false: the 3.x shape, blend only as the blendOp class
};

inline std::vector<std::uint8_t> gzip(std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> out{0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0xFF};
  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(data.size())) + 16;
  std::vector<std::uint8_t> zlib(bound);
  if (mz_compress2(zlib.data(), &bound, data.data(), static_cast<mz_ulong>(data.size()), 6) != MZ_OK) {
    return {};
  }
  // zlib wrapper: 2-byte header, deflate body, 4-byte adler32. Keep the body only.
  out.insert(out.end(), zlib.begin() + 2, zlib.begin() + static_cast<std::ptrdiff_t>(bound) - 4);
  const auto crc = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
  const auto size = static_cast<std::uint32_t>(data.size());
  for (const auto value : {crc, size}) {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
    }
  }
  return out;
}

inline void put_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int i = 3; i >= 0; --i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
  }
}

// The graph plus deferred chunks: everything after the "00 01" marker.
inline std::vector<std::uint8_t> build_graph(const PdnSpec& spec) {
  NrbfWriter w;
  w.header(1);
  w.library(2, "PaintDotNet.Data, Version=4.21.6589.7045, Culture=neutral, PublicKeyToken=null");
  using M = NrbfWriter;
  const auto count = static_cast<std::int32_t>(spec.layers.size());
  w.class_with_types(1, "PaintDotNet.Document",
                     {M::prim("isDisposed", kBool), M::cls("layers", "PaintDotNet.LayerList", 2),
                      M::prim("width", kInt32), M::prim("height", kInt32), M::sys("savedWith", "System.Version"),
                      M::obj("userMetadataItems")},
                     2);
  w.u8(0);
  w.reference(3);
  w.i32(spec.width);
  w.i32(spec.height);
  w.reference(4);
  w.reference(5);
  w.class_with_types(3, "PaintDotNet.LayerList",
                     {M::cls("parent", "PaintDotNet.Document", 2), M::obj("ArrayList+_items"),
                      M::prim("ArrayList+_size", kInt32), M::prim("ArrayList+_version", kInt32)},
                     2);
  w.reference(1);
  w.reference(6);
  w.i32(count);
  w.i32(count);
  w.class_with_types(4, "System.Version",
                     {M::prim("_Major", kInt32), M::prim("_Minor", kInt32), M::prim("_Build", kInt32),
                      M::prim("_Revision", kInt32)},
                     0);
  w.i32(4);
  w.i32(21);
  w.i32(6589);
  w.i32(7045);
  // userMetadataItems: KeyValuePair<string, string>[]
  w.object_array(5, static_cast<std::int32_t>(spec.metadata.size()));
  std::int32_t next_id = 1000;
  bool pair_class_written = false;
  std::int32_t pair_metadata = 0;
  for (const auto& [key, value] : spec.metadata) {
    const auto id = -(next_id++);
    if (!pair_class_written) {
      w.class_with_types(id, "System.Collections.Generic.KeyValuePair`2[[System.String],[System.String]]",
                         {M::str("key"), M::str("value")}, 0);
      pair_metadata = id;
      pair_class_written = true;
    } else {
      w.class_with_id(id, pair_metadata);
    }
    w.string_object(next_id++, key);
    w.string_object(next_id++, value);
  }
  // The layer list's backing array is padded with nulls past _size, like ArrayList's.
  const std::int32_t capacity = count + 2;
  w.object_array(6, capacity);
  const std::int32_t first_layer_id = 100;
  for (std::int32_t i = 0; i < count; ++i) {
    w.reference(first_layer_id + i * 10);
  }
  w.null_run(2);
  w.library(7, "PaintDotNet.Core, Version=4.21.6589.7045, Culture=neutral, PublicKeyToken=null");
  std::vector<std::int32_t> block_order;
  std::int32_t bitmap_meta = 0;
  std::int32_t blp_meta = 0;
  std::int32_t surface_meta = 0;
  std::int32_t lp_meta = 0;
  std::int32_t block_meta = 0;
  std::int32_t mode_meta = 0;
  for (std::int32_t i = 0; i < count; ++i) {
    const auto base = first_layer_id + i * 10;
    if (bitmap_meta == 0) {
      w.class_with_types(base, "PaintDotNet.BitmapLayer",
                         {M::cls("properties", "PaintDotNet.BitmapLayer+BitmapLayerProperties", 7),
                          M::cls("surface", "PaintDotNet.Surface", 7), M::prim("Layer+isDisposed", kBool),
                          M::prim("Layer+width", kInt32), M::prim("Layer+height", kInt32),
                          M::cls("Layer+properties", "PaintDotNet.Layer+LayerProperties", 7)},
                         7);
      bitmap_meta = base;
    } else {
      w.class_with_id(base, bitmap_meta);
    }
    w.reference(base + 1);
    w.reference(base + 2);
    w.u8(0);
    w.i32(spec.width);
    w.i32(spec.height);
    w.reference(base + 3);
  }
  for (std::int32_t i = 0; i < count; ++i) {
    const auto base = first_layer_id + i * 10;
    const auto& layer = spec.layers[static_cast<std::size_t>(i)];
    const auto stride = layer.stride != 0 ? layer.stride : spec.width * 4;
    // BitmapLayerProperties {blendOp}
    if (blp_meta == 0) {
      w.class_with_types(base + 1, "PaintDotNet.BitmapLayer+BitmapLayerProperties", {M::obj("blendOp")}, 7);
      blp_meta = base + 1;
    } else {
      w.class_with_id(base + 1, blp_meta);
    }
    w.reference(base + 4);
    // Surface {width, height, stride, scan0}
    if (surface_meta == 0) {
      w.class_with_types(base + 2, "PaintDotNet.Surface",
                         {M::prim("width", kInt32), M::prim("height", kInt32), M::prim("stride", kInt32),
                          M::cls("scan0", "PaintDotNet.MemoryBlock", 7)},
                         7);
      surface_meta = base + 2;
    } else {
      w.class_with_id(base + 2, surface_meta);
    }
    w.i32(spec.width);
    w.i32(spec.height);
    w.i32(stride);
    w.reference(spec.share_first_block ? first_layer_id + 5 : base + 5);
    // LayerProperties {name, userMetadataItems, visible, isBackground, opacity, blendMode}
    std::vector<M::Member> lp_members{M::str("name"), M::obj("userMetadataItems"), M::prim("visible", kBool),
                                      M::prim("isBackground", kBool), M::prim("opacity", kByte)};
    if (spec.write_blend_mode) {
      lp_members.push_back(M::cls("blendMode", "PaintDotNet.LayerBlendMode", 7));
    }
    if (lp_meta == 0) {
      w.class_with_types(base + 3, "PaintDotNet.Layer+LayerProperties", lp_members, 7);
      lp_meta = base + 3;
    } else {
      w.class_with_id(base + 3, lp_meta);
    }
    w.string_object(base + 6, layer.name);
    w.null();
    w.u8(layer.visible ? 1 : 0);
    w.u8(layer.is_background ? 1 : 0);
    w.u8(layer.opacity);
    // LayerBlendMode is a value type: written inline with a negative id.
    if (!spec.write_blend_mode) {
    } else if (mode_meta == 0) {
      w.class_with_types(-(base + 7), "PaintDotNet.LayerBlendMode", {M::prim("value__", kInt32)}, 7);
      mode_meta = -(base + 7);
    } else {
      w.class_with_id(-(base + 7), mode_meta);
    }
    if (spec.write_blend_mode) {
      w.i32(layer.blend);
    }
    // The blend op: an empty class per op type.
    w.class_with_types(base + 4, "PaintDotNet.UserBlendOps+" + layer.blend_op + "BlendOp", {}, 7);
    // MemoryBlock {length64, hasParent, deferred}
    if (!spec.share_first_block || i == 0) {
      if (block_meta == 0) {
        w.class_with_types(base + 5, "PaintDotNet.MemoryBlock",
                           {M::prim("length64", kInt64), M::prim("hasParent", kBool), M::prim("deferred", kBool)},
                           7);
        block_meta = base + 5;
      } else {
        w.class_with_id(base + 5, block_meta);
      }
      w.i64(static_cast<std::int64_t>(layer.bgra.size()));
      w.u8(0);
      w.u8(1);
      block_order.push_back(i);
    }
  }
  w.message_end();
  auto out = std::move(w.bytes);
  for (const auto index : block_order) {
    const auto& data = spec.layers[static_cast<std::size_t>(index)].bgra;
    out.push_back(spec.chunk_format);
    put_u32_be(out, spec.chunk_size);
    const auto chunk_count = (data.size() + spec.chunk_size - 1) / spec.chunk_size;
    std::vector<std::size_t> order(chunk_count);
    for (std::size_t c = 0; c < chunk_count; ++c) {
      order[c] = spec.reverse_chunks ? chunk_count - 1 - c : c;
    }
    for (const auto c : order) {
      const auto begin = c * spec.chunk_size;
      const auto size = std::min<std::size_t>(spec.chunk_size, data.size() - begin);
      const std::span<const std::uint8_t> piece(data.data() + begin, size);
      const auto stored = spec.chunk_format == 0 ? gzip(piece) : std::vector<std::uint8_t>(piece.begin(), piece.end());
      put_u32_be(out, static_cast<std::uint32_t>(c));
      put_u32_be(out, static_cast<std::uint32_t>(stored.size()));
      out.insert(out.end(), stored.begin(), stored.end());
    }
  }
  return out;
}

inline std::string base64(std::span<const std::uint8_t> data) {
  static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (std::size_t i = 0; i < data.size(); i += 3) {
    const std::uint32_t a = data[i];
    const std::uint32_t b = i + 1 < data.size() ? data[i + 1] : 0;
    const std::uint32_t c = i + 2 < data.size() ? data[i + 2] : 0;
    const auto triple = (a << 16U) | (b << 8U) | c;
    out.push_back(kTable[(triple >> 18U) & 63U]);
    out.push_back(kTable[(triple >> 12U) & 63U]);
    out.push_back(i + 1 < data.size() ? kTable[(triple >> 6U) & 63U] : '=');
    out.push_back(i + 2 < data.size() ? kTable[triple & 63U] : '=');
  }
  return out;
}

inline std::vector<std::uint8_t> build_pdn(const PdnSpec& spec) {
  std::vector<std::uint8_t> payload{0x00, 0x01};
  const auto graph = build_graph(spec);
  payload.insert(payload.end(), graph.begin(), graph.end());
  if (spec.omit_header) {
    return gzip(graph);
  }
  if (spec.gzip_whole_graph) {
    payload = gzip(payload);
  }
  std::string xml = "<pdnImage width=\"" + std::to_string(spec.width) + "\" height=\"" + std::to_string(spec.height) +
                    "\" layers=\"" + std::to_string(spec.layers.size()) +
                    "\" savedWithVersion=\"4.21.6589.7045\"><custom><thumb png=\"" + base64(spec.thumbnail_png) +
                    "\" /></custom></pdnImage>";
  std::vector<std::uint8_t> out{'P', 'D', 'N', '3'};
  const auto length = static_cast<std::uint32_t>(xml.size());
  out.push_back(static_cast<std::uint8_t>(length & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((length >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((length >> 16U) & 0xFFU));
  out.insert(out.end(), xml.begin(), xml.end());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

// A width x height BGRA surface filled by `pixel(x, y)` returning {r, g, b, a}.
template <typename Fn>
std::vector<std::uint8_t> bgra_surface(std::int32_t width, std::int32_t height, Fn pixel, std::int32_t stride = 0) {
  const auto row = stride != 0 ? stride : width * 4;
  std::vector<std::uint8_t> data(static_cast<std::size_t>(row) * static_cast<std::size_t>(height), 0xEE);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto rgba = pixel(x, y);
      auto* at = data.data() + static_cast<std::size_t>(y) * row + static_cast<std::size_t>(x) * 4U;
      at[0] = rgba[2];
      at[1] = rgba[1];
      at[2] = rgba[0];
      at[3] = rgba[3];
    }
  }
  return data;
}

}  // namespace patchy::test::pdn_build
