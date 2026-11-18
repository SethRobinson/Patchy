// Aseprite (.aseprite/.ase) reader/writer, following the published file-format spec
// (https://github.com/aseprite/aseprite/blob/main/docs/ase-file-specs.md).
// Unknown chunks are skipped by length. Zlib cels use the vendored miniz.

#include "formats/aseprite_document_io.hpp"
#include "core/artboard.hpp"
#include "core/layer_metadata.hpp"

#include "core/palette.hpp"
#include "formats/animation_timing.hpp"
#include "formats/binary_le.hpp"
#include "formats/format_file_io.hpp"

#include "formats/miniz/miniz.h"
#include "support/decode_budget.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
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
    case BlendMode::VividLight:
    case BlendMode::LinearLight:
    case BlendMode::HardMix:
    case BlendMode::DarkerColor:
    case BlendMode::LighterColor:
    case BlendMode::Dissolve:
    case BlendMode::Reflect:
    case BlendMode::Glow:
    case BlendMode::Negation:
    case BlendMode::Xor:
    case BlendMode::GrainExtract:
    case BlendMode::GrainMerge:
    case BlendMode::HsvHue:
    case BlendMode::HsvSaturation:
    case BlendMode::HslColor:
    case BlendMode::HsvValue:
    case BlendMode::LchHue:
    case BlendMode::LchChroma:
    case BlendMode::LchColor:
    case BlendMode::LchLightness:
    case BlendMode::Luminance:
    case BlendMode::LumaDarkenOnly:
    case BlendMode::LumaLightenOnly:
    case BlendMode::LinearMultiply:
    case BlendMode::LinearAddition:
    case BlendMode::LinearSubtract:
    case BlendMode::LinearDivide:
    case BlendMode::SoftLightSvg:
    case BlendMode::AdditiveSubtractive:
    case BlendMode::ArcTangent:
    case BlendMode::GeometricMean:
    case BlendMode::Allanon:
    case BlendMode::Parallel:
    case BlendMode::HardOverlay:
    case BlendMode::Interpolation:
    case BlendMode::EasyDodge:
      if (lossy != nullptr) {
        *lossy = true;
      }
      return 0;
  }
  return 0;
}

struct AseHeader {
  std::uint16_t frames{0};
  std::uint16_t speed{0};
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
  std::vector<std::size_t> children;
};

using AsePalette = std::vector<std::array<std::uint8_t, 4>>;

struct AseImage {
  int width{0};
  int height{0};
  bool compressed{false};
  std::span<const std::uint8_t> data;
  // Linked cels share the copy-on-write RGBA pixels. An indexed image can
  // acquire new colors when the same indices are displayed in a later frame.
  PixelBuffer pixels;
  const AsePalette* rendered_palette{nullptr};
};

struct AseCel {
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t opacity{255};
  int z_index{0};
  std::optional<std::size_t> image;
  std::uint16_t linked_frame{0};
};

struct AseFrame {
  std::uint16_t duration{0};
  std::unordered_map<std::uint16_t, AseCel> cels;
  std::shared_ptr<const AsePalette> palette;
};

constexpr std::uint64_t kMaxCanvasPixels = 268'435'456ULL;  // same canvas limit as animated JPEG XL
constexpr std::uint64_t kMaxTreeBytes = 512ULL << 20U;
constexpr std::uint64_t kMaxAnimationRgbaBytes = 4ULL << 30U;

[[noreturn]] void damaged_frame() {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite frame header is damaged"));
}

[[noreturn]] void image_too_large() {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "There is not enough memory available to open this file."));
}

[[nodiscard]] std::string read_ase_string(LittleEndianReader& reader) {
  const auto length = reader.read_u16();
  if (length > reader.remaining()) damaged_frame();
  std::string text;
  text.reserve(length);
  for (std::uint16_t i = 0; i < length; ++i) {
    text.push_back(static_cast<char>(reader.read_u8()));
  }
  return text;
}

[[nodiscard]] std::vector<std::uint8_t> inflate_cel(std::span<const std::uint8_t> compressed,
                                                    std::size_t expected_size) {
  if (compressed.size() > std::numeric_limits<unsigned int>::max()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel data failed to decompress"));
  }
  mz_stream stream{};
  stream.next_in = compressed.data();
  stream.avail_in = static_cast<unsigned int>(compressed.size());
  if (mz_inflateInit(&stream) != MZ_OK) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel data failed to decompress"));
  }
  struct InflateCleanup {
    mz_stream& stream;
    ~InflateCleanup() { mz_inflateEnd(&stream); }
  } cleanup{stream};
  std::vector<std::uint8_t> out;
  std::array<std::uint8_t, 65536> chunk{};
  for (;;) {
    stream.next_out = chunk.data();
    stream.avail_out = static_cast<unsigned int>(chunk.size());
    const auto input_before = stream.avail_in;
    const auto status = mz_inflate(&stream, MZ_NO_FLUSH);
    const auto produced = chunk.size() - stream.avail_out;
    if (produced > expected_size - out.size()) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel data failed to decompress"));
    }
    out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
    if (status == MZ_STREAM_END) {
      break;
    }
    if (status != MZ_OK || (produced == 0 && stream.avail_in == input_before)) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel data failed to decompress"));
    }
  }
  if (out.size() != expected_size) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel data failed to decompress"));
  }
  return out;
}

void write_ase_string(LittleEndianWriter& writer, const std::string& text) {
  writer.write_u16(static_cast<std::uint16_t>(text.size()));
  for (const auto character : text) {
    writer.write_u8(static_cast<std::uint8_t>(character));
  }
}

void update_palette(AsePalette& palette, std::uint16_t type, std::span<const std::uint8_t> data) {
  auto reader = ase_reader(data);
  constexpr std::array<std::uint8_t, 4> black{0, 0, 0, 255};
  if (type == kChunkPaletteNew) {
    const auto size = reader.read_u32();
    const auto first = reader.read_u32();
    const auto last = reader.read_u32();
    reader.skip(8);
    if (first > last || last >= size || static_cast<std::uint64_t>(last) - first + 1 > reader.remaining() / 6) {
      damaged_frame();
    }
    palette.resize(std::min<std::uint32_t>(size, 256), black);
    for (auto index = first; index <= last; ++index) {
      const auto flags = reader.read_u16();
      const std::array color{reader.read_u8(), reader.read_u8(), reader.read_u8(), reader.read_u8()};
      if (index < palette.size()) palette[index] = color;
      if ((flags & 1U) != 0) reader.skip(reader.read_u16());
    }
    return;
  }

  const auto packets = reader.read_u16();
  std::size_t index = 0;
  for (std::uint16_t packet = 0; packet < packets; ++packet) {
    index += reader.read_u8();
    const auto stored_count = reader.read_u8();
    const auto count = stored_count == 0 ? 256U : static_cast<unsigned>(stored_count);
    if (index + count > 256) damaged_frame();
    if (palette.size() < index + count) palette.resize(index + count, black);
    for (unsigned i = 0; i < count; ++i) {
      auto& color = palette[index++];
      for (int channel = 0; channel < 3; ++channel) {
        const auto value = reader.read_u8();
        color[channel] = type == kChunkPaletteOld2
                             ? static_cast<std::uint8_t>((value << 2U) | (value >> 4U)) : value;
      }
      color[3] = 255;
    }
  }
}

// Keep the cel's independent position, opacity and stacking order even for
// links. Only its image data comes from the referenced cel.
void read_cel(std::span<const std::uint8_t> data, AseFrame& frame, std::vector<AseImage>& images) {
  auto reader = LittleEndianReader(data, PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel chunk is truncated"));
  const auto layer_index = reader.read_u16();
  AseCel cel;
  cel.x = static_cast<std::int16_t>(reader.read_u16());
  cel.y = static_cast<std::int16_t>(reader.read_u16());
  cel.opacity = reader.read_u8();
  const auto type = reader.read_u16();
  cel.z_index = static_cast<std::int16_t>(reader.read_u16());
  reader.skip(5);
  if (type == 1) {
    cel.linked_frame = reader.read_u16();
  } else if (type == 0 || type == 2) {
    AseImage image;
    image.width = reader.read_u16();
    image.height = reader.read_u16();
    if (image.width == 0 || image.height == 0) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite image has invalid dimensions"));
    }
    image.compressed = type == 2;
    image.data = data.subspan(reader.position());
    cel.image = images.size();
    images.push_back(std::move(image));
  } else {
    return;  // e.g. tilemap, reported once for its layer
  }
  if (!frame.cels.emplace(layer_index, std::move(cel)).second) damaged_frame();
}

AseFrame read_frame(std::span<const std::uint8_t> data, const AseHeader& header, std::vector<AseLayer>& layers,
                    std::vector<AseImage>& images, const std::shared_ptr<const AsePalette>& previous_palette,
                    bool first_frame) {
  auto reader = ase_reader(data);
  reader.skip(4);
  if (reader.read_u16() != kFrameMagic) damaged_frame();
  const auto old_chunks = reader.read_u16();
  AseFrame frame;
  const auto duration = reader.read_u16();
  frame.duration = duration == 0 ? header.speed : duration;
  reader.skip(2);
  const auto new_chunks = reader.read_u32();
  const auto chunks = new_chunks == 0 ? old_chunks : new_chunks;
  std::vector<std::pair<std::uint16_t, std::span<const std::uint8_t>>> palette_chunks;
  bool has_new_palette = false;
  for (std::uint32_t index = 0; index < chunks; ++index) {
    const auto size = reader.read_u32();
    const auto type = reader.read_u16();
    if (size < 6 || size - 6 > reader.remaining()) damaged_frame();
    const auto payload = data.subspan(reader.position(), size - 6);
    reader.skip(size - 6);
    if (type == kChunkLayer) {
      if (!first_frame || layers.size() >= 65536) damaged_frame();
      auto fields = ase_reader(payload);
      AseLayer layer;
      layer.flags = fields.read_u16();
      layer.type = fields.read_u16();
      layer.child_level = fields.read_u16();
      fields.skip(4);
      layer.blend_mode = fields.read_u16();
      layer.opacity = fields.read_u8();
      fields.skip(3);
      layer.name = read_ase_string(fields);
      layers.push_back(std::move(layer));
    } else if (type == kChunkCel) {
      read_cel(payload, frame, images);
    } else if (header.depth == 8 && (type == kChunkPaletteNew || type == kChunkPaletteOld || type == kChunkPaletteOld2)) {
      palette_chunks.emplace_back(type, payload);
      has_new_palette |= type == kChunkPaletteNew;
    }
  }
  frame.palette = previous_palette;
  if (!palette_chunks.empty()) {
    auto palette = previous_palette ? *previous_palette : AsePalette{};
    for (const auto& [type, payload] : palette_chunks) {
      if (!has_new_palette || type == kChunkPaletteNew) update_palette(palette, type, payload);
    }
    if (!previous_palette || *previous_palette != palette) {
      frame.palette = std::make_shared<AsePalette>(std::move(palette));
    }
  }
  if (header.depth == 8 && (!frame.palette || frame.palette->empty())) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Indexed Aseprite file is missing its palette"));
  }
  return frame;
}

// Resolve in either direction, without recursion. Remember the result on every
// visited cel so even long chains take linear time across the whole animation.
std::size_t resolve_image(std::vector<AseFrame>& frames, std::uint16_t layer, AseCel& cel) {
  auto* target = &cel;
  std::vector<AseCel*> path;
  while (!target->image) {
    if (target->linked_frame >= frames.size() || path.size() >= frames.size()) damaged_frame();
    path.push_back(target);
    auto& cels = frames[target->linked_frame].cels;
    auto found = cels.find(layer);
    if (found == cels.end()) damaged_frame();
    target = &found->second;
  }
  for (auto* link : path) link->image = target->image;
  return *target->image;
}

const PixelBuffer& decode_image(AseImage& image, const AseHeader& header, const AsePalette* palette,
                                bool background, DecodeAllowance& rgba_budget) {
  if (!image.pixels.empty() && image.rendered_palette == palette) return image.pixels;
  const auto source_bytes = checked_decode_bytes(image.width, image.height, header.depth / 8U, image.data.size());
  const auto rgba_bytes = checked_buffer_bytes(static_cast<std::uint64_t>(image.width) * image.height, 4U);
  if (!source_bytes || !rgba_bytes || !rgba_budget.charge(*rgba_bytes)) image_too_large();
  std::span<const std::uint8_t> raw = image.data;
  std::vector<std::uint8_t> inflated;
  if (image.compressed) {
    inflated = inflate_cel(raw, *source_bytes);
    raw = inflated;
  } else if (raw.size() < *source_bytes) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel chunk is truncated"));
  }
  PixelBuffer pixels(image.width, image.height, PixelFormat::rgba8());
  auto out = pixels.data();
  if (header.depth == 32) {
    std::copy_n(raw.begin(), *source_bytes, out.begin());
  } else if (header.depth == 16) {
    for (std::size_t i = 0; i < *source_bytes; i += 2) {
      const auto gray = raw[i];
      out[i * 2] = out[i * 2 + 1] = out[i * 2 + 2] = gray;
      out[i * 2 + 3] = raw[i + 1];
    }
  } else {
    for (std::size_t i = 0; i < *source_bytes; ++i) {
      const auto index = raw[i];
      if (index == header.transparent_index && !background) continue;
      const auto& color = (*palette)[std::min<std::size_t>(index, palette->size() - 1)];
      std::copy(color.begin(), color.end(), out.data() + i * 4);
    }
  }
  image.pixels = std::move(pixels);
  image.rendered_palette = palette;
  return image.pixels;
}

// Aseprite orders each frame by layer index + cel z-index, then z-index for
// ties. Reorder siblings within their source group's blend context.
std::vector<std::size_t> reordered_siblings(const std::vector<std::size_t>& siblings, const AseFrame& frame) {
  const auto z_index = [&](std::size_t index) {
    const auto it = frame.cels.find(static_cast<std::uint16_t>(index));
    return it == frame.cels.end() ? 0 : it->second.z_index;
  };
  if (std::none_of(siblings.begin(), siblings.end(), [&](auto index) { return z_index(index) != 0; })) return {};
  const auto order = [&](std::size_t index) {
    const auto z = z_index(index);
    return std::pair{static_cast<int>(index) + z, z};
  };
  auto sorted = siblings;
  std::sort(sorted.begin(), sorted.end(), [&](auto a, auto b) { return order(a) < order(b); });
  return sorted;
}

struct FrameLayerBuilder {
  Document& document;
  const AseHeader& header;
  const std::vector<AseLayer>& layers;
  std::vector<AseFrame>& frames;
  std::vector<AseImage>& images;
  DecodeAllowance& rgba_budget;
  const PixelBuffer empty_cel{1, 1, PixelFormat::rgba8()};

  Layer pixel_layer(const AseLayer& source, std::uint16_t layer_index, AseFrame& frame) {
    const auto opacity = header.layer_opacity_valid ? static_cast<float>(source.opacity) / 255.0F : 1.0F;
    auto it = frame.cels.find(layer_index);
    if (it == frame.cels.end()) {
      Layer layer(document.allocate_layer_id(), source.name, empty_cel);
      layer.set_opacity(opacity);
      return layer;
    }
    auto& cel = it->second;
    auto& image = images[resolve_image(frames, layer_index, cel)];
    const auto& pixels = decode_image(image, header, frame.palette.get(), (source.flags & 8U) != 0, rgba_budget);
    Layer layer(document.allocate_layer_id(), source.name, pixels);
    layer.set_bounds(Rect{cel.x, cel.y, image.width, image.height});
    layer.set_opacity(opacity * cel.opacity / 255.0F);
    return layer;
  }

  void append(const std::vector<std::size_t>& siblings, AseFrame& frame, Layer* parent) {
    if (parent) parent->children().reserve(siblings.size());
    else document.layers().reserve(siblings.size());
    const auto sorted = reordered_siblings(siblings, frame);
    for (auto index : sorted.empty() ? siblings : sorted) {
      const auto& source = layers[index];
      const bool group = source.type == 1;
      Layer layer = group ? Layer(document.allocate_layer_id(), source.name, LayerKind::Group)
                          : pixel_layer(source, static_cast<std::uint16_t>(index), frame);
      layer.set_visible((source.flags & 1U) != 0);
      if (!group || header.group_opacity_valid) {
        layer.set_blend_mode(blend_mode_from_aseprite(source.blend_mode).value_or(BlendMode::Normal));
      } else if (frames.size() > 1) {
        // Without the group-blending flag Aseprite composites the sprite's
        // layers together. Keep the single-frame editor's original mapping.
        layer.set_blend_mode(BlendMode::PassThrough);
      }
      if (group) {
        if ((source.flags & 32U) != 0) set_layer_group_expanded(layer, false);
        if (header.group_opacity_valid && header.layer_opacity_valid) {
          layer.set_opacity(static_cast<float>(source.opacity) / 255.0F);
        }
        append(source.children, frame, &layer);
      }
      if (parent) parent->add_child(std::move(layer));
      else document.add_layer(std::move(layer));
    }
  }
};

}  // namespace

bool stores_blend_mode(BlendMode mode) noexcept {
  if (mode == BlendMode::PassThrough) {
    return false;
  }
  bool lossy = false;
  static_cast<void>(blend_mode_to_aseprite(mode, &lossy));
  return !lossy;
}

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 6 && (static_cast<std::uint16_t>(bytes[4] | (bytes[5] << 8U)) == kHeaderMagic);
}

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return sniff(bytes);
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices,
                          bool* partial_import, const FrameName& frame_name) {
  if (bytes.size() >= 4 && bytes[0] == 'A' && bytes[1] == 'S' && bytes[2] == 'E' && bytes[3] == 'F') {
    throw std::runtime_error(
        PATCHY_TRANSLATE_NOOP("QObject", "This is an Adobe swatch palette (.ase), not an Aseprite image. Load it from the Palette panel's "
        "Load Palette File instead."));
  }
  if (!sniff(bytes)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "File is not an Aseprite image"));
  }
  auto reader = ase_reader(bytes);
  AseHeader header;
  reader.skip(4);  // file size
  reader.skip(2);  // magic (verified by sniff)
  header.frames = reader.read_u16();
  if (partial_import != nullptr) *partial_import = header.frames > 1;
  header.width = reader.read_u16();
  header.height = reader.read_u16();
  header.depth = reader.read_u16();
  const auto flags = reader.read_u32();
  header.layer_opacity_valid = (flags & 1U) != 0;
  header.group_opacity_valid = (flags & 2U) != 0;
  header.speed = reader.read_u16();
  reader.skip(8);  // reserved
  header.transparent_index = reader.read_u8();
  reader.skip(3);
  reader.skip(2);  // number of colors (the palette chunk is authoritative)
  reader.seek(128);

  if (header.width <= 0 || header.height <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite image has invalid dimensions"));
  }
  if (!checked_pixel_count(header.width, header.height, kMaxCanvasPixels)) image_too_large();
  if (header.depth != 8 && header.depth != 16 && header.depth != 32) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite color depth is not supported"));
  }
  if (header.frames == 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite file contains no frames"));
  }

  std::vector<AseLayer> layers;
  std::vector<AseFrame> frames;
  std::vector<AseImage> images;
  if (header.frames > reader.remaining() / 16) damaged_frame();
  frames.reserve(header.frames);
  for (std::uint32_t index = 0; index < header.frames; ++index) {
    const auto start = reader.position();
    const auto size = reader.read_u32();
    if (size < 16 || size - 4 > reader.remaining()) damaged_frame();
    const auto previous = frames.empty() ? nullptr : frames.back().palette;
    frames.push_back(read_frame(bytes.subspan(start, size), header, layers, images, previous, index == 0));
    reader.skip(size - 4);
  }
  if (layers.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite file contains no layers"));
  }

  std::vector<std::size_t> roots;
  std::vector<std::size_t> parents;
  std::vector<std::string> lossy_blend_layers;
  int skipped_tilemaps = 0;
  std::uint64_t tree_bytes = sizeof(Layer);
  for (std::size_t index = 0; index < layers.size(); ++index) {
    auto& source = layers[index];
    tree_bytes += sizeof(Layer) + source.name.size();
    if (source.type == 2) {
      ++skipped_tilemaps;
      continue;
    }
    // A damaged layer that skips a level lands at the root and closes the
    // previous groups. Bound nesting before building a recursive Layer tree.
    const auto level = source.child_level <= parents.size() ? source.child_level : 0;
    if (level > 64) image_too_large();
    parents.resize(level);
    auto& siblings = parents.empty() ? roots : layers[parents.back()].children;
    siblings.push_back(index);
    if (source.type == 1) parents.push_back(index);
    if ((source.type != 1 || header.group_opacity_valid) && !blend_mode_from_aseprite(source.blend_mode)) {
      lossy_blend_layers.push_back(source.name);
    }
  }
  // An almost empty file can describe a separate copy of a large tree for
  // every frame. Charge these records (including repeated names) before
  // materializing any. Pixel data is charged separately when first decoded.
  if (tree_bytes * frames.size() > kMaxTreeBytes) image_too_large();
  DecodeAllowance rgba_budget(kMaxAnimationRgbaBytes);
  Document document(header.width, header.height, PixelFormat::rgba8());
  if (frames.size() > 1) document.layers().reserve(frames.size());
  FrameLayerBuilder builder{document, header, layers, frames, images, rgba_budget};
  for (std::size_t i = frames.size(); i-- > 0;) {
    auto& frame = frames[i];
    Layer* frame_group = nullptr;
    if (frames.size() > 1) {
      const auto number = static_cast<int>(i + 1);
      auto name = frame_name ? frame_name(number, frame.duration)
                             : "Frame " + std::to_string(number) + " " + animation::format_delay_seconds_token(frame.duration);
      Layer group(document.allocate_layer_id(), std::move(name), LayerKind::Group);
      set_layer_group_expanded(group, false);
      frame_group = &document.add_layer(std::move(group));
    }
    builder.append(roots, frame, frame_group);
  }

  // Offer one editing palette only if all frames use it. Color cycling and
  // transparency have already been applied to each frame's actual RGBA pixels.
  if (header.depth == 8 && std::all_of(frames.begin(), frames.end(), [&](const auto& frame) {
        return frame.palette == frames.front().palette;
      })) {
    std::vector<RgbColor> colors;
    for (const auto& entry : *frames.front().palette) {
      colors.push_back(RgbColor{entry[0], entry[1], entry[2]});
    }
    document.indexed_palette() = DocumentIndexedPalette{std::move(colors), 8};
  }

  if (notices != nullptr) {
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
  return read(formats::read_file_bytes(path, "Aseprite"), notices);
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
  if (document_has_artboards(document)) {
    // Aseprite has no artboards: each becomes a plain group with its background as a layer
    // (docs/artboards.md). Aseprite groups cannot clip, so content past an artboard's edge
    // shows in the file.
    auto plain = document;
    convert_artboards_to_plain_groups(plain);
    return write(plain);
  }
  if (document.width() <= 0 || document.height() <= 0 || document.width() > 0xffff ||
      document.height() > 0xffff) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite dimensions must be between 1 and 65535"));
  }

  std::vector<FlatLayer> flat;
  flatten_layer_tree(document.layers(), 0, flat);
  if (flat.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an Aseprite file without layers"));
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
    if (entry.layer->kind() == LayerKind::Group && !layer_group_expanded(*entry.layer)) {
      flags |= 32;  // group displayed collapsed
    }
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
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Aseprite cel compression failed"));
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
  formats::write_file_bytes(path, write(document), "Aseprite");
}

}  // namespace patchy::aseprite
