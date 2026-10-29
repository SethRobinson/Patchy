#include "formats/xcf_document_io.hpp"

#include "core/document_depth.hpp"
#include "core/environment.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "core/worker_budget.hpp"
#include "formats/format_file_io.hpp"
#include "formats/metadata/gimp_image_metadata.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/metadata/xmp.hpp"
#include "formats/miniz/miniz.h"
#include "formats/xcf_internal.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

// Wire layout (docs/xcf.md has the full record and the mapping tables). Everything is
// big-endian; "pointer" is a u32 file offset up to version 10 and a u64 from version 11:
//
//   "gimp xcf " + version tag ("file", "v001".."v026") + NUL, u32 width, u32 height,
//   u32 base type (0 RGB, 1 gray, 2 indexed), u32 precision (version 4 and later),
//   image properties, layer pointers (top first) + 0, channel pointers + 0, and from
//   version 18 path pointers + 0.
//   A property is u32 type, u32 length, payload; type 0 ends the list. PROP_COLORMAP and
//   PROP_USER_UNIT store wrong lengths in many files, so their payloads are measured.
//   Layer: u32 w, u32 h, u32 type, string name, properties, hierarchy pointer, mask
//   pointer (a channel), and from version 20 effect pointers + 0. Every layer of the tree
//   is in the one top-level list, depth first; PROP_ITEM_PATH gives its position.
//   Channel: u32 w, u32 h, string name, properties, hierarchy pointer.
//   Hierarchy: u32 w, u32 h, u32 bytes per pixel, level pointer, dummy levels + 0.
//   Level: u32 w, u32 h, one pointer per 64x64 tile (row-major) + 0. Tiles are raw,
//   RLE (each byte of the pixel as its own run-length stream) or zlib.

namespace patchy::xcf {

namespace detail {

namespace {
std::atomic<int> g_trace_override{-1};
}  // namespace

void set_trace_override(int enabled) noexcept {
  g_trace_override.store(enabled);
}

[[nodiscard]] bool trace_enabled() {
  static const bool enabled = environment_variable_is_set("PATCHY_XCF_TRACE");
  const int forced = g_trace_override.load();
  return forced >= 0 ? forced != 0 : enabled;
}

void trace(const std::string& message) {
  if (trace_enabled()) {
    std::fprintf(stderr, "[xcf] %s\n", message.c_str());
  }
}

}  // namespace detail

namespace {

using detail::ImportError;
using detail::SizeLimitError;
using detail::trace;

constexpr std::int64_t kMaxSide = 300000;              // the PSB cap every importer shares
constexpr std::uint64_t kMaxLayerPixels = 1ULL << 30U;  // per drawable
constexpr std::uint64_t kMaxDecodedBytes = 24ULL << 30U;  // all drawables of a file together
constexpr std::size_t kMaxItems = 100000;               // layers, channels, paths, effects
constexpr std::size_t kMaxProperties = 100000;          // per property list
constexpr std::size_t kMaxGroupDepth = 64;
constexpr std::uint32_t kTileSize = 64;

// Property types (the spec's PROP_* numbers).
enum : std::uint32_t {
  kPropEnd = 0,
  kPropColormap = 1,
  kPropActiveLayer = 2,
  kPropActiveChannel = 3,
  kPropSelection = 4,
  kPropFloatingSelection = 5,
  kPropOpacity = 6,
  kPropMode = 7,
  kPropVisible = 8,
  kPropLinked = 9,
  kPropLockAlpha = 10,
  kPropApplyMask = 11,
  kPropEditMask = 12,
  kPropShowMask = 13,
  kPropShowMasked = 14,
  kPropOffsets = 15,
  kPropColor = 16,
  kPropCompression = 17,
  kPropGuides = 18,
  kPropResolution = 19,
  kPropTattoo = 20,
  kPropParasites = 21,
  kPropUnit = 22,
  kPropPaths = 23,
  kPropUserUnit = 24,
  kPropVectors = 25,
  kPropTextLayerFlags = 26,
  kPropLockContent = 28,
  kPropGroupItem = 29,
  kPropItemPath = 30,
  kPropGroupItemFlags = 31,
  kPropLockPosition = 32,
  kPropFloatOpacity = 33,
  kPropColorTag = 34,
  kPropCompositeMode = 35,
  kPropCompositeSpace = 36,
  kPropBlendSpace = 37,
  kPropFloatColor = 38,
  kPropFilterRegion = 44,
  kPropFilterArgument = 45,
  kPropVectorLayer = 47,
  kPropLinkLayer = 48,
};

// --- bounds-checked big-endian reading --------------------------------------------------

class Reader {
public:
  Reader(std::span<const std::uint8_t> data, int version) : data_(data), version_(version) {}

  [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return data_; }
  [[nodiscard]] int version() const noexcept { return version_; }
  [[nodiscard]] std::uint64_t size() const noexcept { return data_.size(); }

  void need(std::uint64_t at, std::uint64_t count) const {
    if (at > data_.size() || count > data_.size() - at) {
      throw ImportError("the file is truncated at offset " + std::to_string(at));
    }
  }
  [[nodiscard]] std::uint8_t u8(std::uint64_t at) const {
    need(at, 1);
    return data_[static_cast<std::size_t>(at)];
  }
  [[nodiscard]] std::uint32_t u32(std::uint64_t at) const {
    need(at, 4);
    const auto* p = data_.data() + at;
    return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
  }
  [[nodiscard]] std::uint64_t u64(std::uint64_t at) const {
    return (static_cast<std::uint64_t>(u32(at)) << 32U) | u32(at + 4);
  }
  [[nodiscard]] std::int32_t i32(std::uint64_t at) const { return static_cast<std::int32_t>(u32(at)); }
  [[nodiscard]] float f32(std::uint64_t at) const {
    const auto bits = u32(at);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
  [[nodiscard]] std::uint32_t pointer_size() const noexcept { return version_ >= 11 ? 8U : 4U; }
  [[nodiscard]] std::uint64_t pointer(std::uint64_t at) const { return version_ >= 11 ? u64(at) : u32(at); }

private:
  std::span<const std::uint8_t> data_;
  int version_{0};
};

// A cursor over a Reader or over one property payload.
class Cursor {
public:
  Cursor(const Reader& reader, std::uint64_t at, std::uint64_t end)
      : reader_(&reader), at_(at), end_(std::min<std::uint64_t>(end, reader.size())) {}

  [[nodiscard]] std::uint64_t at() const noexcept { return at_; }
  [[nodiscard]] std::uint64_t remaining() const noexcept { return at_ <= end_ ? end_ - at_ : 0; }
  [[nodiscard]] bool done() const noexcept { return at_ >= end_; }
  void need(std::uint64_t count) const {
    if (count > remaining()) {
      throw ImportError("a record runs past its end at offset " + std::to_string(at_));
    }
  }
  [[nodiscard]] std::uint8_t u8() {
    need(1);
    return reader_->u8(at_++);
  }
  [[nodiscard]] std::uint32_t u32() {
    need(4);
    const auto value = reader_->u32(at_);
    at_ += 4;
    return value;
  }
  [[nodiscard]] std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  [[nodiscard]] float f32() {
    need(4);
    const auto value = reader_->f32(at_);
    at_ += 4;
    return value;
  }
  [[nodiscard]] double f64() {
    need(8);
    const auto bits = reader_->u64(at_);
    at_ += 8;
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
  [[nodiscard]] std::uint64_t pointer() {
    need(reader_->pointer_size());
    const auto value = reader_->pointer(at_);
    at_ += reader_->pointer_size();
    return value;
  }
  [[nodiscard]] std::string string() {
    const auto length = u32();
    if (length == 0) {
      return {};
    }
    need(length);
    const auto bytes = reader_->data().subspan(static_cast<std::size_t>(at_), length);
    at_ += length;
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    while (!text.empty() && text.back() == '\0') {
      text.pop_back();
    }
    return text;
  }
  [[nodiscard]] std::span<const std::uint8_t> bytes(std::uint64_t count) {
    need(count);
    const auto out = reader_->data().subspan(static_cast<std::size_t>(at_), static_cast<std::size_t>(count));
    at_ += count;
    return out;
  }
  void skip(std::uint64_t count) {
    need(count);
    at_ += count;
  }

private:
  const Reader* reader_;
  std::uint64_t at_;
  std::uint64_t end_;
};

// --- precision ----------------------------------------------------------------------------

enum class Component : std::uint8_t { U8, U16, U32, Half, Float, Double };
enum class Transfer : std::uint8_t { Linear, NonLinear, Perceptual };

struct Precision {
  Component component{Component::U8};
  Transfer transfer{Transfer::NonLinear};
};

[[nodiscard]] std::uint32_t component_bytes(Component component) noexcept {
  switch (component) {
    case Component::U8:
      return 1;
    case Component::U16:
    case Component::Half:
      return 2;
    case Component::U32:
    case Component::Float:
      return 4;
    case Component::Double:
      return 8;
  }
  return 1;
}

[[nodiscard]] std::optional<Precision> precision_from_header(int version, std::uint32_t value) {
  if (version == 4) {
    switch (value) {
      case 0:
        return Precision{Component::U8, Transfer::NonLinear};
      case 1:
        return Precision{Component::U16, Transfer::NonLinear};
      case 2:
        return Precision{Component::U32, Transfer::Linear};
      case 3:
        return Precision{Component::Half, Transfer::Linear};
      case 4:
        return Precision{Component::Float, Transfer::Linear};
      default:
        return std::nullopt;
    }
  }
  const auto transfer_code = value % 100U;
  Transfer transfer = Transfer::NonLinear;
  if (transfer_code == 0) {
    transfer = Transfer::Linear;
  } else if (transfer_code == 50) {
    transfer = Transfer::NonLinear;
  } else if (transfer_code == 75) {
    transfer = Transfer::Perceptual;
  } else {
    return std::nullopt;
  }
  auto kind = value / 100U;
  if (version == 5 || version == 6) {
    // Development versions numbered the float precisions one step lower.
    if (kind == 4) {
      kind = 5;
    } else if (kind == 5) {
      kind = 6;
    }
  }
  switch (kind) {
    case 1:
      return Precision{Component::U8, transfer};
    case 2:
      return Precision{Component::U16, transfer};
    case 3:
      return Precision{Component::U32, transfer};
    case 5:
      return Precision{Component::Half, transfer};
    case 6:
      return Precision{Component::Float, transfer};
    case 7:
      return Precision{Component::Double, transfer};
    default:
      return std::nullopt;
  }
}

// --- properties and parasites -----------------------------------------------------------

struct Property {
  std::uint32_t type{0};
  std::uint64_t offset{0};  // payload start
  std::uint64_t length{0};
};

struct Parasite {
  std::string name;
  std::uint32_t flags{0};
  std::span<const std::uint8_t> data;
};

[[nodiscard]] std::vector<Property> read_properties(const Reader& reader, Cursor& cursor) {
  std::vector<Property> properties;
  while (true) {
    if (properties.size() > kMaxProperties) {
      throw ImportError("a property list does not end");
    }
    const auto type = cursor.u32();
    auto length = static_cast<std::uint64_t>(cursor.u32());
    if (type == kPropEnd) {
      return properties;
    }
    const auto start = cursor.at();
    if (type == kPropColormap) {
      // Old releases wrote n + 4 here; the payload is always 4 + 3n bytes.
      const auto count = reader.u32(start);
      length = 4ULL + 3ULL * static_cast<std::uint64_t>(count);
    } else if (type == kPropUserUnit) {
      // Every release up to 2026 wrote a wrong length: measure the fields instead.
      Cursor measure(reader, start, reader.size());
      (void)measure.f32();
      (void)measure.u32();
      const int strings = reader.version() >= 21 ? 3 : 5;
      for (int i = 0; i < strings; ++i) {
        (void)measure.string();
      }
      length = measure.at() - start;
    }
    cursor.skip(length);
    properties.push_back(Property{type, start, length});
  }
}

[[nodiscard]] std::vector<Parasite> read_parasites(const Reader& reader, const Property& property) {
  std::vector<Parasite> parasites;
  Cursor cursor(reader, property.offset, property.offset + property.length);
  while (!cursor.done()) {
    Parasite parasite;
    parasite.name = cursor.string();
    parasite.flags = cursor.u32();
    const auto size = cursor.u32();
    parasite.data = cursor.bytes(size);
    // A later parasite of the same name wins.
    std::erase_if(parasites, [&](const Parasite& other) { return other.name == parasite.name; });
    parasites.push_back(std::move(parasite));
    if (parasites.size() > kMaxItems) {
      throw ImportError("too many parasites");
    }
  }
  return parasites;
}

[[nodiscard]] const Parasite* find_parasite(const std::vector<Parasite>& parasites, std::string_view name) {
  for (const auto& parasite : parasites) {
    if (parasite.name == name) {
      return &parasite;
    }
  }
  return nullptr;
}

// File Info's Application for a file that names no creator (no xmp:CreatorTool, no EXIF
// Software): the first xmpMM:History entry's stEvt:softwareAgent ("GIMP 3.2.6 (Linux)"), else
// "GIMP". The XCF header gives only the file version. An unparseable packet is left alone.
void fill_gimp_creator_tool(DocumentFileInfo& info) {
  if (!metadata::read_fields(info).creator_tool.empty()) {
    return;
  }
  metadata::XmpPacket packet;
  if (info.xmp) {
    auto parsed = metadata::parse_xmp(*info.xmp);
    if (!parsed) {
      return;
    }
    packet = std::move(*parsed);
  }
  std::string agent;
  if (const auto* history = packet.find(metadata::xmp_ns::kXmpMM, "History"); history != nullptr) {
    for (const auto& entry : history->value.items) {
      for (const auto& field : entry.fields) {
        if (field.ns == metadata::xmp_ns::kStEvt && field.name == "softwareAgent") {
          agent = field.value.text;
        }
      }
      if (!agent.empty()) {
        break;
      }
    }
  }
  packet.set_simple(metadata::xmp_ns::kXmp, "CreatorTool", agent.empty() ? std::string("GIMP") : agent);
  const auto bytes = metadata::serialize_xmp(packet);
  info.xmp = make_metadata_bytes(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}

[[nodiscard]] std::uint32_t payload_u32(const Reader& reader, const Property& property, std::uint32_t fallback) {
  return property.length >= 4 ? reader.u32(property.offset) : fallback;
}

// --- records ------------------------------------------------------------------------------

struct ChannelRecord {
  std::uint64_t offset{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::string name;
  float opacity{1.0F};
  bool visible{false};
  bool selection{false};
  bool active{false};
  bool show_masked{false};
  RgbColor color{0, 0, 0};
  std::uint64_t hierarchy{0};
};

struct EffectArgument {
  std::string name;
  std::uint32_t type{0};
  std::span<const std::uint8_t> data;
};

struct EffectRecord {
  std::string name;
  std::string operation;
  bool visible{true};
  float opacity{1.0F};
  std::uint32_t mode{28};        // PROP_MODE: how the filter's result mixes with the layer
  std::int32_t blend_space{0};   // PROP_BLEND_SPACE, 0 or negative for Auto
  std::uint32_t region{1};  // 0: the selection at the time (the mask below), 1: the whole drawable
  std::vector<EffectArgument> arguments;
  std::uint64_t mask{0};    // a channel record: where the effect applies
};

struct LayerRecord {
  std::uint64_t offset{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t type{1};
  std::string name;
  float opacity{1.0F};
  bool visible{true};
  bool lock_alpha{false};
  bool lock_content{false};
  bool lock_position{false};
  bool apply_mask{true};
  bool have_apply_mask{false};
  bool active{false};
  bool group{false};
  bool expanded{true};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint32_t mode{0};
  bool have_mode{false};
  std::int32_t composite_mode{0};
  std::int32_t blend_space{0};
  std::uint32_t color_tag{0};
  std::uint32_t text_flags{0};
  bool vector_layer{false};
  bool link_layer{false};
  std::optional<std::uint64_t> floating_target;
  std::vector<std::uint32_t> item_path;
  std::vector<Parasite> parasites;
  std::uint64_t hierarchy{0};
  std::optional<ChannelRecord> mask;
  std::vector<EffectRecord> effects;
};

struct PathPoint {
  bool anchor{false};
  double x{0.0};
  double y{0.0};
};

struct PathStroke {
  bool closed{false};
  std::vector<PathPoint> points;
};

struct PathRecord {
  std::string name;
  std::vector<PathStroke> strokes;
};

[[nodiscard]] RgbColor color_from_unit(float r, float g, float b) {
  const auto to_byte = [](float value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(std::isfinite(value) ? value * 255.0F : 0.0F), 0L, 255L));
  };
  return RgbColor{to_byte(r), to_byte(g), to_byte(b)};
}

[[nodiscard]] float opacity_from(const Reader& reader, const Property& property, bool is_float) {
  if (is_float) {
    if (property.length < 4) {
      return 1.0F;
    }
    const auto value = reader.f32(property.offset);
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 1.0F;
  }
  return static_cast<float>(std::min<std::uint32_t>(payload_u32(reader, property, 255), 255)) / 255.0F;
}

[[nodiscard]] ChannelRecord read_channel(const Reader& reader, std::uint64_t offset) {
  ChannelRecord channel;
  channel.offset = offset;
  Cursor cursor(reader, offset, reader.size());
  channel.width = cursor.u32();
  channel.height = cursor.u32();
  channel.name = cursor.string();
  bool float_opacity = false;
  bool float_color = false;
  for (const auto& property : read_properties(reader, cursor)) {
    switch (property.type) {
      case kPropOpacity:
        if (!float_opacity) {
          channel.opacity = opacity_from(reader, property, false);
        }
        break;
      case kPropFloatOpacity:
        channel.opacity = opacity_from(reader, property, true);
        float_opacity = true;
        break;
      case kPropVisible:
        channel.visible = payload_u32(reader, property, 0) != 0;
        break;
      case kPropSelection:
        channel.selection = true;
        break;
      case kPropActiveChannel:
        channel.active = true;
        break;
      case kPropShowMasked:
        channel.show_masked = payload_u32(reader, property, 0) != 0;
        break;
      case kPropColor:
        if (!float_color && property.length >= 3) {
          channel.color = RgbColor{reader.u8(property.offset), reader.u8(property.offset + 1),
                                   reader.u8(property.offset + 2)};
        }
        break;
      case kPropFloatColor:
        if (property.length >= 12) {
          channel.color = color_from_unit(reader.f32(property.offset), reader.f32(property.offset + 4),
                                          reader.f32(property.offset + 8));
          float_color = true;
        }
        break;
      default:
        break;
    }
  }
  channel.hierarchy = cursor.pointer();
  return channel;
}

[[nodiscard]] EffectRecord read_effect(const Reader& reader, std::uint64_t offset) {
  EffectRecord effect;
  Cursor cursor(reader, offset, reader.size());
  effect.name = cursor.string();
  (void)cursor.string();  // icon
  effect.operation = cursor.string();
  if (reader.version() >= 22) {
    (void)cursor.string();  // operation version
  }
  bool float_opacity = false;
  for (const auto& property : read_properties(reader, cursor)) {
    switch (property.type) {
      case kPropVisible:
        effect.visible = payload_u32(reader, property, 1) != 0;
        break;
      case kPropOpacity:
        if (!float_opacity) {
          effect.opacity = opacity_from(reader, property, false);
        }
        break;
      case kPropFloatOpacity:
        effect.opacity = opacity_from(reader, property, true);
        float_opacity = true;
        break;
      case kPropFilterRegion:
        effect.region = payload_u32(reader, property, 1);
        break;
      case kPropMode:
        effect.mode = payload_u32(reader, property, 28);
        break;
      case kPropBlendSpace:
        effect.blend_space = static_cast<std::int32_t>(payload_u32(reader, property, 0));
        break;
      case kPropFilterArgument: {
        Cursor argument(reader, property.offset, property.offset + property.length);
        EffectArgument value;
        value.name = argument.string();
        value.type = argument.u32();
        value.data = argument.bytes(argument.remaining());
        effect.arguments.push_back(std::move(value));
        break;
      }
      default:
        break;
    }
  }
  effect.mask = cursor.pointer();
  return effect;
}

[[nodiscard]] LayerRecord read_layer(const Reader& reader, std::uint64_t offset) {
  LayerRecord layer;
  layer.offset = offset;
  Cursor cursor(reader, offset, reader.size());
  layer.width = cursor.u32();
  layer.height = cursor.u32();
  layer.type = cursor.u32();
  layer.name = cursor.string();
  bool float_opacity = false;
  for (const auto& property : read_properties(reader, cursor)) {
    switch (property.type) {
      case kPropActiveLayer:
        layer.active = true;
        break;
      case kPropFloatingSelection:
        if (property.length >= reader.pointer_size()) {
          layer.floating_target = reader.pointer(property.offset);
        } else if (property.length >= 4) {
          layer.floating_target = reader.u32(property.offset);
        }
        break;
      case kPropOpacity:
        if (!float_opacity) {
          layer.opacity = opacity_from(reader, property, false);
        }
        break;
      case kPropFloatOpacity:
        layer.opacity = opacity_from(reader, property, true);
        float_opacity = true;
        break;
      case kPropMode:
        layer.mode = payload_u32(reader, property, 0);
        layer.have_mode = true;
        break;
      case kPropVisible:
        layer.visible = payload_u32(reader, property, 1) != 0;
        break;
      case kPropLockAlpha:
        layer.lock_alpha = payload_u32(reader, property, 0) != 0;
        break;
      case kPropLockContent:
        layer.lock_content = payload_u32(reader, property, 0) != 0;
        break;
      case kPropLockPosition:
        layer.lock_position = payload_u32(reader, property, 0) != 0;
        break;
      case kPropApplyMask:
        layer.apply_mask = payload_u32(reader, property, 1) != 0;
        layer.have_apply_mask = true;
        break;
      case kPropOffsets:
        if (property.length >= 8) {
          layer.x = reader.i32(property.offset);
          layer.y = reader.i32(property.offset + 4);
        }
        break;
      case kPropGroupItem:
        layer.group = true;
        break;
      case kPropGroupItemFlags:
        layer.expanded = (payload_u32(reader, property, 1) & 1U) != 0;
        break;
      case kPropItemPath: {
        layer.item_path.clear();
        for (std::uint64_t at = 0; at + 4 <= property.length && layer.item_path.size() <= kMaxGroupDepth; at += 4) {
          layer.item_path.push_back(reader.u32(property.offset + at));
        }
        break;
      }
      case kPropCompositeMode:
        layer.composite_mode = static_cast<std::int32_t>(payload_u32(reader, property, 0));
        break;
      case kPropBlendSpace:
        layer.blend_space = static_cast<std::int32_t>(payload_u32(reader, property, 0));
        break;
      case kPropColorTag:
        layer.color_tag = payload_u32(reader, property, 0);
        break;
      case kPropTextLayerFlags:
        layer.text_flags = payload_u32(reader, property, 0);
        break;
      case kPropVectorLayer:
        layer.vector_layer = true;
        break;
      case kPropLinkLayer:
        layer.link_layer = true;
        break;
      case kPropParasites:
        layer.parasites = read_parasites(reader, property);
        break;
      default:
        break;
    }
  }
  layer.hierarchy = cursor.pointer();
  const auto mask = cursor.pointer();
  if (mask != 0) {
    layer.mask = read_channel(reader, mask);
  }
  if (reader.version() >= 20) {
    while (true) {
      const auto effect = cursor.pointer();
      if (effect == 0) {
        break;
      }
      if (layer.effects.size() > 1000) {
        throw ImportError("a layer lists too many effects");
      }
      layer.effects.push_back(read_effect(reader, effect));
    }
  }
  return layer;
}

// Strokes of PROP_VECTORS and of the version 18 path structure: per stroke u32 kind (1 =
// Bezier), u32 closed, u32 floats per point (2..6), u32 points, then per point u32 type
// (0 anchor, 1 control) and the floats.
[[nodiscard]] PathStroke read_bezier_stroke(Cursor& cursor) {
  PathStroke stroke;
  const auto kind = cursor.u32();
  stroke.closed = cursor.u32() != 0;
  const auto floats = cursor.u32();
  const auto count = cursor.u32();
  if (kind != 1 || floats < 2 || floats > 6) {
    throw ImportError("a path stroke has an unknown layout");
  }
  if (count > cursor.remaining() / (4ULL + 4ULL * floats)) {
    throw ImportError("a path stroke claims more points than the file holds");
  }
  stroke.points.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    PathPoint point;
    point.anchor = cursor.u32() == 0;
    point.x = cursor.f32();
    point.y = cursor.f32();
    cursor.skip(4ULL * (floats - 2));
    stroke.points.push_back(point);
  }
  return stroke;
}

void read_prop_vectors(const Reader& reader, const Property& property, std::vector<PathRecord>& paths) {
  Cursor cursor(reader, property.offset, property.offset + property.length);
  if (cursor.u32() != 1) {
    throw ImportError("PROP_VECTORS has an unknown version");
  }
  (void)cursor.u32();  // active path
  const auto count = cursor.u32();
  for (std::uint32_t i = 0; i < count && paths.size() < kMaxItems; ++i) {
    PathRecord path;
    path.name = cursor.string();
    (void)cursor.u32();  // tattoo
    (void)cursor.u32();  // visible
    (void)cursor.u32();  // linked
    const auto parasite_count = cursor.u32();
    const auto stroke_count = cursor.u32();
    for (std::uint32_t p = 0; p < parasite_count; ++p) {
      (void)cursor.string();
      (void)cursor.u32();
      cursor.skip(cursor.u32());
    }
    for (std::uint32_t s = 0; s < stroke_count; ++s) {
      path.strokes.push_back(read_bezier_stroke(cursor));
    }
    paths.push_back(std::move(path));
  }
}

// PROP_PATHS, GIMP 1.2's form that GIMP still writes for plain Bezier paths: one stroke per
// path. Files from GIMP 2 and 3 mark anchors 1 and controls 2; the spec's 0 and 1 hold when no
// point is a 2.
void read_prop_paths(const Reader& reader, const Property& property, std::vector<PathRecord>& paths) {
  Cursor cursor(reader, property.offset, property.offset + property.length);
  (void)cursor.u32();  // active path
  const auto count = cursor.u32();
  for (std::uint32_t i = 0; i < count && paths.size() < kMaxItems; ++i) {
    PathRecord path;
    path.name = cursor.string();
    (void)cursor.u32();  // linked
    (void)cursor.u8();   // state
    PathStroke stroke;
    stroke.closed = cursor.u32() != 0;
    const auto point_count = cursor.u32();
    const auto version = cursor.u32();
    if (version < 1 || version > 3) {
      throw ImportError("PROP_PATHS has an unknown path version");
    }
    if (version >= 2) {
      (void)cursor.u32();
    }
    if (version >= 3) {
      (void)cursor.u32();  // tattoo
    }
    if (point_count > cursor.remaining() / 12U) {
      throw ImportError("a path claims more points than the file holds");
    }
    std::vector<std::pair<std::int32_t, PathPoint>> points;
    bool any_two = false;
    for (std::uint32_t p = 0; p < point_count; ++p) {
      const auto type = cursor.i32();
      PathPoint point;
      if (version == 1) {
        point.x = cursor.i32();
        point.y = cursor.i32();
      } else {
        point.x = cursor.f32();
        point.y = cursor.f32();
      }
      any_two = any_two || type == 2;
      points.emplace_back(type, point);
    }
    for (auto& [type, point] : points) {
      point.anchor = any_two ? type == 1 : type == 0;
      stroke.points.push_back(point);
    }
    if (!stroke.points.empty()) {
      path.strokes.push_back(std::move(stroke));
      paths.push_back(std::move(path));
    }
  }
}

[[nodiscard]] PathRecord read_path_structure(const Reader& reader, std::uint64_t offset) {
  PathRecord path;
  Cursor cursor(reader, offset, reader.size());
  path.name = cursor.string();
  (void)cursor.u32();  // payload length
  (void)read_properties(reader, cursor);
  if (cursor.u32() != 1) {
    throw ImportError("a path has an unknown version");
  }
  const auto stroke_count = cursor.u32();
  for (std::uint32_t s = 0; s < stroke_count; ++s) {
    path.strokes.push_back(read_bezier_stroke(cursor));
  }
  return path;
}

// --- tiles --------------------------------------------------------------------------------

enum class Compression : std::uint8_t { None = 0, Rle = 1, Zlib = 2 };

struct TileJob {
  std::uint64_t start{0};
  std::uint64_t end{0};
  std::uint32_t x{0};
  std::uint32_t y{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
};

// The level's tiles, with each tile's data running to the next tile's start (tiles of a
// level are contiguous) or, for the last one, to its largest possible encoded size.
[[nodiscard]] std::vector<TileJob> level_tiles(const Reader& reader, std::uint64_t hierarchy, std::uint32_t width,
                                               std::uint32_t height, std::uint32_t bpp) {
  Cursor cursor(reader, hierarchy, reader.size());
  const auto stored_width = cursor.u32();
  const auto stored_height = cursor.u32();
  const auto stored_bpp = cursor.u32();
  if (stored_width != width || stored_height != height) {
    throw ImportError("a hierarchy's size differs from its drawable");
  }
  if (stored_bpp != bpp) {
    throw ImportError("a hierarchy stores " + std::to_string(stored_bpp) + " bytes per pixel, expected " +
                      std::to_string(bpp));
  }
  const auto level = cursor.pointer();
  Cursor level_cursor(reader, level, reader.size());
  if (level_cursor.u32() != width || level_cursor.u32() != height) {
    throw ImportError("a level's size differs from its drawable");
  }
  const auto columns = (width + kTileSize - 1) / kTileSize;
  const auto rows = (height + kTileSize - 1) / kTileSize;
  const auto count = static_cast<std::uint64_t>(columns) * rows;
  if (count > level_cursor.remaining() / reader.pointer_size() + 1) {
    throw ImportError("a level lists more tiles than the file holds");
  }
  // The list ends at a zero pointer. GIMP's own test files end it early: the tiles past it
  // hold nothing and stay transparent (start 0), which is not damage.
  std::vector<std::uint64_t> starts(static_cast<std::size_t>(count), 0);
  for (auto& start : starts) {
    start = level_cursor.pointer();
    if (start == 0) {
      break;
    }
  }
  std::vector<TileJob> tiles(static_cast<std::size_t>(count));
  const auto max_encoded = static_cast<std::uint64_t>(kTileSize) * kTileSize * bpp * 2ULL + 1024ULL;
  for (std::size_t i = 0; i < tiles.size(); ++i) {
    auto& tile = tiles[i];
    const auto column = static_cast<std::uint32_t>(i % columns);
    const auto row = static_cast<std::uint32_t>(i / columns);
    tile.x = column * kTileSize;
    tile.y = row * kTileSize;
    tile.width = std::min(kTileSize, width - tile.x);
    tile.height = std::min(kTileSize, height - tile.y);
    tile.start = starts[i];
    const auto next = i + 1 < starts.size() ? starts[i + 1] : 0;
    tile.end = next > tile.start ? std::min<std::uint64_t>(next, tile.start + max_encoded) : tile.start + max_encoded;
    tile.end = std::min<std::uint64_t>(tile.end, reader.size());
  }
  return tiles;
}

// Decodes one tile's interleaved bytes (tile width x height x bpp). False when damaged.
[[nodiscard]] bool decode_tile(const Reader& reader, const TileJob& tile, std::uint32_t bpp, Compression compression,
                               std::vector<std::uint8_t>& out) {
  const auto pixels = static_cast<std::size_t>(tile.width) * tile.height;
  out.assign(pixels * bpp, 0);
  if (tile.start == 0) {
    return true;  // an absent tile: transparent
  }
  if (tile.start >= reader.size() || tile.end <= tile.start) {
    return false;
  }
  const auto input = reader.data().subspan(static_cast<std::size_t>(tile.start),
                                           static_cast<std::size_t>(tile.end - tile.start));
  switch (compression) {
    case Compression::None:
      if (input.size() < out.size()) {
        return false;
      }
      std::memcpy(out.data(), input.data(), out.size());
      return true;
    case Compression::Zlib: {
      mz_ulong size = static_cast<mz_ulong>(out.size());
      // mz_uncompress wants the whole stream; GIMP compresses each tile on its own.
      const auto status = mz_uncompress(out.data(), &size, input.data(), static_cast<mz_ulong>(input.size()));
      return (status == MZ_OK || status == MZ_BUF_ERROR) && size == out.size();
    }
    case Compression::Rle: {
      std::size_t at = 0;
      for (std::uint32_t channel = 0; channel < bpp; ++channel) {
        std::size_t filled = 0;
        while (filled < pixels) {
          if (at >= input.size()) {
            return false;
          }
          const auto op = input[at++];
          std::size_t length = 0;
          bool literal = false;
          if (op <= 126) {
            length = static_cast<std::size_t>(op) + 1;
          } else if (op == 127 || op == 128) {
            if (at + 2 > input.size()) {
              return false;
            }
            length = (static_cast<std::size_t>(input[at]) << 8U) | input[at + 1];
            at += 2;
            literal = op == 128;
          } else {
            length = 256U - op;
            literal = true;
          }
          if (length > pixels - filled) {
            return false;
          }
          if (literal) {
            if (at + length > input.size()) {
              return false;
            }
            for (std::size_t i = 0; i < length; ++i) {
              out[(filled + i) * bpp + channel] = input[at + i];
            }
            at += length;
          } else {
            if (at >= input.size()) {
              return false;
            }
            const auto value = input[at++];
            for (std::size_t i = 0; i < length; ++i) {
              out[(filled + i) * bpp + channel] = value;
            }
          }
          filled += length;
        }
      }
      return true;
    }
  }
  return false;
}

template <typename Work>
void run_in_parallel(std::size_t count, Work work) {
  if (count == 0) {
    return;
  }
  const auto workers = static_cast<std::size_t>(std::max(
      1, max_blocking_fanout_workers(static_cast<int>(std::min<std::size_t>(
             std::min<std::size_t>(static_cast<std::size_t>(hardware_worker_threads()), 16), count)))));
  if (workers <= 1) {
    for (std::size_t i = 0; i < count; ++i) {
      work(i);
    }
    return;
  }
  std::atomic<std::size_t> next{0};
  std::vector<std::future<void>> futures;
  futures.reserve(workers);
  for (std::size_t w = 0; w < workers; ++w) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
        work(i);
      }
    }));
  }
  for (auto& future : futures) {
    future.get();
  }
}

// --- samples ------------------------------------------------------------------------------

[[nodiscard]] float half_to_float(std::uint16_t half) noexcept {
  const std::uint32_t sign = (half >> 15U) & 1U;
  const std::uint32_t exponent = (half >> 10U) & 0x1FU;
  const std::uint32_t mantissa = half & 0x3FFU;
  float value = 0.0F;
  if (exponent == 0) {
    value = std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 31) {
    value = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
  } else {
    value = std::ldexp(static_cast<float>(mantissa | 0x400U), static_cast<int>(exponent) - 25);
  }
  return sign != 0 ? -value : value;
}

// One component at `p` as a float (integers scaled to 0..1).
[[nodiscard]] float load_component(const std::uint8_t* p, Component component) noexcept {
  switch (component) {
    case Component::U8:
      return static_cast<float>(p[0]) / 255.0F;
    case Component::U16:
      return static_cast<float>((static_cast<std::uint32_t>(p[0]) << 8U) | p[1]) / 65535.0F;
    case Component::U32: {
      const auto value = (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
                         (static_cast<std::uint32_t>(p[2]) << 8U) | p[3];
      return static_cast<float>(static_cast<double>(value) / 4294967295.0);
    }
    case Component::Half:
      return half_to_float(static_cast<std::uint16_t>((static_cast<std::uint32_t>(p[0]) << 8U) | p[1]));
    case Component::Float: {
      const auto bits = (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
                        (static_cast<std::uint32_t>(p[2]) << 8U) | p[3];
      float value = 0.0F;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    }
    case Component::Double: {
      std::uint64_t bits = 0;
      for (int i = 0; i < 8; ++i) {
        bits = (bits << 8U) | p[i];
      }
      double value = 0.0;
      std::memcpy(&value, &bits, sizeof(value));
      return static_cast<float>(value);
    }
  }
  return 0.0F;
}

[[nodiscard]] float finite_or_zero(float value) noexcept {
  return std::isfinite(value) ? value : 0.0F;
}

// sRGB curve on unbounded values: mirrored below zero, the power segment continued above one.
[[nodiscard]] float decode_unbounded(float encoded) noexcept {
  const float magnitude = std::abs(encoded);
  const float linear = magnitude <= 1.0F ? static_cast<float>(srgb_decode(magnitude))
                                         : std::pow((magnitude + 0.055F) / 1.055F, 2.4F);
  return encoded < 0.0F ? -linear : linear;
}

[[nodiscard]] float encode_clamped(float linear) noexcept {
  return static_cast<float>(srgb_encode(std::clamp(linear, 0.0F, 1.0F)));
}

enum class BaseType : std::uint8_t { Rgb = 0, Gray = 1, Indexed = 2 };

struct PixelLayout {
  BaseType base{BaseType::Rgb};
  bool alpha{true};
  Precision precision{};
  [[nodiscard]] std::uint32_t color_channels() const noexcept { return base == BaseType::Rgb ? 3U : 1U; }
  [[nodiscard]] std::uint32_t component() const noexcept {
    return base == BaseType::Indexed ? 1U : component_bytes(precision.component);
  }
  [[nodiscard]] std::uint32_t bpp() const noexcept { return (color_channels() + (alpha ? 1U : 0U)) * component(); }
};

struct ConvertContext {
  PixelLayout layout;
  BitDepth depth{BitDepth::UInt8};
  const std::vector<RgbColor>* colormap{nullptr};
};

void store_rgba(PixelBuffer& buffer, std::uint8_t* row_base, std::int32_t x, BitDepth depth, const float rgba[4],
                std::uint16_t channels) {
  const auto pixel_bytes = bytes_per_channel(depth) * channels;
  auto* out = row_base + static_cast<std::size_t>(x) * pixel_bytes;
  (void)buffer;
  for (std::uint16_t c = 0; c < channels; ++c) {
    const float value = rgba[c];
    switch (depth) {
      case BitDepth::UInt8:
        out[c] = static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0F), 0L, 255L));
        break;
      case BitDepth::UInt16: {
        const auto sample = static_cast<std::uint16_t>(std::clamp(std::lround(value * 65535.0F), 0L, 65535L));
        std::memcpy(out + c * 2U, &sample, sizeof(sample));
        break;
      }
      case BitDepth::Float32: {
        const float sample = c == 3 ? std::clamp(value, 0.0F, 1.0F) : value;
        std::memcpy(out + c * 4U, &sample, sizeof(sample));
        break;
      }
    }
  }
}

// Converts one decoded tile into the layer buffer (straight RGBA or RGB at the document's
// depth: encoded sRGB at 8 and 16 bits, linear light at 32).
void convert_tile_pixels(const ConvertContext& context, const TileJob& tile, const std::vector<std::uint8_t>& raw,
                         PixelBuffer& target, std::span<std::uint8_t> target_bytes) {
  const auto& layout = context.layout;
  const auto component = layout.component();
  const auto bpp = layout.bpp();
  const auto channels = target.format().channels;
  const auto stride = target.stride_bytes();
  const bool fast_u8 = layout.base == BaseType::Rgb && layout.precision.component == Component::U8 &&
                       layout.precision.transfer != Transfer::Linear && context.depth == BitDepth::UInt8;
  for (std::uint32_t ty = 0; ty < tile.height; ++ty) {
    auto* row = target_bytes.data() + static_cast<std::size_t>(tile.y + ty) * stride;
    const auto* in_row = raw.data() + static_cast<std::size_t>(ty) * tile.width * bpp;
    if (fast_u8) {
      for (std::uint32_t tx = 0; tx < tile.width; ++tx) {
        const auto* in = in_row + static_cast<std::size_t>(tx) * bpp;
        auto* out = row + static_cast<std::size_t>(tile.x + tx) * channels;
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        if (channels == 4) {
          out[3] = layout.alpha ? in[3] : 255;
        }
      }
      continue;
    }
    for (std::uint32_t tx = 0; tx < tile.width; ++tx) {
      const auto* in = in_row + static_cast<std::size_t>(tx) * bpp;
      float rgba[4]{0.0F, 0.0F, 0.0F, 1.0F};
      if (layout.base == BaseType::Indexed) {
        const auto index = in[0];
        if (context.colormap != nullptr && index < context.colormap->size()) {
          const auto color = (*context.colormap)[index];
          rgba[0] = static_cast<float>(color.red) / 255.0F;
          rgba[1] = static_cast<float>(color.green) / 255.0F;
          rgba[2] = static_cast<float>(color.blue) / 255.0F;
        }
        // GIMP renders indexed alpha as on or off at the halfway point.
        rgba[3] = layout.alpha ? (in[1] >= 128 ? 1.0F : 0.0F) : 1.0F;
        if (context.depth == BitDepth::Float32) {
          for (int c = 0; c < 3; ++c) {
            rgba[c] = decode_unbounded(rgba[c]);
          }
        }
      } else {
        const bool linear = layout.precision.transfer == Transfer::Linear;
        for (std::uint32_t c = 0; c < layout.color_channels(); ++c) {
          float value = finite_or_zero(load_component(in + c * component, layout.precision.component));
          if (context.depth == BitDepth::Float32) {
            value = linear ? value : decode_unbounded(value);
          } else {
            value = linear ? encode_clamped(value) : std::clamp(value, 0.0F, 1.0F);
          }
          rgba[c] = value;
        }
        if (layout.base == BaseType::Gray) {
          rgba[1] = rgba[0];
          rgba[2] = rgba[0];
        }
        if (layout.alpha) {
          rgba[3] = std::clamp(
              finite_or_zero(load_component(in + layout.color_channels() * component, layout.precision.component)),
              0.0F, 1.0F);
        }
      }
      store_rgba(target, row, static_cast<std::int32_t>(tile.x + tx), context.depth, rgba, channels);
    }
  }
}

// Converts one decoded single-channel tile (a mask or channel) into coverage at `depth`.
// A mask value is coverage whatever the image's transfer curve: GIMP multiplies it in as is.
void convert_coverage_tile(Component component, BitDepth depth, const TileJob& tile,
                           const std::vector<std::uint8_t>& raw, std::size_t stride,
                           std::span<std::uint8_t> target_bytes) {
  const auto size = component_bytes(component);
  for (std::uint32_t ty = 0; ty < tile.height; ++ty) {
    auto* row = target_bytes.data() + static_cast<std::size_t>(tile.y + ty) * stride;
    for (std::uint32_t tx = 0; tx < tile.width; ++tx) {
      const auto* in = raw.data() + (static_cast<std::size_t>(ty) * tile.width + tx) * size;
      const float value = std::clamp(finite_or_zero(load_component(in, component)), 0.0F, 1.0F);
      const auto x = static_cast<std::size_t>(tile.x + tx);
      switch (depth) {
        case BitDepth::UInt8:
          row[x] = component == Component::U8 ? in[0]
                                              : static_cast<std::uint8_t>(std::lround(value * 255.0F));
          break;
        case BitDepth::UInt16: {
          const auto sample = static_cast<std::uint16_t>(std::lround(value * 65535.0F));
          std::memcpy(row + x * 2U, &sample, sizeof(sample));
          break;
        }
        case BitDepth::Float32:
          std::memcpy(row + x * 4U, &value, sizeof(value));
          break;
      }
    }
  }
}

struct DecodeStats {
  std::atomic<std::size_t> damaged_tiles{0};
};

[[nodiscard]] PixelBuffer decode_color_drawable(const Reader& reader, std::uint64_t hierarchy, std::uint32_t width,
                                                std::uint32_t height, const ConvertContext& context,
                                                Compression compression, bool keep_alpha, DecodeStats& stats) {
  PixelFormat format = with_bit_depth(PixelFormat::rgba8(), context.depth);
  format.channels = keep_alpha ? 4 : 3;
  PixelBuffer pixels(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), format);
  pixels.clear(0);
  if (width == 0 || height == 0) {
    return pixels;
  }
  std::vector<TileJob> tiles;
  try {
    tiles = level_tiles(reader, hierarchy, width, height, context.layout.bpp());
  } catch (const ImportError& error) {
    trace(error.what());
    stats.damaged_tiles.fetch_add(1);
    return pixels;
  }
  // Detach the copy-on-write storage on this thread before the workers write (AGENTS.md).
  auto bytes = pixels.data();
  run_in_parallel(tiles.size(), [&](std::size_t index) {
    std::vector<std::uint8_t> raw;
    if (!decode_tile(reader, tiles[index], context.layout.bpp(), compression, raw)) {
      stats.damaged_tiles.fetch_add(1);
    }
    convert_tile_pixels(context, tiles[index], raw, pixels, bytes);
  });
  return pixels;
}

[[nodiscard]] PixelBuffer decode_coverage_drawable(const Reader& reader, const ChannelRecord& channel,
                                                   Component component, BitDepth depth, Compression compression,
                                                   DecodeStats& stats) {
  PixelBuffer pixels(static_cast<std::int32_t>(channel.width), static_cast<std::int32_t>(channel.height),
                     with_bit_depth(PixelFormat::gray8(), depth));
  pixels.clear(0);
  if (channel.width == 0 || channel.height == 0) {
    return pixels;
  }
  std::vector<TileJob> tiles;
  try {
    tiles = level_tiles(reader, channel.hierarchy, channel.width, channel.height, component_bytes(component));
  } catch (const ImportError& error) {
    trace(error.what());
    stats.damaged_tiles.fetch_add(1);
    return pixels;
  }
  auto bytes = pixels.data();
  const auto stride = pixels.stride_bytes();
  run_in_parallel(tiles.size(), [&](std::size_t index) {
    std::vector<std::uint8_t> raw;
    if (!decode_tile(reader, tiles[index], component_bytes(component), compression, raw)) {
      stats.damaged_tiles.fetch_add(1);
    }
    convert_coverage_tile(component, depth, tiles[index], raw, stride, bytes);
  });
  return pixels;
}

// --- blend modes --------------------------------------------------------------------------

enum class ModeFit : std::uint8_t {
  Exact,        // the same formula (within rounding; GIMP blends some modes in linear light)
  Approximate,  // another color model or formula: the closest Patchy mode
  Missing,      // no equivalent: Normal
};

struct ModeMapping {
  BlendMode mode{BlendMode::Normal};
  ModeFit fit{ModeFit::Exact};
};

// The blend space a mode uses when PROP_BLEND_SPACE leaves it to Auto (its default):
// 1 linear light, 2 the image's profile curve, 3 LAB, 4 perceptual (sRGB curve). Legacy
// modes (below 28, and Overlay's 23 aside) blend in the profile's curve.
[[nodiscard]] int default_blend_space(std::uint32_t mode) {
  switch (mode) {
    case 30:  // Multiply
    case 33:  // Addition
    case 34:  // Subtract
    case 41:  // Divide
    case 54:  // Luma darken only
    case 55:  // Luma lighten only
    case 56:  // Luminance
    case 57:  // Color erase
      return 1;
    case 24:
    case 25:
    case 26:
    case 27:
      return 3;
    case 37:
    case 38:
    case 39:
    case 40:
      return 2;
    default:
      return mode < 28 && mode != 23 ? 2 : 4;
  }
}

// PROP_MODE values (docs/xcf.md, "Blend modes"). GIMP's color-model and linear-light
// modes have their own Patchy modes; a mode set to blend in a space other than its
// default is approximated by its usual form.
[[nodiscard]] ModeMapping map_mode(std::uint32_t mode, bool group, std::int32_t stored_blend_space = 0) {
  using M = BlendMode;
  const int default_space = default_blend_space(mode);
  const int space = stored_blend_space != 0 && std::abs(stored_blend_space) <= 4 ? std::abs(stored_blend_space)
                                                                                 : default_space;
  const bool linear = space == 1;
  // Profile curve and perceptual blend the same for sRGB images; LAB only matters to LCh.
  const bool moved = (space == 1) != (default_space == 1);
  const auto fit = [moved](M blend) { return ModeMapping{blend, moved ? ModeFit::Approximate : ModeFit::Exact}; };
  switch (mode) {
    case 0:
    case 28:
      return {M::Normal, ModeFit::Exact};
    case 1:
      return {M::Dissolve, ModeFit::Exact};
    case 2:
    case 29:
      return {M::Normal, ModeFit::Missing};  // Behind: a paint mode
    case 3:
    case 30:
      return {linear ? M::LinearMultiply : M::Multiply, ModeFit::Exact};
    case 4:
    case 31:
      return fit(M::Screen);
    case 5:   // the old broken Overlay, identical to legacy Soft Light
    case 19:
    case 45:  // GIMP's soft light: within six levels of Photoshop's
      return fit(M::SoftLight);
    case 6:
    case 32:
      return fit(M::Difference);
    case 7:
    case 33:
      return {linear ? M::LinearAddition : M::LinearDodge, ModeFit::Exact};
    case 8:
    case 34:
      return {linear ? M::LinearSubtract : M::Subtract, ModeFit::Exact};
    case 9:
    case 35:
      return fit(M::Darken);
    case 10:
    case 36:
      return fit(M::Lighten);
    case 11:
    case 37:
      return fit(M::HsvHue);
    case 12:
    case 38:
      return fit(M::HsvSaturation);
    case 13:
    case 39:
      return fit(M::HslColor);
    case 14:
    case 40:
      return fit(M::HsvValue);
    case 15:
    case 41:
      return {linear ? M::LinearDivide : M::Divide, ModeFit::Exact};
    case 16:
    case 42:
      return fit(M::ColorDodge);
    case 17:
    case 43:
      return fit(M::ColorBurn);
    case 18:
    case 44:
      return fit(M::HardLight);
    case 20:
    case 46:
      return fit(M::GrainExtract);
    case 21:
    case 47:
      return fit(M::GrainMerge);
    case 23:
      return fit(M::Overlay);
    case 24:
      return fit(M::LchHue);
    case 25:
      return fit(M::LchChroma);
    case 26:
      return fit(M::LchColor);
    case 27:
      return fit(M::LchLightness);
    case 48:
      return fit(M::VividLight);
    case 49:
      return fit(M::PinLight);
    case 50:
      return fit(M::LinearLight);
    case 51:
      return fit(M::HardMix);
    case 52:
      return fit(M::Exclusion);
    case 53:
      return fit(M::LinearBurn);
    case 54:
      return fit(M::LumaDarkenOnly);
    case 55:
      return fit(M::LumaLightenOnly);
    case 56:
      return fit(M::Luminance);
    case 61:
      return {group ? M::PassThrough : M::Normal, group ? ModeFit::Exact : ModeFit::Missing};
    case 58:  // Erase and Split: masks on what lies below (convert_erase_layers)
    case 60:
      return {M::Normal, ModeFit::Exact};
    case 22:  // Color Erase (legacy)
    case 57:  // Color Erase
    case 59:  // Merge
    default:
      return {M::Normal, ModeFit::Missing};
  }
}

// --- notices ------------------------------------------------------------------------------

enum class Notice : std::uint8_t {
  DamagedPixels,
  ModesApproximated,
  ModesMissing,
  FloatingAsLayer,
  FloatingDropped,
  TextModified,
  TextUnreadable,
  EffectsNotApplied,
  VectorLayers,
  LinkLayers,
  DepthNarrowedForModes,
  DeepDisabled,
  DamagedStructure,
  ClipToBackdropMasks,
  CompositeApproximated,
  EraseAsMask,
  EffectsBaked,
  ClipToBackdropClipped,
};

[[nodiscard]] const char* notice_text(Notice notice) {
  switch (notice) {
    case Notice::DamagedPixels:
      return PATCHY_TRANSLATE_NOOP("QObject", "Some layer pixels in this GIMP file are damaged; those areas opened transparent.");
    case Notice::ModesApproximated:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Some layers use GIMP blend modes set to blend in another color space than usual; they opened in the closest "
          "Patchy mode.");
    case Notice::ModesMissing:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Some layers use GIMP blend modes Patchy does not have (Behind, Color Erase or Merge); they opened as "
          "Normal.");
    case Notice::FloatingAsLayer:
      return PATCHY_TRANSLATE_NOOP(
          "QObject", "The floating selection opened as an ordinary layer above the layer it was attached to.");
    case Notice::FloatingDropped:
      return PATCHY_TRANSLATE_NOOP(
          "QObject", "The floating selection was attached to a channel or layer mask, so it was left out.");
    case Notice::TextModified:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Some text layers were painted on in GIMP after their text was set, so they opened as pixel layers.");
    case Notice::TextUnreadable:
      return PATCHY_TRANSLATE_NOOP(
          "QObject", "Some text layers could not be read as text, so they opened as pixel layers.");
    case Notice::EffectsNotApplied:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Some GIMP layer effects have no Patchy equivalent and were not applied; those layers show their pixels "
          "without the effect.");
    case Notice::VectorLayers:
      return PATCHY_TRANSLATE_NOOP("QObject", "GIMP vector layers opened as pixel layers.");
    case Notice::LinkLayers:
      return PATCHY_TRANSLATE_NOOP(
          "QObject", "GIMP link layers opened as pixel layers; they no longer follow their linked files.");
    case Notice::DepthNarrowedForModes:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "This floating-point GIMP image opened at 16 bits per channel, because it uses blend modes Patchy offers "
          "only at 8 and 16 bits.");
    case Notice::DeepDisabled:
      return PATCHY_TRANSLATE_NOOP("QObject", "This high bit depth GIMP image was converted to 8 bits per channel.");
    case Notice::ClipToBackdropMasks:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Some layers show only where the layers below them are, as GIMP composites them; they got layer masks made "
          "from the layers below.");
    case Notice::ClipToBackdropClipped:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Some layers show only where the layer below them is, as GIMP composites them; they opened as clipping "
          "masks on it.");
    case Notice::EffectsBaked:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "GIMP layer effects Patchy can reproduce (Gaussian Blur and color filters such as Levels, Hue-Saturation "
          "and Desaturate) were applied to their layers' pixels.");
    case Notice::EraseAsMask:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Layers in GIMP's Erase or Split mode became masks: the layers below each one are in a group named after it, "
          "masked where it erased.");
    case Notice::CompositeApproximated:
      return PATCHY_TRANSLATE_NOOP(
          "QObject",
          "Some layers use GIMP composite modes Patchy approximates; they may look different where the layers below "
          "them are transparent.");
    case Notice::DamagedStructure:
      return PATCHY_TRANSLATE_NOOP(
          "QObject", "Parts of this GIMP file are damaged; the layers that could be read opened.");
  }
  return "";
}

// --- assembly -----------------------------------------------------------------------------

struct Build {
  const Reader& reader;
  PixelLayout image_layout;
  BitDepth depth{BitDepth::UInt8};
  Compression compression{Compression::Rle};
  std::vector<RgbColor> colormap;
  double ppi{72.0};
  Document& document;
  DecodeStats stats;
  std::set<Notice> notices;
  std::uint64_t decoded_bytes{0};
  std::optional<LayerId> active;
  // GIMP's composite mode per layer (1 union, 2 clip to backdrop, 3 clip to layer,
  // 4 intersection), for the clip-to-backdrop pass.
  std::unordered_map<LayerId, int> composite;
  // Visible layers in GIMP's Erase or Split mode: they become masks (convert_erase_layers).
  std::set<LayerId> erasers;
};

// The composite mode a layer renders with: PROP_COMPOSITE_MODE's absolute value (a negative
// value is "Auto" resolved when GIMP saved), else what Auto resolves to for the mode. Files
// from before GIMP 2.10 carry no such property; their non-Normal modes clip to the backdrop.
[[nodiscard]] int composite_kind(const LayerRecord& record) {
  if (record.composite_mode != 0 && std::abs(record.composite_mode) <= 4) {
    return std::abs(record.composite_mode);
  }
  const auto mode = record.have_mode ? record.mode : 0U;
  switch (mode) {
    case 0:
    case 1:
    case 2:
    case 28:
    case 29:
    case 59:
    case 61:
      return 1;
    default:
      return 2;
  }
}

[[nodiscard]] LayerColorLabel color_label_for_tag(std::uint32_t tag) {
  switch (tag) {
    case 1:
      return kLayerColorBlue;
    case 2:
      return kLayerColorGreen;
    case 3:
      return kLayerColorYellow;
    case 4:
    case 5:  // brown: Photoshop's palette has no brown
      return kLayerColorOrange;
    case 6:
      return kLayerColorRed;
    case 7:
      return kLayerColorViolet;
    case 8:
      return kLayerColorGray;
    default:
      return kLayerColorNone;
  }
}

void account(Build& build, std::uint64_t width, std::uint64_t height) {
  if (width > static_cast<std::uint64_t>(kMaxSide) || height > static_cast<std::uint64_t>(kMaxSide) ||
      width * height > kMaxLayerPixels) {
    throw SizeLimitError("a drawable is " + std::to_string(width) + " x " + std::to_string(height));
  }
  build.decoded_bytes += width * height * 16ULL;
  if (build.decoded_bytes > kMaxDecodedBytes) {
    throw SizeLimitError("the drawables decode to more than the limit");
  }
}

[[nodiscard]] std::optional<LayerMask> build_mask(Build& build, const ChannelRecord& channel, Rect bounds,
                                                  bool apply) {
  if (channel.width != static_cast<std::uint32_t>(bounds.width) ||
      channel.height != static_cast<std::uint32_t>(bounds.height)) {
    trace("a layer mask's size differs from its layer");
    build.notices.insert(Notice::DamagedPixels);
    return std::nullopt;
  }
  account(build, channel.width, channel.height);
  LayerMask mask;
  mask.bounds = bounds;
  mask.pixels = decode_coverage_drawable(build.reader, channel, build.image_layout.precision.component, build.depth,
                                         build.compression, build.stats);
  mask.default_color = 255;
  mask.disabled = !apply;
  return mask;
}

// The Photoshop mode nearest a GIMP-only one, for an effect, whose Blend Mode list offers
// only Photoshop's modes.
[[nodiscard]] BlendMode photoshop_effect_mode(BlendMode mode) {
  using M = BlendMode;
  switch (mode) {
    case M::LinearMultiply:
      return M::Multiply;
    case M::LinearAddition:
      return M::LinearDodge;
    case M::LinearSubtract:
    case M::GrainExtract:
      return M::Subtract;
    case M::LinearDivide:
      return M::Divide;
    case M::GrainMerge:
      return M::LinearLight;
    case M::HsvHue:
    case M::LchHue:
      return M::Hue;
    case M::HsvSaturation:
    case M::LchChroma:
      return M::Saturation;
    case M::HslColor:
    case M::LchColor:
      return M::Color;
    case M::HsvValue:
    case M::LchLightness:
    case M::Luminance:
      return M::Luminosity;
    case M::LumaDarkenOnly:
      return M::DarkerColor;
    case M::LumaLightenOnly:
      return M::LighterColor;
    case M::PassThrough:
      return M::Normal;
    default:
      return mode;
  }
}

// GEGL mixes the shadow in linear light, Patchy's 8 and 16-bit documents in display
// values, where the same opacity reads darker. The contour carries the difference so the
// dialog keeps GIMP's opacity: at each mask level m it holds the display coverage that best
// gives GEGL's mix of `color` at `opacity * m` over white, 75% and 50% gray (least squares
// over the three channels and backdrops), divided by `opacity` (docs/xcf.md, "Layer effects").
[[nodiscard]] StyleContour linear_light_shadow_contour(RgbColor color, double opacity) {
  constexpr int kPoints = 9;
  constexpr std::array<double, 3> kBackdrops{1.0, 0.75, 0.5};
  const std::array<double, 3> channels{color.red / 255.0, color.green / 255.0, color.blue / 255.0};
  StyleContour contour;
  contour.name = "GIMP linear light";
  for (int i = 0; i < kPoints; ++i) {
    const double level = static_cast<double>(i) / (kPoints - 1);
    const double alpha = opacity * level;
    double numerator = 0.0;
    double denominator = 0.0;
    for (const double backdrop : kBackdrops) {
      const double backdrop_linear = srgb_decode(backdrop);
      for (const double channel : channels) {
        const double gimp = srgb_encode(srgb_decode(channel) * alpha + backdrop_linear * (1.0 - alpha));
        numerator += (channel - backdrop) * (gimp - backdrop);
        denominator += (channel - backdrop) * (channel - backdrop);
      }
    }
    const double coverage = denominator > 0.0 ? numerator / denominator : alpha;
    const double output = opacity > 0.0 ? std::clamp(coverage / opacity, 0.0, 1.0) : level;
    contour.points.push_back(StyleContourPoint{static_cast<float>(level * 255.0),
                                               static_cast<float>(std::round(output * 25500.0) / 100.0), false});
  }
  return contour;
}

// GEGL's drop shadow as a Photoshop-style Drop Shadow (docs/xcf.md, "Layer effects"):
// GEGL's x/y offset becomes the distance and light angle, its blur radius the size, and
// its color and opacity carry over. The filter's own mode mixes its result (the layer over
// the shadow) with the layer: Normal, Replace, Behind and Merge show a plain shadow, Dissolve
// a dissolved one, and every other mode none (the shadow opens switched off with the
// nearest Photoshop mode). `linear_light` is true for documents Patchy mixes in linear
// light; the others get linear_light_shadow_contour.
[[nodiscard]] std::optional<LayerDropShadow> drop_shadow_from_effect(const EffectRecord& effect,
                                                                     bool linear_light) {
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = BlendMode::Normal;
  switch (effect.mode) {
    case 0:
    case 2:
    case 28:
    case 29:
    case 59:
    case 62:
      break;
    case 1:
      shadow.blend_mode = BlendMode::Dissolve;
      break;
    default:
      shadow.enabled = false;
      shadow.blend_mode = photoshop_effect_mode(map_mode(effect.mode, false, effect.blend_space).mode);
      break;
  }
  double dx = 20.0;
  double dy = 20.0;
  double radius = 10.0;
  double grow = 0.0;
  double opacity = 0.5;
  const auto f32_of = [](const EffectArgument& argument, double fallback) {
    if (argument.type != 3 || argument.data.size() < 4) {
      return fallback;
    }
    const std::uint32_t bits = (static_cast<std::uint32_t>(argument.data[0]) << 24U) |
                               (static_cast<std::uint32_t>(argument.data[1]) << 16U) |
                               (static_cast<std::uint32_t>(argument.data[2]) << 8U) | argument.data[3];
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value) ? static_cast<double>(value) : fallback;
  };
  double color_alpha = 1.0;
  for (const auto& argument : effect.arguments) {
    if (argument.name == "x") {
      dx = f32_of(argument, dx);
    } else if (argument.name == "y") {
      dy = f32_of(argument, dy);
    } else if (argument.name == "radius") {
      radius = f32_of(argument, radius);
    } else if (argument.name == "grow-radius") {
      grow = f32_of(argument, grow);
    } else if (argument.name == "opacity") {
      opacity = f32_of(argument, opacity);
    } else if (argument.name == "color" && argument.type == 8) {
      // GeglColor: string babl format, u32 size, bytes, u32 profile size, profile.
      Reader nested(argument.data, 0);
      Cursor cursor(nested, 0, argument.data.size());
      try {
        const auto format = cursor.string();
        const auto size = cursor.u32();
        const auto bytes = cursor.bytes(size);
        if (const auto rgba = detail::gegl_color_to_srgb(format, bytes)) {
          shadow.color = color_from_unit(static_cast<float>((*rgba)[0]), static_cast<float>((*rgba)[1]),
                                         static_cast<float>((*rgba)[2]));
          color_alpha = (*rgba)[3];
        } else {
          trace("a drop shadow color in an unknown format: " + format);
        }
      } catch (const ImportError&) {
      }
    }
  }
  const double distance = std::hypot(dx, dy);
  shadow.distance = static_cast<float>(std::min(distance, 30000.0));
  shadow.angle_degrees =
      distance > 0.0 ? static_cast<float>(std::atan2(dy, -dx) * 180.0 / 3.14159265358979323846) : 120.0F;
  // Fitted on effects.xcf against GIMP 3.2's render (docs/xcf.md, "Layer effects"): a size
  // of three times GEGL's blur radius plus the grow radius, the grow share as spread.
  const double size = 3.0 * radius + grow;
  shadow.size = static_cast<float>(std::clamp(size, 0.0, 250.0));
  shadow.spread = size > 0.0 ? static_cast<float>(std::clamp(grow / size * 100.0, 0.0, 100.0))
                                      : 0.0F;
  const double total = std::clamp(opacity * color_alpha * static_cast<double>(effect.opacity), 0.0, 1.0);
  shadow.opacity = static_cast<float>(total);
  // GEGL draws the layer over its shadow, which shows through the layer's soft edges.
  shadow.layer_conceals = false;
  if (shadow.enabled && shadow.blend_mode == BlendMode::Normal && !linear_light) {
    shadow.contour = linear_light_shadow_contour(shadow.color, total);
  }
  return shadow;
}

[[nodiscard]] const EffectArgument* find_argument(const EffectRecord& effect, std::string_view name) {
  for (const auto& argument : effect.arguments) {
    if (argument.name == name) {
      return &argument;
    }
  }
  return nullptr;
}

[[nodiscard]] std::optional<double> float_argument(const EffectRecord& effect, std::string_view name) {
  const auto* argument = find_argument(effect, name);
  if (argument == nullptr || argument->data.size() < 4) {
    return std::nullopt;
  }
  const std::uint32_t bits = (static_cast<std::uint32_t>(argument->data[0]) << 24U) |
                             (static_cast<std::uint32_t>(argument->data[1]) << 16U) |
                             (static_cast<std::uint32_t>(argument->data[2]) << 8U) | argument->data[3];
  if (argument->type == 3) {
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
  }
  if (argument->type == 1 || argument->type == 7) {
    return static_cast<double>(argument->type == 1 ? static_cast<double>(static_cast<std::int32_t>(bits)) : bits);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> enum_argument(const EffectRecord& effect, std::string_view name) {
  const auto* argument = find_argument(effect, name);
  if (argument == nullptr || argument->data.size() < 4 ||
      (argument->type != 1 && argument->type != 2 && argument->type != 5 && argument->type != 7)) {
    return std::nullopt;
  }
  return (static_cast<std::uint32_t>(argument->data[0]) << 24U) | (static_cast<std::uint32_t>(argument->data[1]) << 16U) |
         (static_cast<std::uint32_t>(argument->data[2]) << 8U) | argument->data[3];
}

// One box pass of `radius` along rows or columns, by running sums; edges as in gaussian_pass.
void box_pass(std::vector<float>& rgba, std::int32_t width, std::int32_t height, std::int32_t radius, bool horizontal,
              bool clamp_edges) {
  const std::int32_t lines = horizontal ? height : width;
  const std::int32_t length = horizontal ? width : height;
  const float scale = 1.0F / static_cast<float>(2 * radius + 1);
  const std::size_t step = horizontal ? 4U : static_cast<std::size_t>(width) * 4U;
  run_in_parallel(static_cast<std::size_t>(lines), [&](std::size_t line_index) {
    auto* first = rgba.data() + (horizontal ? line_index * static_cast<std::size_t>(width) * 4U : line_index * 4U);
    std::vector<float> line(static_cast<std::size_t>(length) * 4U);
    for (std::int32_t i = 0; i < length; ++i) {
      std::memcpy(line.data() + static_cast<std::size_t>(i) * 4U, first + static_cast<std::size_t>(i) * step,
                  4U * sizeof(float));
    }
    const auto at = [&](std::int32_t j) -> const float* {
      if (j < 0 || j >= length) {
        if (!clamp_edges) {
          return nullptr;
        }
        j = std::clamp(j, 0, length - 1);
      }
      return line.data() + static_cast<std::size_t>(j) * 4U;
    };
    double sum[4]{0.0, 0.0, 0.0, 0.0};
    for (std::int32_t k = -radius; k <= radius; ++k) {
      if (const auto* px = at(k)) {
        for (int c = 0; c < 4; ++c) sum[c] += px[c];
      }
    }
    for (std::int32_t i = 0; i < length; ++i) {
      auto* out = first + static_cast<std::size_t>(i) * step;
      for (int c = 0; c < 4; ++c) out[c] = static_cast<float>(sum[c]) * scale;
      if (const auto* leaving = at(i - radius)) {
        for (int c = 0; c < 4; ++c) sum[c] -= leaving[c];
      }
      if (const auto* entering = at(i + radius + 1)) {
        for (int c = 0; c < 4; ++c) sum[c] += entering[c];
      }
    }
  });
}

// One pass of a Gaussian along rows (horizontal) or columns, on premultiplied linear RGBA.
// `clamp_edges` repeats the edge pixel past the ends (GEGL's clamp abyss); otherwise the
// outside is transparent. Small sigmas use the sampled kernel; larger ones three box passes,
// which stay within a level of it and cost the same at any radius.
void gaussian_pass(std::vector<float>& rgba, std::int32_t width, std::int32_t height, double sigma, bool horizontal,
                   bool clamp_edges) {
  if (sigma < 0.05) {
    return;
  }
  if (sigma > 3.0) {
    // Three boxes whose variances sum to sigma squared (the usual widths w and w + 2).
    const double ideal = std::sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
    auto lower = static_cast<std::int32_t>(std::floor(ideal));
    if (lower % 2 == 0) {
      --lower;
    }
    const auto upper = lower + 2;
    const auto lower_count = static_cast<std::int32_t>(std::lround(
        (12.0 * sigma * sigma - 3.0 * lower * lower - 12.0 * lower - 9.0) / (-4.0 * lower - 4.0)));
    for (std::int32_t pass = 0; pass < 3; ++pass) {
      const auto size = pass < lower_count ? lower : upper;
      box_pass(rgba, width, height, (size - 1) / 2, horizontal, clamp_edges);
    }
    return;
  }
  const auto radius = static_cast<std::int32_t>(std::ceil(3.0 * sigma));
  std::vector<float> kernel(static_cast<std::size_t>(radius) * 2U + 1U);
  double sum = 0.0;
  for (std::int32_t i = -radius; i <= radius; ++i) {
    const double value = std::exp(-0.5 * (i * i) / (sigma * sigma));
    kernel[static_cast<std::size_t>(i + radius)] = static_cast<float>(value);
    sum += value;
  }
  for (auto& value : kernel) {
    value = static_cast<float>(value / sum);
  }
  const std::int32_t lines = horizontal ? height : width;
  const std::int32_t length = horizontal ? width : height;
  const auto source = rgba;
  run_in_parallel(static_cast<std::size_t>(lines), [&](std::size_t line_index) {
    const auto line = static_cast<std::int32_t>(line_index);
    for (std::int32_t i = 0; i < length; ++i) {
      float acc[4]{0.0F, 0.0F, 0.0F, 0.0F};
      for (std::int32_t k = -radius; k <= radius; ++k) {
        std::int32_t j = i + k;
        if (j < 0 || j >= length) {
          if (!clamp_edges) {
            continue;
          }
          j = std::clamp(j, 0, length - 1);
        }
        const auto x = horizontal ? j : line;
        const auto y = horizontal ? line : j;
        const auto* px = source.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                          static_cast<std::size_t>(x)) * 4U;
        const float weight = kernel[static_cast<std::size_t>(k + radius)];
        for (int c = 0; c < 4; ++c) {
          acc[c] += px[c] * weight;
        }
      }
      const auto x = horizontal ? i : line;
      const auto y = horizontal ? line : i;
      auto* out = rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)) * 4U;
      for (int c = 0; c < 4; ++c) {
        out[c] = acc[c];
      }
    }
  });
}

// GEGL's gaussian-blur effect baked into the layer's pixels: blurred in premultiplied
// linear light, as GEGL does, mixed with the original by the effect's opacity. With
// clip-extent off the layer grows by the blur's reach.
// The sRGB curve both ways through tables (the blur runs on millions of pixels): decoding
// interpolates 4096 steps, encoding takes the nearest of 65536, well inside a 16-bit level.
[[nodiscard]] float decode_fast(float encoded) noexcept {
  static const auto table = [] {
    std::vector<float> values(4098);
    for (std::size_t i = 0; i < values.size(); ++i) {
      values[i] = static_cast<float>(srgb_decode(std::min(1.0, static_cast<double>(i) / 4096.0)));
    }
    return values;
  }();
  const float position = std::clamp(encoded, 0.0F, 1.0F) * 4096.0F;
  const auto index = static_cast<std::size_t>(position);
  const float fraction = position - static_cast<float>(index);
  return table[index] + (table[index + 1] - table[index]) * fraction;
}

[[nodiscard]] float encode_fast(float linear) noexcept {
  static const auto table = [] {
    std::vector<float> values(65537);
    for (std::size_t i = 0; i < values.size(); ++i) {
      values[i] = static_cast<float>(srgb_encode(static_cast<double>(i) / 65536.0));
    }
    return values;
  }();
  return table[static_cast<std::size_t>(std::lround(std::clamp(linear, 0.0F, 1.0F) * 65536.0F))];
}

void bake_gaussian_blur(Layer& layer, const EffectRecord& effect, const PixelBuffer* mask) {
  const double sx = std::clamp(float_argument(effect, "std-dev-x").value_or(1.5), 0.0, 1500.0);
  const double sy = std::clamp(float_argument(effect, "std-dev-y").value_or(1.5), 0.0, 1500.0);
  const bool clip_extent = enum_argument(effect, "clip-extent").value_or(1) != 0;
  const bool clamp_edges = enum_argument(effect, "abyss-policy").value_or(1) == 1;
  const auto& source = std::as_const(layer).pixels();
  const auto format = source.format();
  const auto pad = clip_extent ? 0 : static_cast<std::int32_t>(std::ceil(3.0 * std::max(sx, sy)));
  const auto width = source.width() + pad * 2;
  const auto height = source.height() + pad * 2;
  if (width <= 0 || height <= 0) {
    return;
  }
  const bool linear_domain = format.bit_depth == BitDepth::Float32;
  std::vector<float> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0.0F);
  run_in_parallel(static_cast<std::size_t>(source.height()), [&](std::size_t row) {
    const auto y = static_cast<std::int32_t>(row);
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto value = load_pixel(format, source.pixel(x, y));
      const float alpha = std::clamp(value[3] / 255.0F, 0.0F, 1.0F);
      auto* out = rgba.data() + (static_cast<std::size_t>(y + pad) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x + pad)) * 4U;
      for (int c = 0; c < 3; ++c) {
        const float unit = value[static_cast<std::size_t>(c)] / 255.0F;
        out[c] = (linear_domain ? unit : decode_fast(unit)) * alpha;
      }
      out[3] = alpha;
    }
  });
  const auto original = rgba;
  gaussian_pass(rgba, width, height, sx, true, clamp_edges && clip_extent);
  gaussian_pass(rgba, width, height, sy, false, clamp_edges && clip_extent);
  const float opacity = std::clamp(effect.opacity, 0.0F, 1.0F);
  PixelFormat out_format = format;
  out_format.channels = 4;
  PixelBuffer out(width, height, out_format);
  auto out_bytes = out.data();
  const auto pixel_bytes = bytes_per_pixel(out_format);
  run_in_parallel(static_cast<std::size_t>(height), [&](std::size_t row) {
    const auto y = static_cast<std::int32_t>(row);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
      float amount = opacity;
      if (mask != nullptr) {
        // The selection the effect was made with, in the layer's own pixels.
        const auto mx = x - pad;
        const auto my = y - pad;
        amount *= mx >= 0 && my >= 0 && mx < mask->width() && my < mask->height() ? coverage_at(*mask, mx, my) : 0.0F;
      }
      float mixed[4];
      for (int c = 0; c < 4; ++c) {
        mixed[c] = original[index + static_cast<std::size_t>(c)] +
                   (rgba[index + static_cast<std::size_t>(c)] - original[index + static_cast<std::size_t>(c)]) * amount;
      }
      std::array<float, 4> stored{0.0F, 0.0F, 0.0F, 0.0F};
      const float alpha = std::clamp(mixed[3], 0.0F, 1.0F);
      if (alpha > 0.0F) {
        for (int c = 0; c < 3; ++c) {
          const float linear = mixed[c] / alpha;
          stored[static_cast<std::size_t>(c)] =
              (linear_domain ? linear : encode_fast(linear)) * 255.0F;
        }
      }
      stored[3] = alpha * 255.0F;
      store_pixel(out_format, out_bytes.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                                  static_cast<std::size_t>(x)) * pixel_bytes,
                  stored);
    }
  });
  const auto bounds = layer.bounds();
  layer.set_pixels(std::move(out));
  layer.set_bounds(Rect{bounds.x - pad, bounds.y - pad, width, height});
}

// --- GIMP's color filters, baked ----------------------------------------------------------
//
// Point operations on one pixel: `rgb` holds display-encoded values in 0..1 (and may leave
// that range; the store clamps). Their formulas were fitted on GIMP 3.2's renders of the
// 4096-color sweep (scripts/dev/xcf/fit_effects.py; docs/xcf.md, "Layer effects").
using PointOperation = std::function<void(float* rgb)>;

constexpr float kLumaRed = 0.2225045F;  // the D50 sRGB luminance row GIMP's luma and luminance use
constexpr float kLumaGreen = 0.7168786F;
constexpr float kLumaBlue = 0.0606169F;

[[nodiscard]] float decode_unit(float encoded) noexcept {
  return static_cast<float>(srgb_decode(std::clamp(encoded, 0.0F, 1.0F)));
}

[[nodiscard]] float encode_unit(float linear) noexcept {
  return static_cast<float>(srgb_encode(std::clamp(linear, 0.0F, 1.0F)));
}

// The text of a GimpConfig argument (type 6: u32 length, then the serialized forms).
[[nodiscard]] std::string config_text(const EffectRecord& effect) {
  const auto* argument = find_argument(effect, "config");
  if (argument == nullptr || argument->type != 6 || argument->data.size() < 4) {
    return {};
  }
  const std::size_t length = (static_cast<std::size_t>(argument->data[0]) << 24U) |
                             (static_cast<std::size_t>(argument->data[1]) << 16U) |
                             (static_cast<std::size_t>(argument->data[2]) << 8U) | argument->data[3];
  const auto available = std::min(length, argument->data.size() - 4);
  return std::string(reinterpret_cast<const char*>(argument->data.data()) + 4, available);
}

[[nodiscard]] double config_number(const std::vector<std::pair<std::string, std::string>>& forms, std::size_t from,
                                   std::string_view key, double fallback) {
  for (std::size_t i = from; i < forms.size(); ++i) {
    if (i > from && (forms[i].first == "range" || forms[i].first == "channel")) {
      break;
    }
    if (forms[i].first == key) {
      char* end = nullptr;
      const double value = std::strtod(forms[i].second.c_str(), &end);
      return end != forms[i].second.c_str() && std::isfinite(value) ? value : fallback;
    }
  }
  return fallback;
}

void rgb_to_hsl(const float* rgb, float& h, float& s, float& l) {
  const float maximum = std::max({rgb[0], rgb[1], rgb[2]});
  const float minimum = std::min({rgb[0], rgb[1], rgb[2]});
  l = (maximum + minimum) / 2.0F;
  h = 0.0F;
  s = 0.0F;
  if (maximum == minimum) {
    return;
  }
  const float range = maximum - minimum;
  s = l <= 0.5F ? range / (maximum + minimum) : range / (2.0F - maximum - minimum);
  const float rc = (maximum - rgb[0]) / range;
  const float gc = (maximum - rgb[1]) / range;
  const float bc = (maximum - rgb[2]) / range;
  if (rgb[0] == maximum) {
    h = bc - gc;
  } else if (rgb[1] == maximum) {
    h = 2.0F + rc - bc;
  } else {
    h = 4.0F + gc - rc;
  }
  h /= 6.0F;
  h -= std::floor(h);
}

[[nodiscard]] float hsl_component(float m1, float m2, float hue) noexcept {
  hue -= std::floor(hue);
  if (hue < 1.0F / 6.0F) return m1 + (m2 - m1) * hue * 6.0F;
  if (hue < 0.5F) return m2;
  if (hue < 2.0F / 3.0F) return m1 + (m2 - m1) * (2.0F / 3.0F - hue) * 6.0F;
  return m1;
}

void hsl_to_rgb(float h, float s, float l, float* rgb) {
  if (s <= 0.0F) {
    rgb[0] = rgb[1] = rgb[2] = l;
    return;
  }
  const float m2 = l <= 0.5F ? l * (1.0F + s) : l + s - l * s;
  const float m1 = 2.0F * l - m2;
  rgb[0] = hsl_component(m1, m2, h + 1.0F / 3.0F);
  rgb[1] = hsl_component(m1, m2, h);
  rgb[2] = hsl_component(m1, m2, h - 1.0F / 3.0F);
}

// One levels section (GIMP's value or color channel), in the space the caller chose.
[[nodiscard]] float apply_levels(float value, double low_in, double high_in, double gamma, double low_out,
                                 double high_out, bool clamp_in, bool clamp_out) {
  const double span = high_in - low_in;
  double v = std::abs(span) > 1e-9 ? (value - low_in) / span : (value >= high_in ? 1.0 : 0.0);
  if (clamp_in) {
    v = std::clamp(v, 0.0, 1.0);
  }
  // Values below the input black stay linear: a power has no real result there.
  if (v > 0.0 && gamma > 0.0) {
    v = std::pow(v, 1.0 / gamma);
  }
  v = low_out + (high_out - low_out) * v;
  if (clamp_out) {
    v = std::clamp(v, 0.0, 1.0);
  }
  return static_cast<float>(v);
}

// A filter setting: its own argument, or the first form of that name in the filter's
// GimpConfig argument (GIMP stores some filters' settings only there).
[[nodiscard]] double effect_number(const EffectRecord& effect, std::string_view name, double fallback) {
  if (const auto direct = float_argument(effect, name)) {
    return *direct;
  }
  const auto forms = detail::parse_config_forms(config_text(effect));
  return config_number(forms, 0, name, fallback);
}

// The point operation for a GIMP filter, or nothing when Patchy cannot reproduce it.
[[nodiscard]] std::optional<PointOperation> point_operation(const EffectRecord& effect) {
  const auto& op = effect.operation;
  if (op == "gegl:invert-gamma" || op == "gimp:invert") {
    return PointOperation([](float* rgb) {
      for (int c = 0; c < 3; ++c) rgb[c] = 1.0F - rgb[c];
    });
  }
  if (op == "gegl:invert-linear") {
    return PointOperation([](float* rgb) {
      for (int c = 0; c < 3; ++c) rgb[c] = encode_unit(1.0F - decode_unit(rgb[c]));
    });
  }
  if (op == "gimp:desaturate") {
    const auto mode = enum_argument(effect, "mode").value_or(3);
    if (mode > 4) {
      return std::nullopt;
    }
    return PointOperation([mode](float* rgb) {
      float gray = 0.0F;
      switch (mode) {
        case 0:  // lightness (HSL)
          gray = (std::max({rgb[0], rgb[1], rgb[2]}) + std::min({rgb[0], rgb[1], rgb[2]})) / 2.0F;
          break;
        case 1:  // luma, on display values
          gray = kLumaRed * rgb[0] + kLumaGreen * rgb[1] + kLumaBlue * rgb[2];
          break;
        case 2:  // average
          gray = (rgb[0] + rgb[1] + rgb[2]) / 3.0F;
          break;
        case 3:  // luminance, in linear light
          gray = encode_unit(kLumaRed * decode_unit(rgb[0]) + kLumaGreen * decode_unit(rgb[1]) +
                             kLumaBlue * decode_unit(rgb[2]));
          break;
        default:  // value
          gray = std::max({rgb[0], rgb[1], rgb[2]});
          break;
      }
      rgb[0] = rgb[1] = rgb[2] = gray;
    });
  }
  if (op == "gimp:threshold") {
    if (enum_argument(effect, "channel").value_or(0) != 0) {
      return std::nullopt;  // only the value channel was calibrated
    }
    const auto low = static_cast<float>(float_argument(effect, "low").value_or(0.5));
    const auto high = static_cast<float>(float_argument(effect, "high").value_or(1.0));
    return PointOperation([low, high](float* rgb) {
      const float value = std::max({rgb[0], rgb[1], rgb[2]});
      const float out = value >= low && value <= high ? 1.0F : 0.0F;
      rgb[0] = rgb[1] = rgb[2] = out;
    });
  }
  if (op == "gimp:posterize") {
    const auto levels = std::clamp(float_argument(effect, "levels").value_or(3.0), 2.0, 256.0);
    const auto steps = static_cast<float>(levels - 1.0);
    return PointOperation([steps](float* rgb) {
      for (int c = 0; c < 3; ++c) rgb[c] = std::round(std::clamp(rgb[c], 0.0F, 1.0F) * steps) / steps;
    });
  }
  if (op == "gimp:brightness-contrast") {
    const double half = std::clamp(effect_number(effect, "brightness", 0.0), -1.0, 1.0) / 2.0;
    const double slant = std::tan((std::clamp(effect_number(effect, "contrast", 0.0), -1.0, 1.0) + 1.0) *
                                  3.14159265358979323846 / 4.0);
    return PointOperation([half, slant](float* rgb) {
      for (int c = 0; c < 3; ++c) {
        double v = decode_unit(rgb[c]);
        v = half >= 0.0 ? half + (1.0 - half) * v : v * (1.0 + half);
        v = (v - 0.5) * slant + 0.5;
        rgb[c] = encode_unit(static_cast<float>(v));
      }
    });
  }
  if (op == "gimp:hue-saturation") {
    const auto forms = detail::parse_config_forms(config_text(effect));
    double hue = 0.0;
    double saturation = 0.0;
    double lightness = 0.0;
    for (std::size_t i = 0; i < forms.size(); ++i) {
      if (forms[i].first != "range") {
        continue;
      }
      const double h = config_number(forms, i, "hue", 0.0);
      const double sat = config_number(forms, i, "saturation", 0.0);
      const double l = config_number(forms, i, "lightness", 0.0);
      if (forms[i].second == "all") {
        hue = h;
        saturation = sat;
        lightness = l;
      } else if (std::abs(h) > 1e-9 || std::abs(sat) > 1e-9 || std::abs(l) > 1e-9) {
        return std::nullopt;  // per-hue ranges were not calibrated
      }
    }
    return PointOperation([hue, saturation, lightness](float* rgb) {
      float h = 0.0F;
      float s = 0.0F;
      float l = 0.0F;
      float clamped[3] = {std::clamp(rgb[0], 0.0F, 1.0F), std::clamp(rgb[1], 0.0F, 1.0F), std::clamp(rgb[2], 0.0F, 1.0F)};
      rgb_to_hsl(clamped, h, s, l);
      h = static_cast<float>(h + hue / 2.0);
      h -= std::floor(h);
      s = static_cast<float>(std::clamp(s * (1.0 + saturation), 0.0, 1.0));
      l = static_cast<float>(lightness < 0.0 ? l * (1.0 + lightness) : l + (1.0 - l) * lightness);
      hsl_to_rgb(h, s, l, rgb);
    });
  }
  if (op == "gimp:colorize") {
    const auto hue = static_cast<float>(float_argument(effect, "hue").value_or(0.5));
    const auto saturation = static_cast<float>(float_argument(effect, "saturation").value_or(0.5));
    const auto lightness = static_cast<float>(float_argument(effect, "lightness").value_or(0.0));
    return PointOperation([hue, saturation, lightness](float* rgb) {
      float l = kLumaRed * decode_unit(rgb[0]) + kLumaGreen * decode_unit(rgb[1]) + kLumaBlue * decode_unit(rgb[2]);
      l = lightness > 0.0F ? l * (1.0F - lightness) + lightness : l * (1.0F + lightness);
      float linear[3];
      hsl_to_rgb(hue, saturation, l, linear);
      for (int c = 0; c < 3; ++c) rgb[c] = encode_unit(linear[c]);
    });
  }
  if (op == "gimp:levels") {
    const auto forms = detail::parse_config_forms(config_text(effect));
    if (forms.empty()) {
      return std::nullopt;
    }
    bool linear = true;
    bool clamp_in = false;
    bool clamp_out = false;
    struct Section {
      double low_in{0.0};
      double high_in{1.0};
      double gamma{1.0};
      double low_out{0.0};
      double high_out{1.0};
    };
    std::map<std::string, Section> sections;
    for (std::size_t i = 0; i < forms.size(); ++i) {
      if (forms[i].first == "trc") {
        linear = forms[i].second == "linear";
      } else if (forms[i].first == "clamp-input") {
        clamp_in = forms[i].second == "yes";
      } else if (forms[i].first == "clamp-output") {
        clamp_out = forms[i].second == "yes";
      } else if (forms[i].first == "channel") {
        sections[forms[i].second] = Section{config_number(forms, i, "low-input", 0.0),
                                            config_number(forms, i, "high-input", 1.0),
                                            config_number(forms, i, "gamma", 1.0),
                                            config_number(forms, i, "low-output", 0.0),
                                            config_number(forms, i, "high-output", 1.0)};
      }
    }
    return PointOperation([sections, linear, clamp_in, clamp_out](float* rgb) {
      for (int c = 0; c < 3; ++c) {
        float v = linear ? decode_unit(rgb[c]) : rgb[c];
        static constexpr const char* kChannels[3] = {"red", "green", "blue"};
        for (const char* name : {"value", kChannels[c]}) {
          if (const auto found = sections.find(name); found != sections.end()) {
            const auto& section = found->second;
            v = apply_levels(v, section.low_in, section.high_in, section.gamma, section.low_out, section.high_out,
                             clamp_in, clamp_out);
          }
        }
        rgb[c] = linear ? encode_unit(v) : v;
      }
    });
  }
  if (op == "gegl:exposure") {
    const auto black = float_argument(effect, "black-level").value_or(0.0);
    const auto gain = std::pow(2.0, float_argument(effect, "exposure").value_or(0.0)) /
                      std::max(1e-6, 1.0 - black);
    return PointOperation([black, gain](float* rgb) {
      for (int c = 0; c < 3; ++c) rgb[c] = encode_unit(static_cast<float>((decode_unit(rgb[c]) - black) * gain));
    });
  }
  if (op == "gegl:mono-mixer") {
    double red = float_argument(effect, "red").value_or(0.333);
    double green = float_argument(effect, "green").value_or(0.333);
    double blue = float_argument(effect, "blue").value_or(0.333);
    if (enum_argument(effect, "preserve-luminosity").value_or(0) != 0) {
      const double sum = red + green + blue;
      if (std::abs(sum) > 1e-9) {
        red /= sum;
        green /= sum;
        blue /= sum;
      }
    }
    return PointOperation([red, green, blue](float* rgb) {
      const auto gray = encode_unit(static_cast<float>(red * decode_unit(rgb[0]) + green * decode_unit(rgb[1]) +
                                                       blue * decode_unit(rgb[2])));
      rgb[0] = rgb[1] = rgb[2] = gray;
    });
  }
  return std::nullopt;
}

// Applies a point operation to the layer's pixels, mixed with the original in linear light
// by the effect's opacity and selection mask, as GIMP mixes a filter.
void bake_point_operation(Layer& layer, const EffectRecord& effect, const PixelBuffer* mask,
                          const PointOperation& operation) {
  auto& pixels = layer.pixels();
  const auto format = pixels.format();
  const bool linear_domain = format.bit_depth == BitDepth::Float32;
  const auto pixel_bytes = bytes_per_pixel(format);
  const float opacity = std::clamp(effect.opacity, 0.0F, 1.0F);
  auto bytes = pixels.data();
  const auto stride = pixels.stride_bytes();
  run_in_parallel(static_cast<std::size_t>(pixels.height()), [&](std::size_t row) {
    const auto y = static_cast<std::int32_t>(row);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      float amount = opacity;
      if (mask != nullptr) {
        amount *= x < mask->width() && y < mask->height() ? coverage_at(*mask, x, y) : 0.0F;
      }
      if (amount <= 0.0F) {
        continue;
      }
      auto* px = bytes.data() + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * pixel_bytes;
      auto value = load_pixel(format, px);
      float rgb[3];
      float original[3];
      for (int c = 0; c < 3; ++c) {
        const float unit = value[static_cast<std::size_t>(c)] / 255.0F;
        original[c] = linear_domain ? unit : decode_fast(unit);
        rgb[c] = linear_domain ? encode_unit(unit) : unit;
      }
      operation(rgb);
      for (int c = 0; c < 3; ++c) {
        const float filtered = decode_fast(std::clamp(rgb[c], 0.0F, 1.0F));
        const float mixed = original[c] + (filtered - original[c]) * amount;
        value[static_cast<std::size_t>(c)] = (linear_domain ? mixed : encode_fast(mixed)) * 255.0F;
      }
      store_pixel(format, px, value);
    }
  });
}

[[nodiscard]] std::optional<PixelBuffer> effect_mask(Build& build, const EffectRecord& effect, const Layer& layer) {
  if (effect.region != 0 || effect.mask == 0) {
    return std::nullopt;
  }
  try {
    auto channel = read_channel(build.reader, effect.mask);
    const auto& source = std::as_const(layer).pixels();
    if (channel.width == static_cast<std::uint32_t>(source.width()) &&
        channel.height == static_cast<std::uint32_t>(source.height())) {
      auto mask = decode_coverage_drawable(build.reader, channel, build.image_layout.precision.component,
                                           BitDepth::UInt8, build.compression, build.stats);
      // An empty mask is "no selection" when the effect was made: it applies everywhere.
      const auto bytes = std::as_const(mask).data();
      if (std::any_of(bytes.begin(), bytes.end(), [](std::uint8_t value) { return value != 0; })) {
        return mask;
      }
      return std::nullopt;
    }
    trace("an effect mask's size differs from its layer; the effect applies everywhere");
  } catch (const ImportError& error) {
    trace(std::string("effect mask: ") + error.what());
  }
  return std::nullopt;
}

void apply_effects(Build& build, const LayerRecord& record, Layer& layer) {
  // GIMP lists a layer's effects top first, like its filter stack; the bottom one runs first.
  for (auto it = record.effects.rbegin(); it != record.effects.rend(); ++it) {
    const auto& effect = *it;
    if (!effect.visible) {
      continue;
    }
    if (effect.operation == "gegl:dropshadow") {
      if (const auto shadow = drop_shadow_from_effect(effect, build.depth == BitDepth::Float32)) {
        layer.layer_style().drop_shadows.push_back(*shadow);
        continue;
      }
    }
    if (effect.operation == "gegl:gaussian-blur" && layer.kind() == LayerKind::Pixel) {
      if (layer_is_text(layer)) {
        clear_layer_text_metadata(layer);
        build.notices.insert(Notice::TextModified);
      }
      const auto mask = effect_mask(build, effect, layer);
      bake_gaussian_blur(layer, effect, mask.has_value() ? &*mask : nullptr);
      build.notices.insert(Notice::EffectsBaked);
      continue;
    }
    if (layer.kind() == LayerKind::Pixel) {
      if (const auto operation = point_operation(effect)) {
        if (layer_is_text(layer)) {
          clear_layer_text_metadata(layer);
          build.notices.insert(Notice::TextModified);
        }
        const auto mask = effect_mask(build, effect, layer);
        bake_point_operation(layer, effect, mask.has_value() ? &*mask : nullptr, *operation);
        build.notices.insert(Notice::EffectsBaked);
        continue;
      }
    }
    trace("effect " + effect.operation + " on layer '" + record.name + "' is not applied");
    build.notices.insert(Notice::EffectsNotApplied);
  }
}

[[nodiscard]] Layer build_layer(Build& build, const LayerRecord& record) {
  const auto map = map_mode(record.have_mode ? record.mode : 0, record.group, record.blend_space);
  if (map.fit == ModeFit::Approximate) {
    build.notices.insert(Notice::ModesApproximated);
  } else if (map.fit == ModeFit::Missing) {
    build.notices.insert(Notice::ModesMissing);
  }
  if (record.group) {
    Layer group(build.document.allocate_layer_id(), record.name, LayerKind::Group);
    build.composite[group.id()] = composite_kind(record);
    group.set_opacity(record.opacity);
    group.set_visible(record.visible);
    group.set_blend_mode(map.mode);
    set_layer_group_expanded(group, record.expanded);
    group.set_color_label(color_label_for_tag(record.color_tag));
    LayerLockFlags locks = kLayerLockNone;
    if (record.lock_alpha) locks |= kLayerLockTransparentPixels;
    if (record.lock_content) locks |= kLayerLockImagePixels;
    if (record.lock_position) locks |= kLayerLockPosition;
    group.set_lock_flags(locks);
    if (record.mask.has_value()) {
      const Rect bounds{record.x, record.y, static_cast<std::int32_t>(record.width),
                        static_cast<std::int32_t>(record.height)};
      if (auto mask = build_mask(build, *record.mask, bounds, !record.have_apply_mask || record.apply_mask)) {
        group.set_mask(std::move(*mask));
      }
    }
    apply_effects(build, record, group);
    if (record.active && !build.active.has_value()) {
      build.active = group.id();
    }
    return group;
  }
  account(build, record.width, record.height);
  PixelLayout layout = build.image_layout;
  switch (record.type) {
    case 0:
    case 1:
      layout.base = BaseType::Rgb;
      break;
    case 2:
    case 3:
      layout.base = BaseType::Gray;
      break;
    case 4:
    case 5:
      layout.base = BaseType::Indexed;
      break;
    default:
      throw ImportError("a layer has an unknown type " + std::to_string(record.type));
  }
  layout.alpha = record.type % 2U == 1U;
  ConvertContext context{layout, build.depth, &build.colormap};
  auto pixels = decode_color_drawable(build.reader, record.hierarchy, record.width, record.height, context,
                                      build.compression, true, build.stats);
  Layer layer(build.document.allocate_layer_id(), record.name, std::move(pixels));
  build.composite[layer.id()] = composite_kind(record);
  if (record.visible && record.have_mode && (record.mode == 58 || record.mode == 60)) {
    build.erasers.insert(layer.id());
  }
  layer.set_bounds(Rect{record.x, record.y, static_cast<std::int32_t>(record.width),
                        static_cast<std::int32_t>(record.height)});
  layer.set_opacity(record.opacity);
  layer.set_visible(record.visible);
  layer.set_blend_mode(map.mode);
  layer.set_color_label(color_label_for_tag(record.color_tag));
  LayerLockFlags locks = kLayerLockNone;
  if (record.lock_alpha) locks |= kLayerLockTransparentPixels;
  if (record.lock_content) locks |= kLayerLockImagePixels;
  if (record.lock_position) locks |= kLayerLockPosition;
  layer.set_lock_flags(locks);
  if (record.mask.has_value()) {
    if (auto mask = build_mask(build, *record.mask, layer.bounds(), !record.have_apply_mask || record.apply_mask)) {
      layer.set_mask(std::move(*mask));
    }
  }
  if (const auto* text = find_parasite(record.parasites, "gimp-text-layer"); text != nullptr) {
    if ((record.text_flags & 2U) != 0) {
      build.notices.insert(Notice::TextModified);
    } else {
      try {
        const std::string_view source(reinterpret_cast<const char*>(text->data.data()), text->data.size());
        const auto parsed = detail::parse_text_parasite(source);
        detail::TextLayerContext text_context{build.ppi, static_cast<std::int32_t>(record.width),
                                              static_cast<std::int32_t>(record.height)};
        if (!detail::apply_text_parasite(parsed, text_context, layer)) {
          build.notices.insert(Notice::TextUnreadable);
        }
      } catch (const std::exception& error) {
        trace(std::string("text parasite: ") + error.what());
        build.notices.insert(Notice::TextUnreadable);
      }
    }
  }
  if (record.vector_layer) {
    build.notices.insert(Notice::VectorLayers);
  }
  if (record.link_layer) {
    build.notices.insert(Notice::LinkLayers);
  }
  apply_effects(build, record, layer);
  if (record.active && !build.active.has_value()) {
    build.active = layer.id();
  }
  return layer;
}

// A layer and its children in file order (top first), for the tree built from item paths.
struct Node {
  std::size_t record{0};
  std::vector<Node> children;
};

[[nodiscard]] std::vector<Node> build_tree(const std::vector<LayerRecord>& records, bool& damaged) {
  std::vector<Node> roots;
  // The chain of open groups, as (record index) from the root down.
  std::vector<std::vector<Node>*> lists{&roots};
  std::vector<std::size_t> open_groups;
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    if (record.floating_target.has_value()) {
      continue;  // placed after the tree is built
    }
    auto depth = record.item_path.empty() ? std::size_t{0} : record.item_path.size() - 1;
    if (depth > open_groups.size()) {
      damaged = true;
      depth = open_groups.size();
    }
    while (open_groups.size() > depth) {
      open_groups.pop_back();
      lists.pop_back();
    }
    lists.back()->push_back(Node{i, {}});
    if (record.group && open_groups.size() < kMaxGroupDepth) {
      open_groups.push_back(i);
      lists.push_back(&lists.back()->back().children);
    }
  }
  return roots;
}

[[nodiscard]] std::vector<Layer> build_layers(Build& build, const std::vector<LayerRecord>& records,
                                              const std::vector<Node>& nodes,
                                              std::unordered_map<std::uint64_t, LayerId>& by_offset) {
  std::vector<Layer> layers;
  layers.reserve(nodes.size());
  // File order is top first; Patchy's lists are bottom first.
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    auto layer = build_layer(build, records[it->record]);
    by_offset[records[it->record].offset] = layer.id();
    if (layer.kind() == LayerKind::Group) {
      for (auto& child : build_layers(build, records, it->children, by_offset)) {
        layer.add_child(std::move(child));
      }
    }
    layers.push_back(std::move(layer));
  }
  return layers;
}

// Inserts `layer` right above the layer `target` in whichever list holds it.
[[nodiscard]] bool insert_above(std::vector<Layer>& layers, LayerId target, Layer& layer) {
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (layers[i].id() == target) {
      layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(layer));
      return true;
    }
    if (layers[i].kind() == LayerKind::Group && insert_above(layers[i].children(), target, layer)) {
      return true;
    }
  }
  return false;
}

// Path points to anchors: controls before the first anchor shape its incoming handle;
// after an anchor, the first control is its outgoing handle and a second one the next
// anchor's incoming handle (both the triplet and the anchor-first layouts read this way).
[[nodiscard]] std::optional<PathSubpath> subpath_from_stroke(const PathStroke& stroke) {
  PathSubpath subpath;
  subpath.closed = stroke.closed;
  std::vector<PathPoint> pending;
  std::optional<PathPoint> leading_in;
  bool any_anchor = false;
  for (const auto& point : stroke.points) {
    if (!point.anchor) {
      pending.push_back(point);
      continue;
    }
    PathAnchor anchor;
    anchor.anchor_x = point.x;
    anchor.anchor_y = point.y;
    anchor.in_x = anchor.out_x = point.x;
    anchor.in_y = anchor.out_y = point.y;
    if (!any_anchor) {
      if (!pending.empty()) {
        leading_in = pending.back();
      }
    } else {
      auto& previous = subpath.anchors.back();
      if (!pending.empty()) {
        previous.out_x = pending.front().x;
        previous.out_y = pending.front().y;
      }
      if (pending.size() >= 2) {
        anchor.in_x = pending.back().x;
        anchor.in_y = pending.back().y;
      }
    }
    pending.clear();
    subpath.anchors.push_back(anchor);
    any_anchor = true;
  }
  if (!any_anchor) {
    return std::nullopt;
  }
  auto& last = subpath.anchors.back();
  if (!pending.empty()) {
    last.out_x = pending.front().x;
    last.out_y = pending.front().y;
  }
  auto& first = subpath.anchors.front();
  if (leading_in.has_value()) {
    first.in_x = leading_in->x;
    first.in_y = leading_in->y;
  } else if (pending.size() >= 2) {
    first.in_x = pending.back().x;
    first.in_y = pending.back().y;
  }
  for (auto& anchor : subpath.anchors) {
    const double ix = anchor.in_x - anchor.anchor_x;
    const double iy = anchor.in_y - anchor.anchor_y;
    const double ox = anchor.out_x - anchor.anchor_x;
    const double oy = anchor.out_y - anchor.anchor_y;
    anchor.smooth = std::hypot(ix, iy) > 1e-6 && std::hypot(ox, oy) > 1e-6 && std::abs(ix * oy - iy * ox) < 1e-3;
  }
  return subpath;
}

void add_paths(Document& document, const std::vector<PathRecord>& paths) {
  for (const auto& record : paths) {
    VectorPath path;
    std::int32_t group = 0;
    for (const auto& stroke : record.strokes) {
      if (auto subpath = subpath_from_stroke(stroke)) {
        subpath->shape_group = group++;
        path.subpaths.push_back(std::move(*subpath));
      }
    }
    if (path.subpaths.empty()) {
      continue;
    }
    DocumentPath entry(document.allocate_path_id(), record.name.empty() ? std::string("Path") : record.name,
                       DocumentPathKind::Saved, std::move(path));
    entry.reset_dirty();
    document.add_path(std::move(entry));
  }
}

// --- clip to backdrop -------------------------------------------------------------------
//
// GIMP 2.10 and later composite most non-Normal modes "clip to backdrop": the layer shows
// only where the layers below it (in its group) have coverage, and the result keeps their
// alpha. Patchy, like Photoshop, composites every mode as a union. Over an opaque backdrop
// the two agree; where the backdrop is partly or fully transparent, the import gives the
// layer a mask made from the backdrop's coverage, so it shows where GIMP shows it
// (docs/xcf.md, "Composite modes").

struct AlphaPlane {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<float> alpha;

  AlphaPlane(std::int32_t w, std::int32_t h)
      : width(w), height(h), alpha(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0F) {}
  [[nodiscard]] float at(std::int32_t x, std::int32_t y) const noexcept {
    return x < 0 || y < 0 || x >= width || y >= height
               ? 0.0F
               : alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
  }
};

// The layer's own mask at a document position (1 where it has none or it is disabled).
[[nodiscard]] float mask_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return 1.0F;
  }
  const auto mx = x - mask->bounds.x;
  const auto my = y - mask->bounds.y;
  if (mx < 0 || my < 0 || mx >= mask->pixels.width() || my >= mask->pixels.height()) {
    return static_cast<float>(mask->default_color) / 255.0F;
  }
  return coverage_at(mask->pixels, mx, my);
}

// A pixel layer's coverage at a document position: its alpha, opacity and mask.
[[nodiscard]] float pixel_coverage_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& bounds = layer.bounds();
  const auto lx = x - bounds.x;
  const auto ly = y - bounds.y;
  const auto& pixels = std::as_const(layer).pixels();
  if (lx < 0 || ly < 0 || lx >= pixels.width() || ly >= pixels.height()) {
    return 0.0F;
  }
  return pixel_alpha_at(pixels, lx, ly) * layer.opacity() * mask_at(layer, x, y);
}

// Multiplies the layer's mask (making one when it has none) by the backdrop coverage over
// `area` (document pixels). Returns false when the layer has a disabled mask: GIMP ignores
// that mask, and Patchy has only the one.
[[nodiscard]] bool mask_by_backdrop(Layer& layer, const AlphaPlane& backdrop, Rect area, BitDepth depth) {
  if (layer.mask().has_value() && layer.mask()->disabled) {
    return false;
  }
  LayerMask mask;
  mask.bounds = area;
  mask.pixels = PixelBuffer(area.width, area.height, with_bit_depth(PixelFormat::gray8(), depth));
  mask.default_color = 0;
  std::vector<float> row(static_cast<std::size_t>(area.width));
  for (std::int32_t y = 0; y < area.height; ++y) {
    for (std::int32_t x = 0; x < area.width; ++x) {
      const auto dx = area.x + x;
      const auto dy = area.y + y;
      row[static_cast<std::size_t>(x)] = std::clamp(backdrop.at(dx, dy) * mask_at(layer, dx, dy), 0.0F, 1.0F) * kDeepScale;
    }
    store_coverage_row(mask.pixels, y, 0, area.width, row);
  }
  layer.set_mask(std::move(mask));
  return true;
}

struct ClipPass {
  Build& build;
  std::int32_t width;
  std::int32_t height;
  std::size_t masked{0};
  std::size_t clipped{0};
  std::size_t skipped{0};
  std::size_t other_modes{0};
};

// The layer's footprint on the canvas, and whether the backdrop is short of opaque anywhere
// the layer covers.
[[nodiscard]] bool backdrop_shows_through(const AlphaPlane& backdrop, Rect area,
                                          const std::function<float(std::int32_t, std::int32_t)>& coverage) {
  for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
    for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
      if (backdrop.at(x, y) < 0.999F && coverage(x, y) > 0.0F) {
        return true;
      }
    }
  }
  return false;
}

void union_into(AlphaPlane& backdrop, Rect area, const std::function<float(std::int32_t, std::int32_t)>& coverage) {
  for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
    for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
      auto& a = backdrop.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(backdrop.width) +
                               static_cast<std::size_t>(x)];
      const float c = std::clamp(coverage(x, y), 0.0F, 1.0F);
      a = a + c * (1.0F - a);
    }
  }
}

[[nodiscard]] Rect clamp_to_canvas(Rect rect, std::int32_t width, std::int32_t height) {
  const auto x0 = std::max(rect.x, 0);
  const auto y0 = std::max(rect.y, 0);
  const auto x1 = std::min<std::int64_t>(static_cast<std::int64_t>(rect.x) + rect.width, width);
  const auto y1 = std::min<std::int64_t>(static_cast<std::int64_t>(rect.y) + rect.height, height);
  if (x1 <= x0 || y1 <= y0) {
    return Rect{0, 0, 0, 0};
  }
  return Rect{x0, y0, static_cast<std::int32_t>(x1 - x0), static_cast<std::int32_t>(y1 - y0)};
}

// `stack_started` is per compositing stack: GIMP renders the bottommost visible layer of the
// image and of every isolated group as Normal whatever its mode (it has nothing to blend
// with); a pass-through group's layers belong to the stack around it.
void clip_pass(ClipPass& pass, std::vector<Layer>& layers, AlphaPlane& backdrop, bool& stack_started) {
  // While every visible layer so far in this list sits on the list's first (bottom) pixel
  // layer, a clip-to-backdrop layer is exactly a clipping mask on it: Photoshop's clipping
  // keeps the base's coverage, which is GIMP's backdrop there. Hidden layers in between are
  // clipped too, so the chain does not end on them.
  std::optional<LayerId> base;
  bool chain = false;
  std::vector<Layer*> hidden_since_base;
  for (auto& layer : layers) {
    if (!layer.visible()) {
      if (chain && layer.kind() != LayerKind::Group) {
        hidden_since_base.push_back(&layer);
      } else {
        chain = false;
      }
      continue;
    }
    const auto found = pass.build.composite.find(layer.id());
    const int kind = found == pass.build.composite.end() ? 1 : found->second;
    const bool forced_union = !stack_started;
    std::function<float(std::int32_t, std::int32_t)> coverage;
    std::optional<AlphaPlane> group_plane;
    Rect area{0, 0, pass.width, pass.height};
    if (layer.kind() == LayerKind::Group) {
      if (layer.blend_mode() == BlendMode::PassThrough) {
        chain = false;
        clip_pass(pass, layer.children(), backdrop, stack_started);
        continue;
      }
      group_plane.emplace(pass.width, pass.height);
      bool group_started = false;
      clip_pass(pass, layer.children(), *group_plane, group_started);
      const auto* plane = &*group_plane;
      const float opacity = layer.opacity();
      coverage = [plane, opacity, &layer](std::int32_t x, std::int32_t y) {
        return plane->at(x, y) * opacity * mask_at(layer, x, y);
      };
    } else {
      area = clamp_to_canvas(layer.bounds(), pass.width, pass.height);
      coverage = [&layer](std::int32_t x, std::int32_t y) { return pixel_coverage_at(layer, x, y); };
    }
    stack_started = true;
    if (forced_union) {
      chain = layer.kind() != LayerKind::Group && !layer.clipped();
      base = layer.id();
      hidden_since_base.clear();
      union_into(backdrop, area, coverage);
      continue;
    }
    if (kind != 1) {
      if (kind != 2) {
        ++pass.other_modes;  // clip to layer, intersection: treated as clip to backdrop
      }
      const bool shows_through = !area.empty() && backdrop_shows_through(backdrop, area, coverage);
      if (!shows_through) {
        // An opaque backdrop: the union Patchy draws is the same. The layer stays unclipped,
        // so layers above it cannot clip to the base any more.
        chain = false;
        continue;
      }
      if (chain && layer.kind() != LayerKind::Group) {
        for (auto* hidden : hidden_since_base) {
          hidden->set_clipped(true);
        }
        hidden_since_base.clear();
        layer.set_clipped(true);
        ++pass.clipped;
        continue;  // the backdrop's coverage stays what it was
      }
      chain = false;
      {
        const Rect mask_area = layer.kind() == LayerKind::Group ? Rect{0, 0, pass.width, pass.height} : layer.bounds();
        if (mask_by_backdrop(layer, backdrop, mask_area, pass.build.depth)) {
          ++pass.masked;
        } else {
          ++pass.skipped;
        }
      }
      continue;
    }
    chain = false;
    if (!area.empty()) {
      union_into(backdrop, area, coverage);
    }
  }
}

// --- Erase ------------------------------------------------------------------------------
//
// GIMP's Erase and Split modes take coverage away from what lies below the layer. Patchy
// has no such blend mode, so the import puts the layers below it (in its list) into a group
// whose mask is the inverse of the layer's coverage, and drops the layer (docs/xcf.md,
// "Blend modes"). Split equals Erase over an opaque backdrop and is treated the same.
// GIMP renders the bottom visible layer of a stack (the image or an isolated group) as
// Normal, so an eraser with nothing visible below it in such a stack stays a Normal layer.
void convert_erase_layers(Build& build, std::vector<Layer>& layers, bool stack_root) {
  for (auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      convert_erase_layers(build, layer.children(), layer.blend_mode() != BlendMode::PassThrough);
    }
  }
  const auto width = build.document.width();
  const auto height = build.document.height();
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (!build.erasers.contains(layers[i].id())) {
      continue;
    }
    const bool visible_below =
        std::any_of(layers.begin(), layers.begin() + static_cast<std::ptrdiff_t>(i),
                    [](const Layer& below) { return below.visible(); });
    if (!visible_below && stack_root) {
      layers[i].set_blend_mode(BlendMode::Normal);  // GIMP draws it as Normal
      continue;
    }
    build.notices.insert(Notice::EraseAsMask);
    if (i == 0) {
      if (build.active == layers[i].id()) {
        build.active.reset();
      }
      layers.erase(layers.begin());  // nothing below it to erase
      i = static_cast<std::size_t>(-1);
      continue;
    }
    const Layer eraser = std::move(layers[i]);
    LayerMask mask;
    mask.bounds = Rect{0, 0, width, height};
    mask.pixels = PixelBuffer(width, height, with_bit_depth(PixelFormat::gray8(), build.depth));
    mask.default_color = 255;
    std::vector<float> row(static_cast<std::size_t>(width));
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        row[static_cast<std::size_t>(x)] = (1.0F - std::clamp(pixel_coverage_at(eraser, x, y), 0.0F, 1.0F)) * kDeepScale;
      }
      store_coverage_row(mask.pixels, y, 0, width, row);
    }
    Layer group(build.document.allocate_layer_id(), eraser.name(), LayerKind::Group);
    group.set_blend_mode(BlendMode::Normal);
    for (std::size_t k = 0; k < i; ++k) {
      group.add_child(std::move(layers[k]));
    }
    group.set_mask(std::move(mask));
    set_layer_group_expanded(group, false);
    if (build.active == eraser.id()) {
      build.active = group.id();  // the eraser is gone; its group stands in for it
    }
    layers.erase(layers.begin(), layers.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    layers.insert(layers.begin(), std::move(group));
    i = 0;
  }
}

[[nodiscard]] int parse_version(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 14 || std::memcmp(bytes.data(), "gimp xcf ", 9) != 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not a GIMP image"));
  }
  const std::string tag(reinterpret_cast<const char*>(bytes.data()) + 9, 4);
  if (tag == "file") {
    return 0;
  }
  if (tag[0] != 'v' || !std::isdigit(static_cast<unsigned char>(tag[1])) ||
      !std::isdigit(static_cast<unsigned char>(tag[2])) || !std::isdigit(static_cast<unsigned char>(tag[3]))) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not a GIMP image"));
  }
  const int version = (tag[1] - '0') * 100 + (tag[2] - '0') * 10 + (tag[3] - '0');
  if (version >= 100) {
    // CinePaint's XCF dialect: another format under the same name.
    throw std::runtime_error(
        PATCHY_TRANSLATE_NOOP("QObject", "This XCF file was written by CinePaint, whose format Patchy cannot read"));
  }
  return version;
}

[[nodiscard]] bool modes_need_sixteen_bits(const std::vector<LayerRecord>& records) {
  return std::any_of(records.begin(), records.end(), [](const LayerRecord& record) {
    const auto map = map_mode(record.have_mode ? record.mode : 0, record.group, record.blend_space);
    return !blend_mode_supported_at_depth(map.mode, BitDepth::Float32);
  });
}

class PhaseTimer {
public:
  void mark(const char* phase) {
    const auto now = std::chrono::steady_clock::now();
    trace(std::string(phase) + ": " +
          std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_).count()) + " ms");
    last_ = now;
  }

private:
  std::chrono::steady_clock::time_point last_{std::chrono::steady_clock::now()};
};

[[nodiscard]] Document read_document(std::span<const std::uint8_t> bytes, std::vector<std::string>& notices) {
  PhaseTimer timer;
  const int version = parse_version(bytes);
  const Reader reader(bytes, version);
  Cursor cursor(reader, 14, reader.size());
  const auto width = cursor.u32();
  const auto height = cursor.u32();
  const auto base = cursor.u32();
  if (width == 0 || height == 0) {
    throw ImportError("the canvas is empty");
  }
  if (width > kMaxSide || height > kMaxSide) {
    throw SizeLimitError("the canvas is " + std::to_string(width) + " x " + std::to_string(height));
  }
  if (base > 2) {
    throw ImportError("unknown base type " + std::to_string(base));
  }
  Precision precision{Component::U8, Transfer::NonLinear};
  if (version >= 4) {
    const auto value = cursor.u32();
    const auto parsed = precision_from_header(version, value);
    if (!parsed.has_value()) {
      throw ImportError("unknown precision " + std::to_string(value));
    }
    precision = *parsed;
  }
  if (base == 2) {
    precision = Precision{Component::U8, Transfer::NonLinear};
  }

  // Image properties.
  std::vector<RgbColor> colormap;
  Compression compression = Compression::None;
  std::vector<DocumentGuide> guides;
  double xres = 72.0;
  double yres = 72.0;
  std::uint32_t unit = 0;
  std::vector<Parasite> parasites;
  std::vector<PathRecord> paths;
  bool damaged_structure = false;
  for (const auto& property : read_properties(reader, cursor)) {
    try {
      switch (property.type) {
        case kPropColormap: {
          const auto count = std::min<std::uint32_t>(reader.u32(property.offset), 256);
          for (std::uint32_t i = 0; i < count; ++i) {
            const auto at = property.offset + 4 + 3ULL * i;
            colormap.push_back(RgbColor{reader.u8(at), reader.u8(at + 1), reader.u8(at + 2)});
          }
          break;
        }
        case kPropCompression: {
          const auto value = property.length >= 1 ? reader.u8(property.offset) : 0;
          if (value > 2) {
            throw ImportError("unknown tile compression " + std::to_string(value));
          }
          compression = static_cast<Compression>(value);
          break;
        }
        case kPropGuides:
          for (std::uint64_t at = 0; at + 5 <= property.length; at += 5) {
            const auto position = reader.i32(property.offset + at);
            const auto orientation = reader.u8(property.offset + at + 4);
            if (version < 15 && position < 0) {
              continue;
            }
            if (orientation != 1 && orientation != 2) {
              continue;
            }
            const auto clamped = std::clamp<std::int64_t>(position, -kMaxGuideOffsetPixels, kMaxGuideOffsetPixels);
            guides.push_back(DocumentGuide{orientation == 1 ? GuideOrientation::Horizontal : GuideOrientation::Vertical,
                                           static_cast<std::int32_t>(clamped * 32)});
          }
          break;
        case kPropResolution:
          if (property.length >= 8) {
            xres = reader.f32(property.offset);
            yres = reader.f32(property.offset + 4);
          }
          break;
        case kPropUnit:
          unit = payload_u32(reader, property, 0);
          break;
        case kPropParasites:
          parasites = read_parasites(reader, property);
          break;
        case kPropPaths:
          read_prop_paths(reader, property, paths);
          break;
        case kPropVectors:
          read_prop_vectors(reader, property, paths);
          break;
        default:
          break;
      }
    } catch (const SizeLimitError&) {
      throw;
    } catch (const ImportError& error) {
      if (property.type == kPropCompression) {
        throw;
      }
      trace(std::string("image property ") + std::to_string(property.type) + ": " + error.what());
      damaged_structure = true;
    }
  }

  // Pointer lists.
  std::vector<std::uint64_t> layer_offsets;
  while (true) {
    const auto pointer = cursor.pointer();
    if (pointer == 0) {
      break;
    }
    if (layer_offsets.size() >= kMaxItems) {
      throw SizeLimitError("too many layers");
    }
    layer_offsets.push_back(pointer);
  }
  std::vector<std::uint64_t> channel_offsets;
  while (true) {
    const auto pointer = cursor.pointer();
    if (pointer == 0) {
      break;
    }
    if (channel_offsets.size() >= kMaxItems) {
      throw SizeLimitError("too many channels");
    }
    channel_offsets.push_back(pointer);
  }
  std::vector<std::uint64_t> path_offsets;
  if (version >= 18) {
    try {
      while (true) {
        const auto pointer = cursor.pointer();
        if (pointer == 0) {
          break;
        }
        if (path_offsets.size() >= kMaxItems) {
          break;
        }
        path_offsets.push_back(pointer);
      }
    } catch (const ImportError& error) {
      trace(std::string("path list: ") + error.what());
    }
  }

  std::vector<LayerRecord> records;
  records.reserve(layer_offsets.size());
  for (const auto offset : layer_offsets) {
    try {
      records.push_back(read_layer(reader, offset));
    } catch (const ImportError& error) {
      trace(std::string("layer at ") + std::to_string(offset) + ": " + error.what());
      damaged_structure = true;
    }
  }
  if (records.empty() && !layer_offsets.empty()) {
    throw ImportError("no layer could be read");
  }
  std::vector<ChannelRecord> channels;
  for (const auto offset : channel_offsets) {
    try {
      channels.push_back(read_channel(reader, offset));
    } catch (const ImportError& error) {
      trace(std::string("channel at ") + std::to_string(offset) + ": " + error.what());
      damaged_structure = true;
    }
  }
  for (const auto offset : path_offsets) {
    try {
      paths.push_back(read_path_structure(reader, offset));
    } catch (const ImportError& error) {
      trace(std::string("path at ") + std::to_string(offset) + ": " + error.what());
      damaged_structure = true;
    }
  }

  // The document's depth (docs/xcf.md, "Precision").
  std::set<Notice> depth_notices;
  BitDepth depth = BitDepth::UInt8;
  if (precision.component == Component::U8) {
    depth = precision.transfer == Transfer::Linear ? BitDepth::UInt16 : BitDepth::UInt8;
  } else if (precision.component == Component::U16 || precision.component == Component::U32) {
    depth = BitDepth::UInt16;
  } else if (modes_need_sixteen_bits(records)) {
    depth = BitDepth::UInt16;
    depth_notices.insert(Notice::DepthNarrowedForModes);
  } else {
    depth = BitDepth::Float32;
  }
  if (depth != BitDepth::UInt8 && !deep_editing_enabled()) {
    depth = BitDepth::UInt8;
    depth_notices.clear();
    if (!(precision.component == Component::U8 && precision.transfer == Transfer::Linear)) {
      depth_notices.insert(Notice::DeepDisabled);
    }
  }
  if (depth != BitDepth::UInt8 && !depth_supported_on_platform(depth)) {
    depth = BitDepth::UInt16;
  }

  Document document(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                    with_bit_depth(PixelFormat::rgba8(), depth));
  document.color_state().bit_depth = depth;
  const auto valid_ppi = [](double value) { return std::isfinite(value) && value >= 1.0 && value <= 1e6; };
  auto& print = document.print_settings();
  print.horizontal_ppi = valid_ppi(xres) ? xres : 72.0;
  print.vertical_ppi = valid_ppi(yres) ? yres : print.horizontal_ppi;
  // GIMP's units: 1 inch, 2 millimeters, 3 points, 4 picas.
  if (unit == 2) {
    print.horizontal_resolution_display_unit = 2;
    print.vertical_resolution_display_unit = 2;
    print.width_display_unit = 2;
    print.height_display_unit = 2;
  } else if (unit == 3 || unit == 4) {
    print.width_display_unit = static_cast<std::uint16_t>(unit);
    print.height_display_unit = static_cast<std::uint16_t>(unit);
  }
  document.guides() = std::move(guides);

  Build build{reader, PixelLayout{static_cast<BaseType>(base), true, precision}, depth, compression,
              std::move(colormap), print.horizontal_ppi, document, {}, {}, 0, std::nullopt, {}, {}};
  build.notices.insert(depth_notices.begin(), depth_notices.end());
  if (damaged_structure) {
    build.notices.insert(Notice::DamagedStructure);
  }

  bool tree_damaged = false;
  const auto tree = build_tree(records, tree_damaged);
  if (tree_damaged) {
    build.notices.insert(Notice::DamagedStructure);
  }
  timer.mark("records");
  std::unordered_map<std::uint64_t, LayerId> by_offset;
  for (auto& layer : build_layers(build, records, tree, by_offset)) {
    document.add_layer(std::move(layer));
  }
  timer.mark("layers and pixels");
  for (const auto& record : records) {
    if (record.composite_mode != 0 && std::abs(record.composite_mode) != 1 && !record.group &&
        record.have_mode && record.mode != 28 && record.mode != 0) {
      // Clip to backdrop and the other non-union modes differ only over transparency.
      trace("layer '" + record.name + "' composites with mode " + std::to_string(record.composite_mode));
    }
  }

  // The floating selection: a layer above the layer it belongs to, or nothing.
  for (const auto& record : records) {
    if (!record.floating_target.has_value()) {
      continue;
    }
    const auto target = by_offset.find(*record.floating_target);
    if (target == by_offset.end()) {
      build.notices.insert(Notice::FloatingDropped);
      continue;
    }
    auto floating = build_layer(build, record);
    if (!insert_above(document.layers(), target->second, floating)) {
      build.notices.insert(Notice::FloatingDropped);
      continue;
    }
    build.notices.insert(Notice::FloatingAsLayer);
  }

  if (!build.erasers.empty()) {
    convert_erase_layers(build, document.layers(), true);
  }
  timer.mark("floating selection and erase");
  // Clip-to-backdrop layers over transparency get masks from what lies below them.
  if (std::any_of(build.composite.begin(), build.composite.end(), [](const auto& entry) { return entry.second != 1; })) {
    ClipPass pass{build, document.width(), document.height()};
    AlphaPlane backdrop(document.width(), document.height());
    bool started = false;
    clip_pass(pass, document.layers(), backdrop, started);
    if (pass.masked != 0) {
      build.notices.insert(Notice::ClipToBackdropMasks);
    }
    if (pass.clipped != 0) {
      build.notices.insert(Notice::ClipToBackdropClipped);
    }
    if (pass.skipped != 0 || pass.other_modes != 0) {
      build.notices.insert(Notice::CompositeApproximated);
    }
  }

  timer.mark("clip to backdrop");
  // A bottom layer without alpha that covers the canvas is GIMP's background layer.
  if (!document.layers().empty()) {
    auto& bottom = document.layers().front();
    const auto* bottom_record = [&]() -> const LayerRecord* {
      for (const auto& record : records) {
        if (by_offset.contains(record.offset) && by_offset[record.offset] == bottom.id()) {
          return &record;
        }
      }
      return nullptr;
    }();
    if (bottom_record != nullptr && bottom.kind() == LayerKind::Pixel && bottom_record->type % 2U == 0 &&
        bottom.bounds() == Rect{0, 0, document.width(), document.height()} && bottom.opacity() >= 1.0F &&
        bottom.visible() && !bottom.mask().has_value() && bottom.blend_mode() == BlendMode::Normal &&
        bottom_record->effects.empty() && !layer_is_text(bottom)) {
      const PixelFormat format = std::as_const(bottom).pixels().format();
      const auto& source = std::as_const(bottom).pixels();
      PixelFormat rgb = format;
      rgb.channels = 3;
      PixelBuffer opaque(source.width(), source.height(), rgb);
      const auto sample = bytes_per_channel(format.bit_depth);
      auto out = opaque.data();
      const auto in = source.data();
      const auto count = static_cast<std::size_t>(source.width()) * source.height();
      for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(out.data() + i * 3 * sample, in.data() + i * 4 * sample, 3 * sample);
      }
      Layer background(bottom.id(), bottom.name(), std::move(opaque));
      background.set_lock_flags(bottom.lock_flags() | kLayerLockPosition | kLayerLockTransparentPixels);
      background.set_color_label(bottom.color_label());
      bottom = std::move(background);
    }
  }

  // Channels and the selection.
  for (const auto& channel : channels) {
    if (channel.width != width || channel.height != height) {
      trace("a channel's size differs from the canvas");
      build.notices.insert(Notice::DamagedStructure);
      continue;
    }
    account(build, channel.width, channel.height);
    auto pixels = decode_coverage_drawable(reader, channel, precision.component, depth, compression, build.stats);
    std::string name = channel.name;
    if (name.empty()) {
      name = document.next_alpha_channel_name();
    }
    DocumentChannel saved(document.allocate_channel_id(), std::move(name), DocumentChannelKind::Alpha,
                          std::move(pixels));
    DocumentChannelDisplayInfo display;
    display.color = channel.color;
    display.opacity = channel.opacity;
    saved.set_display_info(display);
    document.add_channel(std::move(saved));
  }
  add_paths(document, paths);
  if (base == 2 && !build.colormap.empty()) {
    // The colormap in file order, so the open path offers Palette mode like an indexed PNG.
    const auto count = build.colormap.size();
    const std::uint16_t index_bits = count <= 4 ? 2 : (count <= 16 ? 4 : 8);
    document.indexed_palette() = DocumentIndexedPalette{build.colormap, index_bits};
  }

  // Image parasites: the ICC profile, the comment, Exif and XMP, then GIMP's metadata tag
  // list filling what those left out.
  if (const auto* profile = find_parasite(parasites, "icc-profile"); profile != nullptr && base == 0) {
    document.color_state().embedded_icc_profile.assign(profile->data.begin(), profile->data.end());
  }
  auto& info = document.file_info();
  info.source_format = "XCF";
  if (const auto* comment = find_parasite(parasites, "gimp-comment"); comment != nullptr) {
    std::string text(reinterpret_cast<const char*>(comment->data.data()), comment->data.size());
    while (!text.empty() && text.back() == '\0') {
      text.pop_back();
    }
    if (!text.empty()) {
      info.text.push_back(FileInfoText{FileInfoTextKind::Comment, "", std::move(text), ""});
    }
  }
  if (const auto* exif = find_parasite(parasites, "exif-data"); exif != nullptr) {
    auto data = exif->data;
    if (data.size() >= 6 && std::memcmp(data.data(), "Exif\0\0", 6) == 0) {
      data = data.subspan(6);
    }
    if (data.size() >= 8 && (std::memcmp(data.data(), "II*\0", 4) == 0 || std::memcmp(data.data(), "MM\0*", 4) == 0)) {
      info.exif = std::make_shared<const std::vector<std::uint8_t>>(data.begin(), data.end());
    }
  }
  if (const auto* xmp = find_parasite(parasites, "gimp-metadata"); xmp != nullptr) {
    auto data = xmp->data;
    if (data.size() >= 10 && std::memcmp(data.data(), "GIMP_XMP_1", 10) == 0) {
      data = data.subspan(10);
    }
    if (!data.empty()) {
      info.xmp = std::make_shared<const std::vector<std::uint8_t>>(data.begin(), data.end());
    }
  }
  if (const auto* tags = find_parasite(parasites, "gimp-image-metadata"); tags != nullptr) {
    auto gimp = metadata::parse_gimp_image_metadata(tags->data);
    if (!info.exif && !gimp.exif.empty()) {
      info.exif = make_metadata_bytes(std::move(gimp.exif));
    }
    if (!info.iptc && !gimp.iptc.empty()) {
      info.iptc = make_metadata_bytes(std::move(gimp.iptc));
    }
    if (gimp.xmp) {
      if (!info.xmp) {
        const auto packet = metadata::serialize_xmp(*gimp.xmp);
        info.xmp = make_metadata_bytes(std::vector<std::uint8_t>(packet.begin(), packet.end()));
      } else if (auto existing = metadata::parse_xmp(*info.xmp);
                 existing && metadata::add_missing_xmp_properties(*existing, *gimp.xmp) > 0) {
        const auto packet = metadata::serialize_xmp(*existing);
        info.xmp = make_metadata_bytes(std::vector<std::uint8_t>(packet.begin(), packet.end()));
      }
    }
  }
  fill_gimp_creator_tool(info);

  if (document.layers().empty()) {
    // An image with no layers (GIMP can save one): a transparent layer keeps it openable.
    document.add_pixel_layer("Layer 1", PixelBuffer(document.width(), document.height(),
                                                    with_bit_depth(PixelFormat::rgba8(), depth)));
  }
  if (build.active.has_value() && std::as_const(document).find_layer(*build.active) != nullptr) {
    document.set_active_layer(*build.active);
  } else {
    document.set_active_layer(document.layers().back().id());
  }
  if (build.stats.damaged_tiles.load() != 0) {
    build.notices.insert(Notice::DamagedPixels);
  }
  for (const auto notice : build.notices) {
    notices.emplace_back(notice_text(notice));
  }
  return document;
}

}  // namespace

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() >= 9 && std::memcmp(bytes.data(), "gimp xcf ", 9) == 0) {
    return true;
  }
  const bool gzip = bytes.size() >= 3 && bytes[0] == 0x1F && bytes[1] == 0x8B && bytes[2] == 8;
  const bool bzip2 = bytes.size() >= 4 && bytes[0] == 'B' && bytes[1] == 'Z' && bytes[2] == 'h';
  const bool xz = bytes.size() >= 6 && std::memcmp(bytes.data(), "\xFD" "7zXZ\0", 6) == 0;
  if (!gzip && !bzip2 && !xz) {
    return false;
  }
  try {
    // Only the first bytes: a small limit stops the decoder early.
    std::vector<std::uint8_t> head;
    if (gzip) {
      head = detail::decompress_gzip(bytes, 64);
    } else if (bzip2) {
      head = detail::decompress_bzip2(bytes, 64);
    } else {
      head = detail::decompress_xz(bytes, 64);
    }
    return head.size() >= 9 && std::memcmp(head.data(), "gimp xcf ", 9) == 0;
  } catch (const std::exception&) {
    return false;
  }
}

const std::vector<std::string>& xcf_extensions() {
  static const std::vector<std::string> extensions{".xcf", ".xcfgz", ".xcfbz2", ".xcfxz"};
  return extensions;
}

bool is_compressed_xcf_file_name(std::string_view file_name) noexcept {
  const auto ends_with = [&](std::string_view suffix) {
    if (file_name.size() < suffix.size()) {
      return false;
    }
    for (std::size_t i = 0; i < suffix.size(); ++i) {
      const auto c = static_cast<unsigned char>(file_name[file_name.size() - suffix.size() + i]);
      if (static_cast<char>(std::tolower(c)) != suffix[i]) {
        return false;
      }
    }
    return true;
  };
  return ends_with(".xcf.gz") || ends_with(".xcf.bz2") || ends_with(".xcf.xz");
}

std::vector<std::uint8_t> unwrap(std::span<const std::uint8_t> bytes) {
  try {
    if (bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B) {
      return detail::decompress_gzip(bytes, detail::kMaxUnwrappedBytes);
    }
    if (bytes.size() >= 3 && bytes[0] == 'B' && bytes[1] == 'Z' && bytes[2] == 'h') {
      return detail::decompress_bzip2(bytes, detail::kMaxUnwrappedBytes);
    }
    if (bytes.size() >= 6 && std::memcmp(bytes.data(), "\xFD" "7zXZ\0", 6) == 0) {
      return detail::decompress_xz(bytes, detail::kMaxUnwrappedBytes);
    }
  } catch (const SizeLimitError& error) {
    trace(error.what());
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This GIMP image is too large for Patchy to open."));
  } catch (const std::exception& error) {
    trace(error.what());
    throw std::runtime_error(
        PATCHY_TRANSLATE_NOOP("QObject", "This compressed GIMP file is damaged and could not be opened."));
  }
  return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  std::vector<std::uint8_t> unwrapped;
  const bool wrapped = !(bytes.size() >= 9 && std::memcmp(bytes.data(), "gimp xcf ", 9) == 0);
  if (wrapped) {
    unwrapped = unwrap(bytes);
    bytes = std::span<const std::uint8_t>(unwrapped);
  }
  std::vector<std::string> pending;
  Document document;
  try {
    document = read_document(bytes, pending);
  } catch (const SizeLimitError& error) {
    trace(error.what());
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This GIMP image is too large for Patchy to open."));
  } catch (const ImportError& error) {
    trace(error.what());
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This GIMP file is damaged and could not be opened."));
  }
  if (notices != nullptr) {
    for (auto& notice : pending) {
      notices->push_back(std::move(notice));
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  const auto bytes = formats::read_file_bytes(path, "GIMP");
  return read(std::span<const std::uint8_t>(bytes), notices);
}

}  // namespace patchy::xcf
