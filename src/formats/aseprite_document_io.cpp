// Aseprite (.aseprite/.ase) reader/writer, following the published file-format spec
// (https://github.com/aseprite/aseprite/blob/main/docs/ase-file-specs.md). Frame 1 only in
// v1 — animation import is future work. Unknown chunks are skipped by length, never
// parse-or-die. zlib cels go through the vendored miniz (src/formats/miniz/).

#include "formats/aseprite_document_io.hpp"

#include "core/palette.hpp"
#include "formats/binary_le.hpp"

#include "formats/miniz/miniz.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace patchy::aseprite {

namespace {

constexpr std::uint16_t kHeaderMagic = 0xA5E0;
constexpr std::uint16_t kFrameMagic = 0xF1FA;
constexpr std::uint16_t kChunkLayer = 0x2004;
constexpr std::uint16_t kChunkCel = 0x2005;
constexpr std::uint16_t kChunkPaletteNew = 0x2019;
constexpr std::uint16_t kChunkPaletteOld = 0x0004;
constexpr std::uint16_t kChunkPaletteOld2 = 0x0011;
constexpr std::uint8_t kAlphaThreshold = 128;

[[nodiscard]] LittleEndianReader ase_reader(std::span<const std::uint8_t> bytes) {
  return LittleEndianReader(bytes, "Aseprite data ended unexpectedly");
}

// Aseprite blend mode (WORD) <-> Patchy BlendMode. Unmappable modes fall back to Normal
// with an import notice.
[[nodiscard]] std::optional<BlendMode> blend_mode_from_aseprite(std::uint16_t mode) {
  switch (mode) {
    case 0:
      return BlendMode::Normal;
    case 1:
      return BlendMode::Multiply;
    case 2:
      return BlendMode::Screen;
    case 3:
      return BlendMode::Overlay;
    case 4:
      return BlendMode::Darken;
    case 5:
      return BlendMode::Lighten;
    case 6:
      return BlendMode::ColorDodge;
    case 7:
      return BlendMode::ColorBurn;
    case 8:
      return BlendMode::HardLight;
    case 9:
      return BlendMode::SoftLight;
    case 10:
      return BlendMode::Difference;
    case 11:
      return BlendMode::Exclusion;
    case 12:
      return BlendMode::Hue;
    case 13:
      return BlendMode::Saturation;
    case 14:
      return BlendMode::Color;
    case 15:
      return BlendMode::Luminosity;
    case 16:
      return BlendMode::LinearDodge;  // Aseprite "Addition"
    case 17:
      return BlendMode::Subtract;
    case 18:
      return BlendMode::Divide;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::uint16_t blend_mode_to_aseprite(BlendMode mode, bool* lossy) {
  switch (mode) {
    case BlendMode::PassThrough:
    case BlendMode::Normal:
      return 0;
    case BlendMode::Multiply:
      return 1;
    case BlendMode::Screen:
      return 2;
    case BlendMode::Overlay:
      return 3;
    case BlendMode::Darken:
      return 4;
    case BlendMode::Lighten:
      return 5;
    case BlendMode::ColorDodge:
      return 6;
    case BlendMode::ColorBurn:
      return 7;
    case BlendMode::HardLight:
      return 8;
    case BlendMode::SoftLight:
      return 9;
    case BlendMode::Difference:
      return 10;
    case BlendMode::Exclusion:
      return 11;
    case BlendMode::Hue:
      return 12;
    case BlendMode::Saturation:
      return 13;
    case BlendMode::Color:
      return 14;
    case BlendMode::Luminosity:
      return 15;
    case BlendMode::LinearDodge:
      return 16;  // Aseprite "Addition"
    case BlendMode::Subtract:
      return 17;
    case BlendMode::Divide:
      return 18;
    case BlendMode::LinearBurn:
    case BlendMode::PinLight:
      if (lossy != nullptr) {
        *lossy = true;
      }
      return 0;
  }
  return 0;
}

struct AseHeader {
  std::uint16_t frames{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::uint16_t depth{0};  // 32 RGBA, 16 grayscale, 8 indexed
  bool layer_opacity_valid{false};
  // Header flag bit 2: without it, GROUP chunks carry meaningless blend/opacity bytes
  // (Aseprite itself writes opacity 0 there) and must render as Normal at 100%.
  bool group_opacity_valid{false};
  std::uint8_t transparent_index{0};
};

struct AseLayer {
  std::uint16_t flags{0};
  std::uint16_t type{0};  // 0 image, 1 group, 2 tilemap
  std::uint16_t child_level{0};
  std::uint16_t blend_mode{0};
  std::uint8_t opacity{255};
  std::string name;
};

struct AseCel {
  std::uint16_t layer_index{0};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t opacity{255};
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> pixels;  // raw, in the file's color depth
};

[[nodiscard]] std::string read_ase_string(LittleEndianReader& reader) {
  const auto length = reader.read_u16();
  std::string text;
  text.reserve(length);
  for (std::uint16_t i = 0; i < length; ++i) {
    text.push_back(static_cast<char>(reader.read_u8()));
  }
  return text;
}

[[nodiscard]] std::vector<std::uint8_t> inflate_cel(std::span<const std::uint8_t> compressed,
                                                    std::size_t expected_size) {
  std::vector<std::uint8_t> out(expected_size);
  mz_ulong out_length = static_cast<mz_ulong>(expected_size);
  const auto status =
      mz_uncompress(out.data(), &out_length, compressed.data(), static_cast<mz_ulong>(compressed.size()));
  if (status != MZ_OK || out_length != expected_size) {
    throw std::runtime_error("Aseprite cel data failed to decompress");
  }
  return out;
}

void write_ase_string(LittleEndianWriter& writer, const std::string& text) {
  writer.write_u16(static_cast<std::uint16_t>(text.size()));
  for (const auto character : text) {
    writer.write_u8(static_cast<std::uint8_t>(character));
  }
}

}  // namespace

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 6 && (static_cast<std::uint16_t>(bytes[4] | (bytes[5] << 8U)) == kHeaderMagic);
}

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return sniff(bytes);
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  if (bytes.size() >= 4 && bytes[0] == 'A' && bytes[1] == 'S' && bytes[2] == 'E' && bytes[3] == 'F') {
    throw std::runtime_error(
        "This is an Adobe swatch palette (.ase), not an Aseprite image. Load it from the Palette panel's "
        "Load Palette File instead.");
  }
  if (!sniff(bytes)) {
    throw std::runtime_error("File is not an Aseprite image");
  }
  auto reader = ase_reader(bytes);
  AseHeader header;
  reader.skip(4);  // file size
  reader.skip(2);  // magic (verified by sniff)
  header.frames = reader.read_u16();
  header.width = reader.read_u16();
  header.height = reader.read_u16();
  header.depth = reader.read_u16();
  const auto flags = reader.read_u32();
  header.layer_opacity_valid = (flags & 1U) != 0;
  header.group_opacity_valid = (flags & 2U) != 0;
  reader.skip(2 + 4 + 4);  // speed, set-be-0 dwords
  header.transparent_index = reader.read_u8();
  reader.skip(3);
  reader.skip(2);  // number of colors (the palette chunk is authoritative)
  reader.seek(128);

  if (header.width <= 0 || header.height <= 0) {
    throw std::runtime_error("Aseprite image has invalid dimensions");
  }
  if (header.depth != 8 && header.depth != 16 && header.depth != 32) {
    throw std::runtime_error("Aseprite color depth is not supported");
  }
  if (header.frames == 0) {
    throw std::runtime_error("Aseprite file contains no frames");
  }
  const std::size_t bytes_per_pixel = header.depth / 8U;

  // Frame 1 only.
  std::vector<AseLayer> layers;
  std::vector<AseCel> cels;
  std::vector<RgbColor> palette;
  int skipped_tilemaps = 0;
  {
    const auto frame_start = reader.position();
    const auto frame_bytes = reader.read_u32();
    if (reader.read_u16() != kFrameMagic) {
      throw std::runtime_error("Aseprite frame header is damaged");
    }
    const auto old_chunks = reader.read_u16();
    reader.skip(2 + 2);  // duration + future
    const auto new_chunks = reader.read_u32();
    const auto chunk_count = new_chunks != 0 ? new_chunks : old_chunks;
    const auto frame_end = frame_start + frame_bytes;

    for (std::uint32_t chunk = 0; chunk < chunk_count && reader.position() + 6 <= frame_end; ++chunk) {
      const auto chunk_start = reader.position();
      const auto chunk_size = reader.read_u32();
      const auto chunk_type = reader.read_u16();
      if (chunk_size < 6 || chunk_start + chunk_size > bytes.size()) {
        break;  // damaged length: stop parsing rather than run off the end
      }
      const auto chunk_end = chunk_start + chunk_size;

      if (chunk_type == kChunkLayer) {
        AseLayer layer;
        layer.flags = reader.read_u16();
        layer.type = reader.read_u16();
        layer.child_level = reader.read_u16();
        reader.skip(2 + 2);  // default width/height (ignored per spec)
        layer.blend_mode = reader.read_u16();
        layer.opacity = reader.read_u8();
        reader.skip(3);
        layer.name = read_ase_string(reader);
        if (layer.type == 2) {
          ++skipped_tilemaps;
        }
        layers.push_back(std::move(layer));
      } else if (chunk_type == kChunkCel) {
        AseCel cel;
        cel.layer_index = reader.read_u16();
        cel.x = static_cast<std::int16_t>(reader.read_u16());
        cel.y = static_cast<std::int16_t>(reader.read_u16());
        cel.opacity = reader.read_u8();
        const auto cel_type = reader.read_u16();
        reader.skip(2 + 5);  // z-index + future
        if (cel_type == 0 || cel_type == 2) {
          cel.width = reader.read_u16();
          cel.height = reader.read_u16();
          const auto pixel_bytes = static_cast<std::size_t>(cel.width) * static_cast<std::size_t>(cel.height) *
                                   bytes_per_pixel;
          if (cel_type == 0) {
            cel.pixels.resize(pixel_bytes);
            for (auto& byte : cel.pixels) {
              byte = reader.read_u8();
            }
          } else {
            const auto compressed_size = chunk_end - reader.position();
            cel.pixels = inflate_cel(bytes.subspan(reader.position(), compressed_size), pixel_bytes);
          }
          cels.push_back(std::move(cel));
        }
        // Linked cels (type 1) cannot appear in frame 1; compressed tilemaps (type 3) are
        // counted with their tilemap layer above.
      } else if (chunk_type == kChunkPaletteNew) {
        const auto size = reader.read_u32();
        const auto first = reader.read_u32();
        const auto last = reader.read_u32();
        reader.skip(8);
        if (size <= 256 && first <= last && last < 256) {
          if (palette.size() < size) {
            palette.resize(size, RgbColor{0, 0, 0});
          }
          for (std::uint32_t index = first; index <= last; ++index) {
            const auto entry_flags = reader.read_u16();
            const auto red = reader.read_u8();
            const auto green = reader.read_u8();
            const auto blue = reader.read_u8();
            reader.skip(1);  // alpha
            if (index < palette.size()) {
              palette[index] = RgbColor{red, green, blue};
            }
            if ((entry_flags & 1U) != 0) {
              (void)read_ase_string(reader);
            }
          }
        }
      } else if ((chunk_type == kChunkPaletteOld || chunk_type == kChunkPaletteOld2) && palette.empty()) {
        const auto packets = reader.read_u16();
        std::size_t index = 0;
        for (std::uint16_t packet = 0; packet < packets && reader.position() + 2 <= chunk_end; ++packet) {
          index += reader.read_u8();
          auto count = static_cast<std::size_t>(reader.read_u8());
          if (count == 0) {
            count = 256;
          }
          for (std::size_t i = 0; i < count; ++i) {
            const auto red = reader.read_u8();
            const auto green = reader.read_u8();
            const auto blue = reader.read_u8();
            if (index + i >= palette.size()) {
              palette.resize(index + i + 1, RgbColor{0, 0, 0});
            }
            // The 0x0011 variant stores 0..63 values.
            const auto scale = chunk_type == kChunkPaletteOld2 ? 4 : 1;
            palette[index + i] = RgbColor{static_cast<std::uint8_t>(red * scale),
                                          static_cast<std::uint8_t>(green * scale),
                                          static_cast<std::uint8_t>(blue * scale)};
          }
          index += count;
        }
      }
      reader.seek(chunk_end);
    }
  }

  if (layers.empty()) {
    throw std::runtime_error("Aseprite file contains no layers");
  }
  if (header.depth == 8 && palette.empty()) {
    throw std::runtime_error("Indexed Aseprite file is missing its palette");
  }

  // Decode each layer's cel into an RGBA pixel buffer.
  std::unordered_map<std::uint16_t, const AseCel*> cel_by_layer;
  for (const auto& cel : cels) {
    cel_by_layer.emplace(cel.layer_index, &cel);
  }

  Document document(header.width, header.height, PixelFormat::rgba8());
  std::vector<std::string> lossy_blend_layers;

  // Rebuild the tree from child levels: file order is bottom-first, groups precede their
  // children. parents[level] holds the group awaiting children at that depth.
  std::vector<Layer*> parents;
  for (std::size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
    const auto& source = layers[layer_index];
    if (source.type == 2) {
      continue;  // tilemap layers are skipped with a notice
    }
    const bool visible = (source.flags & 1U) != 0;
    const bool background = (source.flags & 8U) != 0;
    const bool is_group = source.type == 1;
    // Group blend/opacity bytes are only meaningful under header flag bit 2; Aseprite
    // writes opacity 0 on groups without it, which must NOT hide the group's children.
    const bool blend_opacity_meaningful = !is_group || header.group_opacity_valid;
    const auto blend = blend_opacity_meaningful ? blend_mode_from_aseprite(source.blend_mode)
                                                : std::optional<BlendMode>(BlendMode::Normal);
    if (!blend.has_value()) {
      lossy_blend_layers.push_back(source.name);
    }

    Layer built = source.type == 1 ? Layer(document.allocate_layer_id(), source.name, LayerKind::Group)
                                   : [&] {
                                       const auto* cel = [&]() -> const AseCel* {
                                         const auto found = cel_by_layer.find(static_cast<std::uint16_t>(layer_index));
                                         return found != cel_by_layer.end() ? found->second : nullptr;
                                       }();
                                       const auto cel_width = cel != nullptr ? cel->width : 1;
                                       const auto cel_height = cel != nullptr ? cel->height : 1;
                                       PixelBuffer pixels(std::max(1, cel_width), std::max(1, cel_height),
                                                          PixelFormat::rgba8());
                                       pixels.clear(0);
                                       if (cel != nullptr) {
                                         for (std::int32_t y = 0; y < cel->height; ++y) {
                                           for (std::int32_t x = 0; x < cel->width; ++x) {
                                             const auto offset = (static_cast<std::size_t>(y) *
                                                                      static_cast<std::size_t>(cel->width) +
                                                                  static_cast<std::size_t>(x)) *
                                                                 bytes_per_pixel;
                                             auto* dst = pixels.pixel(x, y);
                                             if (header.depth == 32) {
                                               dst[0] = cel->pixels[offset];
                                               dst[1] = cel->pixels[offset + 1];
                                               dst[2] = cel->pixels[offset + 2];
                                               dst[3] = cel->pixels[offset + 3];
                                             } else if (header.depth == 16) {
                                               dst[0] = cel->pixels[offset];
                                               dst[1] = cel->pixels[offset];
                                               dst[2] = cel->pixels[offset];
                                               dst[3] = cel->pixels[offset + 1];
                                             } else {
                                               const auto index = cel->pixels[offset];
                                               if (index == header.transparent_index && !background) {
                                                 continue;  // stays fully transparent
                                               }
                                               const auto& color =
                                                   palette[std::min<std::size_t>(index, palette.size() - 1)];
                                               dst[0] = color.red;
                                               dst[1] = color.green;
                                               dst[2] = color.blue;
                                               dst[3] = 255;
                                             }
                                           }
                                         }
                                       }
                                       Layer image(document.allocate_layer_id(), source.name, std::move(pixels));
                                       if (cel != nullptr) {
                                         image.set_bounds(Rect{cel->x, cel->y, cel->width, cel->height});
                                         const auto combined = static_cast<float>(source.opacity) *
                                                               static_cast<float>(cel->opacity) / (255.0F * 255.0F);
                                         image.set_opacity(header.layer_opacity_valid ? combined : 1.0F);
                                       } else if (header.layer_opacity_valid) {
                                         image.set_opacity(static_cast<float>(source.opacity) / 255.0F);
                                       }
                                       return image;
                                     }();
    built.set_visible(visible);
    built.set_blend_mode(blend.value_or(BlendMode::Normal));
    if (is_group && header.layer_opacity_valid && header.group_opacity_valid) {
      built.set_opacity(static_cast<float>(source.opacity) / 255.0F);
    }

    const auto level = static_cast<std::size_t>(source.child_level);
    Layer* added = nullptr;
    if (level == 0 || level > parents.size()) {
      added = &document.add_layer(std::move(built));
    } else {
      auto* parent = parents[level - 1];
      parent->add_child(std::move(built));
      added = &parent->children().back();
    }
    parents.resize(level);
    if (added->kind() == LayerKind::Group) {
      parents.push_back(added);
    }
  }

  // Only INDEXED files carry a pixel-defining palette; RGB/grayscale files still ship a
  // palette chunk (often 256 padded/black legacy entries) that is just editing swatches —
  // adopting it would wrongly offer palette mode with garbage colors.
  if (header.depth == 8 && !palette.empty() && palette.size() <= 256) {
    document.indexed_palette() = DocumentIndexedPalette{palette, 8};
  }

  if (notices != nullptr) {
    if (header.frames > 1) {
      notices->push_back("Imported the first frame only (" + std::to_string(header.frames) +
                         " frames in the file); animation import is not supported yet");
    }
    if (!lossy_blend_layers.empty()) {
      std::string names;
      for (const auto& name : lossy_blend_layers) {
        names += (names.empty() ? "" : ", ") + name;
      }
      notices->push_back("Unsupported blend modes were replaced with Normal on: " + names);
    }
    if (skipped_tilemaps > 0) {
      notices->push_back("Skipped " + std::to_string(skipped_tilemaps) + " tilemap layers (not supported)");
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open Aseprite file");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return read(bytes, notices);
}

namespace {

struct FlatLayer {
  const Layer* layer{nullptr};
  std::uint16_t child_level{0};
};

void flatten_layer_tree(const std::vector<Layer>& layers, std::uint16_t level, std::vector<FlatLayer>& out) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Adjustment) {
      continue;  // no Aseprite equivalent; skipped by the writer with a notice upstream
    }
    out.push_back({&layer, level});
    if (layer.kind() == LayerKind::Group) {
      flatten_layer_tree(layer.children(), static_cast<std::uint16_t>(level + 1), out);
    }
  }
}

void write_chunk(LittleEndianWriter& frame, std::uint16_t type, const std::vector<std::uint8_t>& payload) {
  frame.write_u32(static_cast<std::uint32_t>(payload.size() + 6));
  frame.write_u16(type);
  frame.write_bytes(payload);
}

}  // namespace

std::vector<std::uint8_t> DocumentIo::write(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0 || document.width() > 0xffff ||
      document.height() > 0xffff) {
    throw std::runtime_error("Aseprite dimensions must be between 1 and 65535");
  }

  std::vector<FlatLayer> flat;
  flatten_layer_tree(document.layers(), 0, flat);
  if (flat.empty()) {
    throw std::runtime_error("Cannot write an Aseprite file without layers");
  }

  // Indexed when the document is palette-mode and a transparent slot fits; RGBA otherwise.
  std::vector<RgbColor> palette;
  int transparent_index = -1;
  bool indexed = false;
  if (document.palette_editing().has_value() && !document.palette_editing()->palette.colors.empty() &&
      document.palette_editing()->palette.colors.size() <= 255) {
    indexed = true;
    palette = document.palette_editing()->palette.colors;
    transparent_index = static_cast<int>(palette.size());
    palette.push_back(RgbColor{0, 0, 0});
  } else if (document.palette_editing().has_value()) {
    palette = document.palette_editing()->palette.colors;
  } else if (document.indexed_palette().has_value()) {
    palette = document.indexed_palette()->colors;
  }

  PaletteLut lut;
  std::unordered_map<std::uint32_t, int> exact_index;
  if (indexed) {
    lut.build(document.palette_editing()->palette.colors);
    const auto& colors = document.palette_editing()->palette.colors;
    for (int index = 0; index < static_cast<int>(colors.size()); ++index) {
      exact_index.emplace(palette_color_key(colors[static_cast<std::size_t>(index)]), index);
    }
  }
  const std::size_t bytes_per_pixel = indexed ? 1 : 4;

  // Layer chunks + cel chunks for this single frame.
  LittleEndianWriter frame_chunks;
  std::uint32_t chunk_count = 0;

  if (!palette.empty() && palette.size() <= 256) {
    LittleEndianWriter payload;
    payload.write_u32(static_cast<std::uint32_t>(palette.size()));
    payload.write_u32(0);
    payload.write_u32(static_cast<std::uint32_t>(palette.size() - 1));
    for (int i = 0; i < 8; ++i) {
      payload.write_u8(0);
    }
    for (const auto& color : palette) {
      payload.write_u16(0);
      payload.write_u8(color.red);
      payload.write_u8(color.green);
      payload.write_u8(color.blue);
      payload.write_u8(255);
    }
    write_chunk(frame_chunks, kChunkPaletteNew, payload.bytes());
    ++chunk_count;
  }

  for (const auto& entry : flat) {
    LittleEndianWriter payload;
    std::uint16_t flags = entry.layer->visible() ? 1 : 0;
    flags |= 2;  // editable
    payload.write_u16(flags);
    payload.write_u16(entry.layer->kind() == LayerKind::Group ? 1 : 0);
    payload.write_u16(entry.child_level);
    payload.write_u16(0);
    payload.write_u16(0);
    bool lossy = false;
    payload.write_u16(blend_mode_to_aseprite(entry.layer->blend_mode(), &lossy));
    payload.write_u8(static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(entry.layer->opacity() * 255.0F + 0.5F), 0, 255)));
    payload.write_u8(0);
    payload.write_u8(0);
    payload.write_u8(0);
    write_ase_string(payload, entry.layer->name());
    write_chunk(frame_chunks, kChunkLayer, payload.bytes());
    ++chunk_count;
  }

  for (std::size_t index = 0; index < flat.size(); ++index) {
    const auto* layer = flat[index].layer;
    if (layer->kind() != LayerKind::Pixel || layer->pixels().width() <= 0 || layer->pixels().height() <= 0) {
      continue;
    }
    const auto& pixels = layer->pixels();
    const auto channels = pixels.format().channels;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(pixels.width()) *
                                  static_cast<std::size_t>(pixels.height()) * bytes_per_pixel);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto* src = pixels.pixel(x, y);
        const auto red = src[0];
        const auto green = channels >= 2 ? src[1] : src[0];
        const auto blue = channels >= 3 ? src[2] : src[0];
        const auto alpha = channels >= 4 ? src[3] : std::uint8_t{255};
        auto* dst = raw.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels.width()) +
                                  static_cast<std::size_t>(x)) *
                                     bytes_per_pixel;
        if (indexed) {
          if (alpha < kAlphaThreshold) {
            dst[0] = static_cast<std::uint8_t>(transparent_index);
          } else {
            const auto found = exact_index.find(palette_color_key(RgbColor{red, green, blue}));
            dst[0] = found != exact_index.end() ? static_cast<std::uint8_t>(found->second)
                                                : static_cast<std::uint8_t>(lut.index_for(red, green, blue));
          }
        } else {
          dst[0] = red;
          dst[1] = green;
          dst[2] = blue;
          dst[3] = alpha;
        }
      }
    }

    mz_ulong compressed_bound = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressed_bound);
    if (mz_compress(compressed.data(), &compressed_bound, raw.data(), static_cast<mz_ulong>(raw.size())) != MZ_OK) {
      throw std::runtime_error("Aseprite cel compression failed");
    }
    compressed.resize(compressed_bound);

    LittleEndianWriter payload;
    payload.write_u16(static_cast<std::uint16_t>(index));
    payload.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(layer->bounds().x)));
    payload.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(layer->bounds().y)));
    payload.write_u8(255);  // cel opacity (layer opacity carries the blend)
    payload.write_u16(2);   // compressed image cel
    payload.write_u16(0);   // z-index
    for (int i = 0; i < 5; ++i) {
      payload.write_u8(0);
    }
    payload.write_u16(static_cast<std::uint16_t>(pixels.width()));
    payload.write_u16(static_cast<std::uint16_t>(pixels.height()));
    payload.write_bytes(compressed);
    write_chunk(frame_chunks, kChunkCel, payload.bytes());
    ++chunk_count;
  }

  // Frame header + file header.
  LittleEndianWriter file;
  const auto frame_size = static_cast<std::uint32_t>(frame_chunks.bytes().size() + 16);
  const auto file_size = static_cast<std::uint32_t>(128 + frame_size);
  file.write_u32(file_size);
  file.write_u16(kHeaderMagic);
  file.write_u16(1);  // frames
  file.write_u16(static_cast<std::uint16_t>(document.width()));
  file.write_u16(static_cast<std::uint16_t>(document.height()));
  file.write_u16(indexed ? 8 : 32);
  file.write_u32(1U | 2U);  // layer opacity valid + group blend/opacity valid
  file.write_u16(100);
  file.write_u32(0);
  file.write_u32(0);
  file.write_u8(indexed ? static_cast<std::uint8_t>(transparent_index) : 0);
  file.write_u8(0);
  file.write_u8(0);
  file.write_u8(0);
  file.write_u16(static_cast<std::uint16_t>(palette.size() <= 256 ? palette.size() : 0));
  file.write_u8(1);  // pixel width
  file.write_u8(1);  // pixel height
  file.write_u16(0);
  file.write_u16(0);
  file.write_u16(0);
  file.write_u16(0);
  while (file.bytes().size() < 128) {
    file.write_u8(0);
  }
  file.write_u32(frame_size);
  file.write_u16(kFrameMagic);
  file.write_u16(static_cast<std::uint16_t>(std::min<std::uint32_t>(chunk_count, 0xffff)));
  file.write_u16(100);  // duration
  file.write_u8(0);
  file.write_u8(0);
  file.write_u32(chunk_count);
  file.write_bytes(frame_chunks.bytes());
  return std::move(file.bytes());
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path) {
  const auto bytes = write(document);
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open Aseprite file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write Aseprite file");
  }
}

}  // namespace patchy::aseprite
