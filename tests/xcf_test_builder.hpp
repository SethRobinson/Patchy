#pragma once

// Builds GIMP .xcf bytes in code for the XCF reader tests (docs/xcf.md). The layout follows
// the published XCF specification: header, image properties, pointer lists, then the
// layer, channel, hierarchy, level and tile records. Pixel data is given as interleaved
// big-endian component bytes and written as raw, RLE or zlib tiles.

#include "formats/miniz/miniz.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace patchy::test::xcf {

enum Prop : std::uint32_t {
  kColormap = 1,
  kActiveLayer = 2,
  kSelection = 4,
  kFloatingSelection = 5,
  kOpacity = 6,
  kMode = 7,
  kVisible = 8,
  kLockAlpha = 10,
  kApplyMask = 11,
  kOffsets = 15,
  kCompression = 17,
  kGuides = 18,
  kResolution = 19,
  kParasites = 21,
  kUnit = 22,
  kPaths = 23,
  kUserUnit = 24,
  kVectors = 25,
  kTextLayerFlags = 26,
  kLockContent = 28,
  kGroupItem = 29,
  kItemPath = 30,
  kGroupItemFlags = 31,
  kLockPosition = 32,
  kFloatOpacity = 33,
  kColorTag = 34,
  kCompositeMode = 35,
  kFloatColor = 38,
  kFilterArgument = 45,
};

class Bytes {
public:
  std::vector<std::uint8_t> data;

  void u8(std::uint8_t v) { data.push_back(v); }
  void u32(std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>(v >> shift));
  }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void u64(std::uint64_t v) {
    u32(static_cast<std::uint32_t>(v >> 32U));
    u32(static_cast<std::uint32_t>(v));
  }
  void f32(float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    u32(bits);
  }
  void string(const std::string& s) {
    if (s.empty()) {
      u32(0);
      return;
    }
    u32(static_cast<std::uint32_t>(s.size() + 1));
    data.insert(data.end(), s.begin(), s.end());
    data.push_back(0);
  }
  void append(const std::vector<std::uint8_t>& bytes) { data.insert(data.end(), bytes.begin(), bytes.end()); }
  void put_u32(std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) data[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
  }
  void put_u64(std::size_t at, std::uint64_t v) {
    put_u32(at, static_cast<std::uint32_t>(v >> 32U));
    put_u32(at + 4, static_cast<std::uint32_t>(v));
  }
};

struct Property {
  std::uint32_t type{0};
  std::vector<std::uint8_t> payload;
  std::optional<std::uint32_t> stored_length;  // a wrong length word, as some GIMP versions wrote
};

[[nodiscard]] inline Property prop_u32(std::uint32_t type, std::uint32_t value) {
  Bytes b;
  b.u32(value);
  return {type, b.data, std::nullopt};
}

[[nodiscard]] inline Property prop_f32(std::uint32_t type, float value) {
  Bytes b;
  b.f32(value);
  return {type, b.data, std::nullopt};
}

[[nodiscard]] inline Property prop_empty(std::uint32_t type) { return {type, {}, std::nullopt}; }

[[nodiscard]] inline Property prop_offsets(std::int32_t x, std::int32_t y) {
  Bytes b;
  b.i32(x);
  b.i32(y);
  return {kOffsets, b.data, std::nullopt};
}

[[nodiscard]] inline Property prop_item_path(const std::vector<std::uint32_t>& path) {
  Bytes b;
  for (const auto v : path) b.u32(v);
  return {kItemPath, b.data, std::nullopt};
}

struct Parasite {
  std::string name;
  std::vector<std::uint8_t> data;
};

[[nodiscard]] inline Property prop_parasites(const std::vector<Parasite>& parasites) {
  Bytes b;
  for (const auto& p : parasites) {
    b.string(p.name);
    b.u32(1);
    b.u32(static_cast<std::uint32_t>(p.data.size()));
    b.append(p.data);
  }
  return {kParasites, b.data, std::nullopt};
}

[[nodiscard]] inline Parasite text_parasite(const std::string& text) {
  return {"gimp-text-layer", std::vector<std::uint8_t>(text.begin(), text.end())};
}

struct Drawable {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t bpp{4};
  std::vector<std::uint8_t> pixels;  // width * height * bpp, interleaved
  bool no_tiles{false};               // a level with no tile pointers (claims without data)
};

struct Channel {
  std::string name{"Channel"};
  std::vector<Property> properties;
  Drawable drawable;
};

struct Effect {
  std::string name;
  std::string operation;
  std::vector<Property> properties;
};

struct Layer {
  std::string name{"Layer"};
  std::uint32_t type{1};  // 0 RGB, 1 RGBA, 2 gray, 3 gray+alpha, 4 indexed, 5 indexed+alpha
  std::vector<Property> properties;
  Drawable drawable;
  std::optional<Channel> mask;
  std::vector<Effect> effects;
  // Index into the layer list for PROP_FLOATING_SELECTION (resolved to its offset).
  std::optional<std::size_t> floating_target;
};

enum class Compression : std::uint8_t { None = 0, Rle = 1, Zlib = 2 };

struct Image {
  int version{11};
  std::uint32_t width{8};
  std::uint32_t height{8};
  std::uint32_t base_type{0};
  std::uint32_t precision{150};
  Compression compression{Compression::Rle};
  std::vector<Property> properties;  // besides PROP_COMPRESSION, which the builder writes
  std::vector<Layer> layers;         // top first, depth first, as GIMP lists them
  std::vector<Channel> channels;
  std::vector<std::vector<std::uint8_t>> path_structures;  // version 18+ path records, raw
  bool omit_compression_property{false};
};

[[nodiscard]] inline std::vector<std::uint8_t> rle_stream(const std::uint8_t* data, std::size_t count,
                                                          std::size_t stride) {
  std::vector<std::uint8_t> out;
  std::size_t i = 0;
  while (i < count) {
    std::size_t run = 1;
    while (i + run < count && run < 32768 && data[(i + run) * stride] == data[i * stride]) ++run;
    if (run >= 3) {
      if (run <= 127) {
        out.push_back(static_cast<std::uint8_t>(run - 1));
      } else {
        out.push_back(127);
        out.push_back(static_cast<std::uint8_t>(run >> 8U));
        out.push_back(static_cast<std::uint8_t>(run));
      }
      out.push_back(data[i * stride]);
      i += run;
      continue;
    }
    std::size_t literal = 0;
    while (i + literal < count && literal < 127) {
      if (i + literal + 2 < count && data[(i + literal) * stride] == data[(i + literal + 1) * stride] &&
          data[(i + literal) * stride] == data[(i + literal + 2) * stride]) {
        break;
      }
      ++literal;
    }
    out.push_back(static_cast<std::uint8_t>(256 - literal));
    for (std::size_t k = 0; k < literal; ++k) out.push_back(data[(i + k) * stride]);
    i += literal;
  }
  return out;
}

[[nodiscard]] inline std::vector<std::uint8_t> encode_tile(const Drawable& d, std::uint32_t tx, std::uint32_t ty,
                                                           std::uint32_t tw, std::uint32_t th, Compression compression) {
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(tw) * th * d.bpp);
  for (std::uint32_t y = 0; y < th; ++y) {
    const auto* row = d.pixels.data() + (static_cast<std::size_t>(ty + y) * d.width + tx) * d.bpp;
    raw.insert(raw.end(), row, row + static_cast<std::size_t>(tw) * d.bpp);
  }
  if (compression == Compression::None) return raw;
  if (compression == Compression::Zlib) {
    mz_ulong size = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<std::uint8_t> out(size);
    mz_compress(out.data(), &size, raw.data(), static_cast<mz_ulong>(raw.size()));
    out.resize(size);
    return out;
  }
  std::vector<std::uint8_t> out;
  for (std::uint32_t c = 0; c < d.bpp; ++c) {
    const auto stream = rle_stream(raw.data() + c, static_cast<std::size_t>(tw) * th, d.bpp);
    out.insert(out.end(), stream.begin(), stream.end());
  }
  return out;
}

class Writer {
public:
  explicit Writer(const Image& image) : image_(image) {}

  [[nodiscard]] std::vector<std::uint8_t> write() {
    Bytes& b = out_;
    const std::string tag = image_.version == 0 ? std::string("file") : [&] {
      char buffer[8];
      std::snprintf(buffer, sizeof(buffer), "v%03d", image_.version);
      return std::string(buffer);
    }();
    for (const char c : std::string("gimp xcf ")) b.u8(static_cast<std::uint8_t>(c));
    for (const char c : tag) b.u8(static_cast<std::uint8_t>(c));
    b.u8(0);
    b.u32(image_.width);
    b.u32(image_.height);
    b.u32(image_.base_type);
    if (image_.version >= 4) b.u32(image_.precision);
    std::vector<Property> props = image_.properties;
    if (!image_.omit_compression_property) {
      props.insert(props.begin(), Property{kCompression, {static_cast<std::uint8_t>(image_.compression)}, std::nullopt});
    }
    write_properties(props);
    // Pointer lists, patched once the records are placed.
    std::vector<std::size_t> layer_slots;
    for (std::size_t i = 0; i < image_.layers.size(); ++i) layer_slots.push_back(pointer_slot());
    write_pointer(0);
    std::vector<std::size_t> channel_slots;
    for (std::size_t i = 0; i < image_.channels.size(); ++i) channel_slots.push_back(pointer_slot());
    write_pointer(0);
    std::vector<std::size_t> path_slots;
    if (image_.version >= 18) {
      for (std::size_t i = 0; i < image_.path_structures.size(); ++i) path_slots.push_back(pointer_slot());
      write_pointer(0);
    }
    // Layers first get offsets so floating selections can point at their targets: a
    // layer's record offset is known only once it is written, so the floating property is
    // patched afterwards.
    std::vector<std::uint64_t> layer_offsets(image_.layers.size());
    std::vector<std::pair<std::size_t, std::size_t>> floating_patches;
    for (std::size_t i = 0; i < image_.layers.size(); ++i) {
      layer_offsets[i] = b.data.size();
      patch_pointer(layer_slots[i], layer_offsets[i]);
      const auto patch = write_layer(image_.layers[i]);
      if (patch.has_value()) floating_patches.emplace_back(*patch, *image_.layers[i].floating_target);
    }
    for (const auto& [slot, target] : floating_patches) patch_pointer(slot, layer_offsets[target]);
    for (std::size_t i = 0; i < image_.channels.size(); ++i) {
      patch_pointer(channel_slots[i], b.data.size());
      write_channel(image_.channels[i]);
    }
    for (std::size_t i = 0; i < path_slots.size(); ++i) {
      patch_pointer(path_slots[i], b.data.size());
      b.append(image_.path_structures[i]);
    }
    return b.data;
  }

private:
  [[nodiscard]] bool wide() const { return image_.version >= 11; }
  [[nodiscard]] std::size_t pointer_slot() {
    const auto at = out_.data.size();
    write_pointer(0);
    return at;
  }
  void write_pointer(std::uint64_t v) {
    if (wide()) out_.u64(v); else out_.u32(static_cast<std::uint32_t>(v));
  }
  void patch_pointer(std::size_t at, std::uint64_t v) {
    if (wide()) out_.put_u64(at, v); else out_.put_u32(at, static_cast<std::uint32_t>(v));
  }
  void write_properties(const std::vector<Property>& props) {
    for (const auto& p : props) {
      out_.u32(p.type);
      out_.u32(p.stored_length.value_or(static_cast<std::uint32_t>(p.payload.size())));
      out_.append(p.payload);
    }
    out_.u32(0);
    out_.u32(0);
  }
  // Writes the hierarchy, level and tiles of `d` right here; returns the hierarchy offset.
  std::uint64_t write_hierarchy(const Drawable& d) {
    Bytes& b = out_;
    const auto hierarchy = b.data.size();
    b.u32(d.width);
    b.u32(d.height);
    b.u32(d.bpp);
    const auto level_slot = pointer_slot();
    write_pointer(0);
    patch_pointer(level_slot, b.data.size());
    b.u32(d.width);
    b.u32(d.height);
    if (d.no_tiles) {
      write_pointer(0);
      return hierarchy;
    }
    const auto columns = (d.width + 63) / 64;
    const auto rows = (d.height + 63) / 64;
    std::vector<std::size_t> tile_slots;
    for (std::uint32_t i = 0; i < columns * rows; ++i) tile_slots.push_back(pointer_slot());
    write_pointer(0);
    for (std::uint32_t r = 0; r < rows; ++r) {
      for (std::uint32_t c = 0; c < columns; ++c) {
        patch_pointer(tile_slots[r * columns + c], b.data.size());
        const auto tw = std::min<std::uint32_t>(64, d.width - c * 64);
        const auto th = std::min<std::uint32_t>(64, d.height - r * 64);
        b.append(encode_tile(d, c * 64, r * 64, tw, th, image_.compression));
      }
    }
    return hierarchy;
  }
  void write_channel(const Channel& channel) {
    Bytes& b = out_;
    b.u32(channel.drawable.width);
    b.u32(channel.drawable.height);
    b.string(channel.name);
    write_properties(channel.properties);
    const auto slot = pointer_slot();
    patch_pointer(slot, write_hierarchy(channel.drawable));
  }
  std::optional<std::size_t> write_layer(const Layer& layer) {
    Bytes& b = out_;
    b.u32(layer.drawable.width);
    b.u32(layer.drawable.height);
    b.u32(layer.type);
    b.string(layer.name);
    std::optional<std::size_t> floating_slot;
    for (const auto& p : layer.properties) {
      b.u32(p.type);
      b.u32(p.stored_length.value_or(static_cast<std::uint32_t>(p.payload.size())));
      b.append(p.payload);
    }
    if (layer.floating_target.has_value()) {
      b.u32(kFloatingSelection);
      b.u32(wide() ? 8 : 4);
      floating_slot = pointer_slot();
    }
    b.u32(0);
    b.u32(0);
    const auto hierarchy_slot = pointer_slot();
    const auto mask_slot = pointer_slot();
    std::vector<std::size_t> effect_slots;
    if (image_.version >= 20) {
      for (std::size_t i = 0; i < layer.effects.size(); ++i) effect_slots.push_back(pointer_slot());
      write_pointer(0);
    }
    patch_pointer(hierarchy_slot, write_hierarchy(layer.drawable));
    if (layer.mask.has_value()) {
      patch_pointer(mask_slot, b.data.size());
      write_channel(*layer.mask);
    }
    for (std::size_t i = 0; i < effect_slots.size(); ++i) {
      patch_pointer(effect_slots[i], b.data.size());
      const auto& effect = layer.effects[i];
      b.string(effect.name);
      b.string("gimp-image");
      b.string(effect.operation);
      if (image_.version >= 22) b.string("0:0");
      write_properties(effect.properties);
      write_pointer(0);
    }
    return floating_slot;
  }

  const Image& image_;
  Bytes out_;
};

[[nodiscard]] inline std::vector<std::uint8_t> write(const Image& image) { return Writer(image).write(); }

// An 8-bit drawable filled with one RGBA (or gray/indexed) value per pixel.
[[nodiscard]] inline Drawable solid(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixel) {
  Drawable d;
  d.width = width;
  d.height = height;
  d.bpp = static_cast<std::uint32_t>(pixel.size());
  for (std::uint32_t i = 0; i < width * height; ++i) d.pixels.insert(d.pixels.end(), pixel.begin(), pixel.end());
  return d;
}

}  // namespace patchy::test::xcf
