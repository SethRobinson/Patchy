#pragma once

// Synthetic layered TIFFs (docs/tiff.md) for the core and UI suites. One encoder writes
// every structural field in either byte order and keeps pixel samples big-endian, the way
// Photoshop writes its IBM PC and Macintosh variants, so a little-endian file must convert
// to exactly the PSD stream its big-endian twin yields.

#include "psd/psd_descriptor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::test::layered_tiff {

// Writes fields in one byte order; four-character codes are u32s, so a little-endian
// encoder writes them reversed ("MIB8").
struct Encoder {
  bool little{false};
  std::vector<std::uint8_t> bytes;

  void u8(std::uint32_t value) { bytes.push_back(static_cast<std::uint8_t>(value)); }
  void uint(std::uint64_t value, int width) {
    for (int index = 0; index < width; ++index) {
      const int shift = little ? 8 * index : 8 * (width - 1 - index);
      bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
  void u16(std::uint32_t value) { uint(value, 2); }
  void u32(std::uint64_t value) { uint(value, 4); }
  void f64(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    uint(bits, 8);
  }
  void ostype(std::string_view code) {
    std::uint32_t value = 0;
    for (const char c : code) {
      value = (value << 8U) | static_cast<std::uint8_t>(c);
    }
    u32(value);
  }
  void raw(std::span<const std::uint8_t> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }
  void unicode(std::u16string_view text) {
    u32(text.size());
    for (const auto unit : text) {
      u16(unit);
    }
  }
  void id(std::string_view code) {
    if (code.size() == 4U) {
      u32(0);
      ostype(code);
    } else {
      u32(code.size());
      raw(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(code.data()), code.size()));
    }
  }
  void pad_to(std::size_t multiple) {
    while (bytes.size() % multiple != 0U) {
      u8(0);
    }
  }
};

struct MaskSpec {
  std::int32_t top, left, bottom, right;
};

struct LayerSpec {
  std::u16string name;
  std::int32_t top{0}, left{0}, bottom{0}, right{0};
  std::string blend{"norm"};
  std::uint8_t opacity{255};
  bool hidden{false};
  bool alpha{true};
  bool rle{true};
  std::uint32_t section{0};  // lsct type: 0 none, 1 open folder, 3 bounding divider
  std::optional<MaskSpec> mask;
  bool red_overlay{false};
  std::uint8_t seed{0};
};

inline std::uint8_t sample(const LayerSpec& layer, int x, int y, int channel) {
  return static_cast<std::uint8_t>(x * 31 + y * 17 + channel * 70 + layer.seed);
}

// One plane's channel data: compression code, then RLE row counts and PackBits rows, or
// raw samples. Samples are big-endian whatever the encoder's order.
inline std::vector<std::uint8_t> channel_data(bool little, const LayerSpec& layer, int channel, std::int32_t width,
                                       std::int32_t height, int depth) {
  const auto bytes_per_sample = depth / 8;
  std::vector<std::vector<std::uint8_t>> rows;
  for (int y = 0; y < height; ++y) {
    std::vector<std::uint8_t> row;
    for (int x = 0; x < width; ++x) {
      const auto value = channel == -1 ? static_cast<std::uint8_t>(255 - x * 9) : sample(layer, x, y, channel);
      if (depth == 8) {
        row.push_back(value);
      } else if (depth == 16) {
        const auto wide = static_cast<std::uint16_t>(value * 257U);
        row.push_back(static_cast<std::uint8_t>(wide >> 8U));
        row.push_back(static_cast<std::uint8_t>(wide & 0xFFU));
      } else {
        const float scaled = static_cast<float>(value) / 255.0F;
        std::uint32_t bits = 0;
        std::memcpy(&bits, &scaled, sizeof(bits));
        for (int shift = 24; shift >= 0; shift -= 8) {
          row.push_back(static_cast<std::uint8_t>((bits >> static_cast<unsigned>(shift)) & 0xFFU));
        }
      }
    }
    rows.push_back(std::move(row));
  }
  (void)bytes_per_sample;
  Encoder out{little, {}};
  if (layer.rle) {
    out.u16(1);
    std::vector<std::vector<std::uint8_t>> encoded;
    for (const auto& row : rows) {
      encoded.push_back(patchy::psd::encode_packbits_row(row));
      out.u16(static_cast<std::uint32_t>(encoded.back().size()));
    }
    for (const auto& row : encoded) {
      out.raw(row);
    }
  } else {
    out.u16(0);
    for (const auto& row : rows) {
      out.raw(row);
    }
  }
  return out.bytes;
}

inline std::vector<std::uint8_t> mask_data(bool little, const MaskSpec& mask) {
  Encoder out{little, {}};
  out.u16(1);
  const auto width = mask.right - mask.left;
  const auto height = mask.bottom - mask.top;
  std::vector<std::vector<std::uint8_t>> encoded;
  for (int y = 0; y < height; ++y) {
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x) {
      row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(x < width / 2 ? 255 : 0);
    }
    encoded.push_back(patchy::psd::encode_packbits_row(row));
    out.u16(static_cast<std::uint32_t>(encoded.back().size()));
  }
  for (const auto& row : encoded) {
    out.raw(row);
  }
  return out.bytes;
}

// A layer-effects descriptor with an enabled red Color Overlay ('SoFi').
inline std::vector<std::uint8_t> red_overlay_effects(bool little) {
  Encoder out{little, {}};
  out.u32(0);   // object effects version
  out.u32(16);  // descriptor version
  out.unicode(u"");
  out.id("null");
  out.u32(3);
  out.id("Scl ");
  out.ostype("UntF");
  out.ostype("#Prc");
  out.f64(100.0);
  out.id("masterFXSwitch");
  out.ostype("bool");
  out.u8(1);
  out.id("SoFi");
  out.ostype("Objc");
  out.unicode(u"");
  out.id("SoFi");
  out.u32(4);
  out.id("enab");
  out.ostype("bool");
  out.u8(1);
  out.id("Md  ");
  out.ostype("enum");
  out.id("BlnM");
  out.id("Nrml");
  out.id("Clr ");
  out.ostype("Objc");
  out.unicode(u"");
  out.id("RGBC");
  out.u32(3);
  out.id("Rd  ");
  out.ostype("doub");
  out.f64(255.0);
  out.id("Grn ");
  out.ostype("doub");
  out.f64(0.0);
  out.id("Bl  ");
  out.ostype("doub");
  out.f64(0.0);
  out.id("Opct");
  out.ostype("UntF");
  out.ostype("#Prc");
  out.f64(100.0);
  return out.bytes;
}

inline void tagged_block(Encoder& out, std::string_view key, const std::vector<std::uint8_t>& payload) {
  out.ostype("8BIM");
  out.ostype(key);
  out.u32(payload.size());
  out.raw(payload);
}

// Layer info ('Layr'/'Lr16'/'Lr32' contents): count, records bottom to top, channel data.
inline std::vector<std::uint8_t> layer_info(bool little, const std::vector<LayerSpec>& layers, int depth,
                                     bool merged_transparency) {
  Encoder out{little, {}};
  out.u16(static_cast<std::uint16_t>(merged_transparency ? -static_cast<int>(layers.size())
                                                         : static_cast<int>(layers.size())));
  std::vector<std::vector<std::vector<std::uint8_t>>> planes;
  for (std::size_t index = 0; index < layers.size(); ++index) {
    const auto& layer = layers[index];
    const auto width = layer.right - layer.left;
    const auto height = layer.bottom - layer.top;
    std::vector<std::pair<int, std::vector<std::uint8_t>>> channels;
    if (layer.alpha) {
      channels.emplace_back(-1, channel_data(little, layer, -1, width, height, depth));
    }
    for (int channel = 0; channel < 3; ++channel) {
      channels.emplace_back(channel, channel_data(little, layer, channel, width, height, depth));
    }
    if (layer.mask.has_value()) {
      channels.emplace_back(-2, mask_data(little, *layer.mask));
    }
    out.u32(static_cast<std::uint32_t>(layer.top));
    out.u32(static_cast<std::uint32_t>(layer.left));
    out.u32(static_cast<std::uint32_t>(layer.bottom));
    out.u32(static_cast<std::uint32_t>(layer.right));
    out.u16(static_cast<std::uint32_t>(channels.size()));
    std::vector<std::vector<std::uint8_t>> data;
    for (auto& [id, bytes] : channels) {
      out.u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(id)));
      out.u32(bytes.size());
      data.push_back(std::move(bytes));
    }
    planes.push_back(std::move(data));
    out.ostype("8BIM");
    out.ostype(layer.blend);
    out.u8(layer.opacity);
    out.u8(0);
    out.u8(layer.hidden ? 0x0AU : 0x08U);
    out.u8(0);

    Encoder extra{little, {}};
    if (layer.mask.has_value()) {
      extra.u32(20);
      extra.u32(static_cast<std::uint32_t>(layer.mask->top));
      extra.u32(static_cast<std::uint32_t>(layer.mask->left));
      extra.u32(static_cast<std::uint32_t>(layer.mask->bottom));
      extra.u32(static_cast<std::uint32_t>(layer.mask->right));
      extra.u8(0);  // default color
      extra.u8(0);  // flags
      extra.u16(0);
    } else {
      extra.u32(0);
    }
    extra.u32(0);  // blending ranges
    const std::string ascii = "layer";
    extra.u8(static_cast<std::uint32_t>(ascii.size()));
    extra.raw(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(ascii.data()), ascii.size()));
    extra.pad_to(4);
    {
      Encoder name{little, {}};
      name.unicode(layer.name);
      name.pad_to(2);
      tagged_block(extra, "luni", name.bytes);
    }
    {
      Encoder id{little, {}};
      id.u32(index + 1U);
      tagged_block(extra, "lyid", id.bytes);
    }
    {
      Encoder flags{little, {}};
      flags.u8(1);
      flags.u8(0);
      flags.u8(0);
      flags.u8(0);
      tagged_block(extra, "clbl", flags.bytes);
    }
    if (layer.section != 0U) {
      Encoder section{little, {}};
      section.u32(layer.section);
      if (layer.section == 1U) {
        section.ostype("8BIM");
        section.ostype("pass");
      }
      tagged_block(extra, "lsct", section.bytes);
    }
    if (layer.red_overlay) {
      tagged_block(extra, "lfx2", red_overlay_effects(little));
    }
    {
      Encoder reference{little, {}};
      reference.f64(1.5);
      reference.f64(-2.25);
      tagged_block(extra, "fxrp", reference.bytes);
    }
    out.u32(extra.bytes.size());
    out.raw(extra.bytes);
  }
  for (const auto& layer_planes : planes) {
    for (const auto& plane : layer_planes) {
      out.raw(plane);
    }
  }
  return out.bytes;
}

// Background, a masked Multiply layer with a Unicode name, and a group holding a hidden
// layer with a red Color Overlay; `scale` multiplies every coordinate (8 x 6 at 1).
inline std::vector<LayerSpec> sample_layers(int scale = 1) {
  std::vector<LayerSpec> layers;
  LayerSpec background;
  background.name = u"Background";
  background.bottom = 6;
  background.right = 8;
  background.alpha = false;
  background.seed = 3;
  layers.push_back(background);

  LayerSpec masked;
  masked.name = u"Rot \u00FC \u30C6";
  masked.top = 1;
  masked.left = 2;
  masked.bottom = 5;
  masked.right = 7;
  masked.blend = "mul ";
  masked.opacity = 200;
  masked.rle = false;
  masked.mask = MaskSpec{1, 2, 5, 7};
  masked.seed = 40;
  layers.push_back(masked);

  LayerSpec divider;
  divider.name = u"</Layer group>";
  divider.section = 3;
  divider.alpha = true;
  layers.push_back(divider);

  LayerSpec inner;
  inner.name = u"Inside";
  inner.top = 2;
  inner.left = 0;
  inner.bottom = 4;
  inner.right = 3;
  inner.hidden = true;
  inner.red_overlay = true;
  inner.seed = 90;
  layers.push_back(inner);

  LayerSpec folder;
  folder.name = u"Group";
  folder.section = 1;
  layers.push_back(folder);
  for (auto& layer : layers) {
    layer.top *= scale;
    layer.left *= scale;
    layer.bottom *= scale;
    layer.right *= scale;
    if (layer.mask.has_value()) {
      layer.mask = MaskSpec{layer.mask->top * scale, layer.mask->left * scale, layer.mask->bottom * scale,
                            layer.mask->right * scale};
    }
  }
  return layers;
}

// 37724's contents: the header, the layer block, 'LMsk' and an empty 'Patt', each padded
// to four bytes.
inline std::vector<std::uint8_t> source_data(bool little, const std::vector<LayerSpec>& layers, int depth,
                                      bool merged_transparency) {
  Encoder out{little, {}};
  const std::string_view header("Adobe Photoshop Document Data Block\0", 36);
  out.raw(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(header.data()), header.size()));
  const auto info = layer_info(little, layers, depth, merged_transparency);
  tagged_block(out, depth == 16 ? "Lr16" : depth == 32 ? "Lr32" : "Layr", info);
  out.pad_to(4);
  Encoder mask{little, {}};
  for (int index = 0; index < 5; ++index) {
    mask.u16(index == 0 ? 0 : 65535);
  }
  mask.u16(50);
  mask.u8(128);
  mask.u8(0);
  tagged_block(out, "LMsk", mask.bytes);
  out.pad_to(4);
  tagged_block(out, "Patt", {});
  return out.bytes;
}

// A minimal uncompressed TIFF: the composite (zeros) in the first directory, Photoshop
// resources (a 72 PPI resolution record) and the layer data.
inline std::vector<std::uint8_t> tiff_file(bool little, std::int32_t width, std::int32_t height, int depth, bool alpha,
                                    const std::vector<std::uint8_t>& layer_data, std::uint16_t photometric = 2,
                                    std::uint32_t extra_directories = 0) {
  Encoder out{little, {}};
  out.raw(std::array<std::uint8_t, 2>{static_cast<std::uint8_t>(little ? 'I' : 'M'),
                                      static_cast<std::uint8_t>(little ? 'I' : 'M')});
  out.u16(42);
  out.u32(0);  // patched below
  const std::uint16_t samples = alpha ? 4 : 3;
  const auto sample_bytes = static_cast<std::size_t>(depth / 8);
  const auto strip = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * samples * sample_bytes;

  Encoder resources{false, {}};
  resources.raw(std::array<std::uint8_t, 4>{'8', 'B', 'I', 'M'});
  resources.u16(1005);
  resources.u16(0);
  resources.u32(16);
  resources.u32(72U << 16U);
  resources.u16(1);
  resources.u16(1);
  resources.u32(72U << 16U);
  resources.u16(1);
  resources.u16(1);

  // Out-of-line data first, then the directory.
  const auto bits_at = out.bytes.size();
  for (int index = 0; index < samples; ++index) {
    out.u16(static_cast<std::uint32_t>(depth));
  }
  const auto format_at = out.bytes.size();
  for (int index = 0; index < samples; ++index) {
    out.u16(depth == 32 ? 3 : 1);
  }
  out.pad_to(2);
  const auto strip_at = out.bytes.size();
  out.bytes.resize(out.bytes.size() + strip, 0);
  out.pad_to(2);
  const auto resources_at = out.bytes.size();
  out.raw(resources.bytes);
  out.pad_to(2);
  const auto layers_at = out.bytes.size();
  out.raw(layer_data);
  out.pad_to(2);

  struct Field {
    std::uint16_t tag, type;
    std::uint32_t count, value;
  };
  std::vector<Field> fields{
      {256, 4, 1, static_cast<std::uint32_t>(width)},
      {257, 4, 1, static_cast<std::uint32_t>(height)},
      {258, 3, samples, static_cast<std::uint32_t>(bits_at)},
      {259, 3, 1, 1},
      {262, 3, 1, photometric},
      {273, 4, 1, static_cast<std::uint32_t>(strip_at)},
      {277, 3, 1, samples},
      {278, 4, 1, static_cast<std::uint32_t>(height)},
      {279, 4, 1, static_cast<std::uint32_t>(strip)},
      {284, 3, 1, 1},
  };
  if (alpha) {
    fields.push_back({338, 3, 1, 2});
  }
  fields.push_back({339, 3, samples, static_cast<std::uint32_t>(format_at)});
  fields.push_back({34377, 1, static_cast<std::uint32_t>(resources.bytes.size()), static_cast<std::uint32_t>(resources_at)});
  if (!layer_data.empty()) {
    fields.push_back({37724, 7, static_cast<std::uint32_t>(layer_data.size()), static_cast<std::uint32_t>(layers_at)});
  }
  const auto write_directory = [&out](const std::vector<Field>& entries, std::uint32_t next) {
    out.u16(static_cast<std::uint32_t>(entries.size()));
    for (const auto& field : entries) {
      out.u16(field.tag);
      out.u16(field.type);
      out.u32(field.count);
      const bool inline_short = field.type == 3 && field.count == 1;
      if (inline_short) {
        out.u16(field.value);
        out.u16(0);
      } else {
        out.u32(field.value);
      }
    }
    out.u32(next);
  };
  const auto directory_at = out.bytes.size();
  // Extra directories (more pages; the first is a reduced-resolution preview) chain after
  // the first, each reusing the first page's strip.
  const auto directory_size = [&](std::size_t entry_count) { return 2U + entry_count * 12U + 4U; };
  write_directory(fields, extra_directories > 0 ? static_cast<std::uint32_t>(directory_at + directory_size(fields.size())) : 0U);
  for (std::uint32_t page = 0; page < extra_directories; ++page) {
    std::vector<Field> more{{254, 4, 1, page == 0 ? 1U : 2U}, {256, 4, 1, static_cast<std::uint32_t>(width)},
                            {257, 4, 1, static_cast<std::uint32_t>(height)}};
    const auto here = out.bytes.size();
    write_directory(more, page + 1 < extra_directories ? static_cast<std::uint32_t>(here + directory_size(more.size())) : 0U);
  }
  Encoder offset{little, {}};
  offset.u32(directory_at);
  std::copy(offset.bytes.begin(), offset.bytes.end(), out.bytes.begin() + 4);
  return out.bytes;
}

inline std::vector<std::uint8_t> sample_tiff(bool little, int depth = 8, int scale = 1) {
  return tiff_file(little, 8 * scale, 6 * scale, depth, false, source_data(little, sample_layers(scale), depth, false));
}

}  // namespace patchy::test::layered_tiff
