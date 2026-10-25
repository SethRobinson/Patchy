// Krita .kra reader (docs/kra.md). Written only from files Krita 5.3 writes and openly
// licensed files found online, unzipped and inspected as data; Krita's source was never read.
//
// Ordering: maindoc.xml lists the uppermost node first, Patchy's layers()[0] is the bottom
// layer, so every sibling list reverses.
//
// Reading runs in two passes. The first parses maindoc.xml into plain records and reads
// each layer's ICC profile, which decides the document depth (any float layer, or integer
// samples in a linear-light profile, make a 32-bit document: Krita blends those in linear
// light, and so does Patchy's 32-bit mode). The second decodes the tiles, layer by layer
// in parallel, straight into PixelBuffers at that depth.

#include "formats/kra_document_io.hpp"

#include "color/color_management.hpp"
#include "color/rgb_profiles.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/vector_raster.hpp"
#include "core/worker_budget.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/kra_internal.hpp"
#include "formats/ora_document_io.hpp"
#include "formats/stb/stb_image.h"
#include "formats/svg_xml.hpp"
#include "formats/zip_entry_reader.hpp"
#include "psd/asl_io.hpp"
#include "render/compositor.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace patchy::kra {

using namespace detail;

namespace {

constexpr std::string_view kMimetype = "application/x-krita";

// Hostile-input caps, the OpenRaster reader's (docs/ora.md "Limits"): a canvas or layer side
// past the PSB limit, an image past 2^28 pixels, more than 2^31 decoded pixels in all, more
// than 4 GiB extracted, more than 200000 entries, or maindoc.xml past 64 MiB.
constexpr std::int64_t kMaxSide = 300000;
constexpr std::uint64_t kMaxImagePixels = 1ULL << 28;
constexpr std::uint64_t kMaxTotalPixels = 1ULL << 31;
constexpr std::uint64_t kMaxEntryBytes = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
constexpr std::uint64_t kMaxTotalExtracted = 1ULL << 32;
constexpr std::uint64_t kMaxXmlBytes = 64ULL << 20;
constexpr std::uint64_t kMaxSmallEntryBytes = 64ULL << 20;
constexpr std::size_t kMaxEntries = 200000;
constexpr int kMaxDepth = 64;

[[nodiscard]] std::runtime_error read_error(const char* message) {
  return std::runtime_error(message);
}

[[nodiscard]] const char* damaged_tree_message() {
  return PATCHY_TRANSLATE_NOOP("QObject", "The Krita layer list (maindoc.xml) is damaged or not valid XML");
}

[[nodiscard]] formats::ZipReaderMessages zip_messages() {
  return {PATCHY_TRANSLATE_NOOP("QObject", "The Krita file is damaged: its zip directory is missing or cut short"),
          PATCHY_TRANSLATE_NOOP("QObject", "The Krita file is too large to open"),
          PATCHY_TRANSLATE_NOOP(
              "QObject", "The Krita file uses zip features Patchy cannot read (encryption or an unknown compression)"),
          PATCHY_TRANSLATE_NOOP("QObject", "The Krita file is damaged: an entry cannot be unpacked")};
}

// ---------------------------------------------------------------------------------------
// XML helpers. maindoc.xml declares Krita's namespace as the default one, so element names
// arrive bare; a prefixed form is tolerated all the same.

[[nodiscard]] std::string_view local_name(std::string_view name) {
  const auto colon = name.rfind(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

[[nodiscard]] const svg::XmlNode* find_child(const svg::XmlNode& node, std::string_view name) {
  for (const auto& child : node.children) {
    if (!child.is_text() && local_name(child.name) == name) {
      return &child;
    }
  }
  return nullptr;
}

[[nodiscard]] std::string attribute_or(const svg::XmlNode& node, std::string_view name, std::string fallback = {}) {
  const auto* value = node.attribute(name);
  return value != nullptr ? *value : std::move(fallback);
}

[[nodiscard]] std::optional<double> parse_number(std::string_view text) {
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  while (!text.empty() && text.back() == ' ') {
    text.remove_suffix(1);
  }
  if (text.empty()) {
    return std::nullopt;
  }
  std::istringstream stream{std::string(text)};
  stream.imbue(std::locale::classic());
  double value = 0.0;
  stream >> value;
  if (stream.fail() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::int32_t attribute_int(const svg::XmlNode& node, std::string_view name, std::int32_t fallback) {
  const auto value = parse_number(attribute_or(node, name));
  if (!value.has_value() || std::abs(*value) > static_cast<double>(kMaxSide)) {
    return fallback;
  }
  return static_cast<std::int32_t>(std::lround(*value));
}

[[nodiscard]] bool attribute_flag(const svg::XmlNode& node, std::string_view name, bool fallback) {
  const auto* value = node.attribute(name);
  if (value == nullptr) {
    return fallback;
  }
  return *value == "1" || *value == "true";
}

void add_unique(std::vector<std::string>& list, std::string value) {
  if (std::find(list.begin(), list.end(), value) == list.end()) {
    list.push_back(std::move(value));
  }
}

[[nodiscard]] std::string joined(const std::vector<std::string>& items) {
  std::string text;
  for (const auto& item : items) {
    text += (text.empty() ? "" : ", ") + item;
  }
  return text;
}

// ---------------------------------------------------------------------------------------
// Blend modes. Krita's own ids, as maindoc.xml stores them in compositeop. The ids Krita also
// writes into OpenRaster files (prefixed "krita:") resolve through the OpenRaster table; the
// rest are the ids whose OpenRaster form is a spec name.

struct KritaOp {
  std::string_view id;
  BlendMode mode;
};

constexpr std::array kKritaOps = {
    KritaOp{"normal", BlendMode::Normal},
    KritaOp{"multiply", BlendMode::Multiply},
    KritaOp{"screen", BlendMode::Screen},
    KritaOp{"overlay", BlendMode::Overlay},
    KritaOp{"darken", BlendMode::Darken},
    KritaOp{"lighten", BlendMode::Lighten},
    KritaOp{"dodge", BlendMode::ColorDodge},
    KritaOp{"burn", BlendMode::ColorBurn},
    KritaOp{"hard_light", BlendMode::HardLight},
    KritaOp{"diff", BlendMode::Difference},
    KritaOp{"color", BlendMode::Color},
    KritaOp{"hue", BlendMode::Hue},
    KritaOp{"saturation", BlendMode::Saturation},
    KritaOp{"luminize", BlendMode::Luminosity},
    KritaOp{"add", BlendMode::LinearDodge},
    // Paint.NET's modes have the same formulas in Krita (measured on its renders).
    KritaOp{"xor", BlendMode::Xor},
    KritaOp{"negation", BlendMode::Negation},
};

// Krita modes that open as a near Patchy mode (docs/kra.md "Blend modes" has the measured
// differences).
constexpr std::array kApproximateOps = {
    KritaOp{"soft_light_svg", BlendMode::SoftLight},
    // Krita's Reflect and Glow round differently from Paint.NET's (at most 11 levels apart).
    KritaOp{"reflect", BlendMode::Reflect},
    KritaOp{"glow", BlendMode::Glow},
    // SAI's Luminosity/Shine adds the layer scaled by its alpha: Linear Dodge where the sum
    // stays below white.
    KritaOp{"luminosity_sai", BlendMode::LinearDodge},
};

[[nodiscard]] std::optional<BlendMode> blend_mode_from_krita(std::string_view id, bool& approximate) {
  approximate = false;
  for (const auto& entry : kKritaOps) {
    if (entry.id == id) {
      return entry.mode;
    }
  }
  if (const auto mode = ora::detail::blend_mode_from_composite_op("krita:" + std::string(id)); mode.has_value()) {
    return mode;
  }
  for (const auto& entry : kApproximateOps) {
    if (entry.id == id) {
      approximate = true;
      return entry.mode;
    }
  }
  return std::nullopt;
}

// Krita's color labels in its menu order (none, blue, green, yellow, orange, brown, red,
// purple, gray) to Patchy's; brown has no label of its own and opens as orange.
[[nodiscard]] LayerColorLabel color_label_from_krita(int label) {
  switch (label) {
    case 1:
      return kLayerColorBlue;
    case 2:
      return kLayerColorGreen;
    case 3:
      return kLayerColorYellow;
    case 4:
    case 5:
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

// ---------------------------------------------------------------------------------------
// The layer tree as plain records.

enum class NodeType : std::uint8_t { Paint, Group, Vector, Filter, Generator, Clone, File, Unknown };
enum class MaskType : std::uint8_t { Transparency, Filter, Selection, Transform, Colorize, Unknown };

struct MaskNode {
  MaskType type{MaskType::Unknown};
  std::string nodetype;
  std::string name;
  std::string filename;
  std::string filter_name;
  std::int32_t x{0};
  std::int32_t y{0};
  bool visible{true};
  std::optional<RawImage> selection;
  std::optional<FilterConfig> config;
};

// How a layer's samples become Patchy's: the color space, and whether its profile has a
// linear tone curve (those samples are linear light, whatever their sample type).
struct SampleSource {
  ColorSpace space;
  std::vector<std::uint8_t> icc;
  bool linear{false};
};

struct Node {
  NodeType type{NodeType::Unknown};
  std::string nodetype;
  std::string name;
  std::string uuid;
  std::string filename;
  std::string tile_file;  // the tile entry: the filename, or an animated layer's current frame
  std::string composite_op{"normal"};
  std::string layer_style;
  std::string clone_from_uuid;
  std::string clone_from_name;
  std::string file_source;
  std::string filter_name;
  std::int32_t x{0};
  std::int32_t y{0};
  float opacity{1.0F};
  bool visible{true};
  bool locked{false};
  bool alpha_locked{false};
  bool inherit_alpha{false};
  bool passthrough{false};
  bool selected{false};
  std::uint8_t restricted_channels{0};
  int color_label{0};
  SampleSource source;
  std::vector<MaskNode> masks;  // uppermost first, as in the file
  std::vector<Node> children;   // uppermost first, as in the file
  std::optional<RawImage> raw;
  std::vector<std::uint8_t> encoded;  // the layer's tile file, until the parallel decode
  std::optional<PixelBuffer> pixels;  // decoded at the document depth
  Rect pixel_bounds{};
  bool covers_canvas{false};  // an opaque default pixel: the layer fills the canvas
  std::optional<FilterConfig> config;     // filter and fill layers
  std::optional<RawImage> selection;      // filter and fill layers
  std::vector<std::uint8_t> svg;          // vector layers
};

struct ReadState {
  explicit ReadState(formats::ZipEntryReader& reader) : zip(reader) {}

  formats::ZipEntryReader& zip;
  std::string prefix;  // "<image name>/" in front of every layer entry
  std::vector<std::uint8_t> image_icc;
  ColorSpace image_space;
  std::uint64_t pixel_budget{kMaxTotalPixels};
  std::int32_t width{0};
  std::int32_t height{0};
  double xres{72.0};
  double yres{72.0};
  int current_time{0};
  int layer_count{0};
  std::vector<std::string> unknown_ops;
  std::vector<std::string> approximated_ops;
  std::vector<std::string> unreadable;
  std::vector<std::string> unsupported_spaces;
  std::vector<std::string> unsupported_nodes;
  std::vector<std::string> unsupported_masks;
  std::vector<std::string> unsupported_filters;
  std::vector<std::string> approximated_filters;
  std::vector<std::string> unsupported_generators;
  std::vector<std::string> merged_masks;
  std::vector<std::string> inherit_alpha_grouped;
  std::vector<std::string> inherit_alpha_unclipped;
  std::vector<std::string> inlined_groups;
  std::vector<std::string> inherit_alpha_flattened;
  std::vector<std::string> clone_layers;
  std::vector<std::string> missing_files;
  std::vector<std::string> loaded_files;
  std::vector<std::string> vector_notices;
  std::vector<std::string> missing_styles;
  std::vector<std::string> style_notices;
  bool any_float{false};
  bool any_sixteen{false};
  bool any_gray{false};
  bool any_cmyk{false};
  bool any_lab{false};
  bool animated{false};
  std::map<std::string, std::optional<CmykToRgbTransform>> cmyk_transforms;
};

[[nodiscard]] NodeType node_type_from(std::string_view type) {
  if (type == "paintlayer") return NodeType::Paint;
  if (type == "grouplayer") return NodeType::Group;
  if (type == "shapelayer") return NodeType::Vector;
  if (type == "adjustmentlayer") return NodeType::Filter;
  if (type == "generatorlayer") return NodeType::Generator;
  if (type == "clonelayer") return NodeType::Clone;
  if (type == "filelayer") return NodeType::File;
  return NodeType::Unknown;
}

[[nodiscard]] MaskType mask_type_from(std::string_view type) {
  if (type == "transparencymask") return MaskType::Transparency;
  if (type == "filtermask") return MaskType::Filter;
  if (type == "selectionmask") return MaskType::Selection;
  if (type == "transformmask") return MaskType::Transform;
  if (type == "colorizemask") return MaskType::Colorize;
  return MaskType::Unknown;
}

// Whether a profile's tone curve is linear: mid gray comes out far lighter in sRGB.
[[nodiscard]] bool profile_is_linear(const SampleSource& source) {
  if (source.icc.empty()) {
    return false;
  }
  if (source.space.model == ColorModel::Rgb) {
    const auto transform = RgbProfileTransform::create(source.icc, {}, RenderingIntent::RelativeColorimetric, true);
    return transform.has_value() && transform->convert_color(RgbColor{128, 128, 128}).red > 160;
  }
  if (source.space.model == ColorModel::Gray) {
    const auto transform = GrayToRgbTransform::from_icc_profile(source.icc);
    return transform.has_value() && transform->convert_single(128).red > 160;
  }
  return false;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> extract_small(ReadState& state, const std::string& name) {
  return state.zip.extract(name, kMaxSmallEntryBytes);
}

[[nodiscard]] std::optional<RawImage> read_selection(ReadState& state, const std::string& base, std::int32_t x,
                                                     std::int32_t y, std::string& reason) {
  auto bytes = state.zip.extract(state.prefix + "layers/" + base + ".pixelselection", kMaxEntryBytes);
  if (!bytes.has_value()) {
    reason = "no selection data";
    return std::nullopt;
  }
  std::vector<std::uint8_t> default_pixel{0};
  if (auto value = extract_small(state, state.prefix + "layers/" + base + ".pixelselection.defaultpixel");
      value.has_value() && value->size() == 1) {
    default_pixel = std::move(*value);
  }
  if (bytes->empty()) {
    RawImage empty;
    empty.pixel_bytes = 1;
    empty.default_pixel = default_pixel;
    empty.default_is_transparent = default_pixel[0] == 0;
    return empty;
  }
  return read_tiles(*bytes, 1, std::move(default_pixel), x, y, TileReadLimits{kMaxSide, kMaxImagePixels},
                    state.pixel_budget, reason);
}

struct Keyframe {
  std::string file;
  std::optional<std::pair<std::int32_t, std::int32_t>> offset;
};

// The content keyframe showing at `time`: the last one at or before it (the first one when
// the time is before them all).
[[nodiscard]] std::optional<Keyframe> current_keyframe(std::span<const std::uint8_t> xml, int time) {
  svg::XmlNode root;
  try {
    root = svg::parse_xml(xml);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  std::vector<std::pair<int, Keyframe>> frames;
  for (const auto& channel : root.children) {
    if (channel.is_text() || local_name(channel.name) != "channel" || attribute_or(channel, "name") != "content") {
      continue;
    }
    for (const auto& key : channel.children) {
      if (key.is_text() || local_name(key.name) != "keyframe") {
        continue;
      }
      Keyframe frame;
      frame.file = attribute_or(key, "frame");
      if (!formats::safe_zip_entry_path(frame.file)) {
        continue;
      }
      if (const auto* offset = find_child(key, "offset"); offset != nullptr) {
        frame.offset = std::make_pair(attribute_int(*offset, "x", 0), attribute_int(*offset, "y", 0));
      }
      frames.emplace_back(attribute_int(key, "time", 0), std::move(frame));
    }
  }
  if (frames.empty()) {
    return std::nullopt;
  }
  std::stable_sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::size_t chosen = 0;
  for (std::size_t i = 0; i < frames.size(); ++i) {
    if (frames[i].first <= time) {
      chosen = i;
    }
  }
  return frames[chosen].second;
}

void read_masks(const svg::XmlNode& list, Node& node, ReadState& state);
void read_layers(const svg::XmlNode& list, std::vector<Node>& out, ReadState& state, Node* owner, int depth);

void read_mask_element(const svg::XmlNode& element, Node& owner, ReadState& state) {
  MaskNode mask;
  mask.nodetype = attribute_or(element, "nodetype");
  mask.type = mask_type_from(mask.nodetype);
  mask.name = attribute_or(element, "name", "Mask");
  mask.filename = attribute_or(element, "filename");
  mask.filter_name = attribute_or(element, "filtername");
  mask.x = attribute_int(element, "x", 0);
  mask.y = attribute_int(element, "y", 0);
  mask.visible = attribute_flag(element, "visible", true);
  if (!formats::safe_zip_entry_path(mask.filename)) {
    mask.filename.clear();
  }
  if ((mask.type == MaskType::Transparency || mask.type == MaskType::Filter || mask.type == MaskType::Selection) &&
      !mask.filename.empty()) {
    std::string reason;
    mask.selection = read_selection(state, mask.filename, mask.x, mask.y, reason);
    if (!mask.selection.has_value()) {
      add_unique(state.unreadable, owner.name + " / " + mask.name + " (" + reason + ")");
    }
  }
  if (mask.type == MaskType::Filter && !mask.filename.empty()) {
    if (auto xml = extract_small(state, state.prefix + "layers/" + mask.filename + ".filterconfig");
        xml.has_value()) {
      mask.config = parse_filter_config(mask.filter_name, *xml);
    }
  }
  owner.masks.push_back(std::move(mask));
}

void read_masks(const svg::XmlNode& list, Node& node, ReadState& state) {
  for (const auto& child : list.children) {
    if (!child.is_text() && local_name(child.name) == "mask") {
      read_mask_element(child, node, state);
    }
  }
}

// The channel flags string ("1110": one character per channel in the color space's own
// order, alpha last) as Patchy's restricted channels and the alpha flag.
void read_channel_flags(const std::string& flags, const ColorSpace& space, std::uint8_t& restricted, bool& alpha_off) {
  restricted = 0;
  alpha_off = false;
  if (flags.empty() || static_cast<int>(flags.size()) != space.channels()) {
    return;
  }
  alpha_off = flags.back() == '0';
  if (space.model != ColorModel::Rgb) {
    return;
  }
  // Integer RGB stores blue, green, red; float RGB stores red, green, blue.
  const bool bgr = !space.is_float();
  const std::array<std::uint8_t, 3> bits = bgr ? std::array<std::uint8_t, 3>{kRestrictBlue, kRestrictGreen, kRestrictRed}
                                               : std::array<std::uint8_t, 3>{kRestrictRed, kRestrictGreen, kRestrictBlue};
  for (std::size_t i = 0; i < 3; ++i) {
    if (flags[i] == '0') {
      restricted |= bits[i];
    }
  }
}

void read_layer_element(const svg::XmlNode& element, std::vector<Node>& out, ReadState& state, int depth) {
  Node node;
  ++state.layer_count;
  node.nodetype = attribute_or(element, "nodetype");
  node.type = node_type_from(node.nodetype);
  node.name = attribute_or(element, "name");
  if (node.name.empty()) {
    node.name = "Layer " + std::to_string(state.layer_count);
  }
  node.uuid = attribute_or(element, "uuid");
  node.filename = attribute_or(element, "filename");
  if (!formats::safe_zip_entry_path(node.filename)) {
    node.filename.clear();
  }
  node.composite_op = attribute_or(element, "compositeop", "normal");
  node.layer_style = attribute_or(element, "layerstyle");
  node.clone_from_uuid = attribute_or(element, "clonefromuuid");
  node.clone_from_name = attribute_or(element, "clonefrom");
  node.file_source = attribute_or(element, "source");
  node.filter_name = attribute_or(element, node.type == NodeType::Generator ? "generatorname" : "filtername");
  node.x = attribute_int(element, "x", 0);
  node.y = attribute_int(element, "y", 0);
  node.opacity = static_cast<float>(std::clamp(attribute_int(element, "opacity", 255), 0, 255)) / 255.0F;
  node.visible = attribute_flag(element, "visible", true);
  node.locked = attribute_flag(element, "locked", false);
  node.passthrough = attribute_flag(element, "passthrough", false);
  node.selected = attribute_flag(element, "selected", false);
  node.color_label = attribute_int(element, "colorlabel", 0);
  node.source.space = parse_color_space(attribute_or(element, "colorspacename", state.image_space.name));
  std::uint8_t ignored = 0;
  bool lock_alpha = false;
  read_channel_flags(attribute_or(element, "channellockflags"), node.source.space, ignored, lock_alpha);
  node.alpha_locked = lock_alpha;
  read_channel_flags(attribute_or(element, "channelflags"), node.source.space, node.restricted_channels,
                     node.inherit_alpha);

  if (const auto* masks = find_child(element, "masks"); masks != nullptr) {
    read_masks(*masks, node, state);
  }
  const auto layer_entry = state.prefix + "layers/" + node.filename;
  switch (node.type) {
    case NodeType::Group:
      if (const auto* layers = find_child(element, "layers"); layers != nullptr) {
        read_layers(*layers, node.children, state, &node, depth + 1);
      }
      break;
    case NodeType::Paint: {
      if (node.filename.empty()) {
        break;
      }
      if (auto icc = extract_small(state, layer_entry + ".icc"); icc.has_value()) {
        node.source.icc = std::move(*icc);
      } else {
        node.source.icc = state.image_icc;
      }
      // An animated layer: its keyframes list names the tile file for each time. The frame
      // showing at the document's current time opens, as Krita shows and renders it.
      node.tile_file = node.filename;
      if (const auto keyframes = attribute_or(element, "keyframes"); !keyframes.empty() &&
                                                                     formats::safe_zip_entry_path(keyframes)) {
        if (auto xml = extract_small(state, state.prefix + "layers/" + keyframes); xml.has_value()) {
          if (const auto frame = current_keyframe(*xml, state.current_time); frame.has_value()) {
            state.animated = true;
            node.tile_file = frame->file;
            if (frame->offset.has_value()) {
              node.x = frame->offset->first;
              node.y = frame->offset->second;
            }
          }
        }
      }
      if (auto bytes = state.zip.extract(state.prefix + "layers/" + node.tile_file, kMaxEntryBytes);
          bytes.has_value()) {
        node.encoded = std::move(*bytes);
      }
      break;
    }
    case NodeType::Vector:
      if (!node.filename.empty()) {
        if (auto svg = extract_small(state, layer_entry + ".shapelayer/content.svg"); svg.has_value()) {
          node.svg = std::move(*svg);
        }
      }
      break;
    case NodeType::Filter:
    case NodeType::Generator: {
      if (node.filename.empty()) {
        break;
      }
      if (auto xml = extract_small(state, layer_entry + ".filterconfig"); xml.has_value()) {
        node.config = parse_filter_config(node.filter_name, *xml);
      } else {
        node.config = FilterConfig{node.filter_name, 0, {}};
      }
      std::string reason;
      node.selection = read_selection(state, node.filename, node.x, node.y, reason);
      break;
    }
    default:
      break;
  }
  out.push_back(std::move(node));
}

void read_layers(const svg::XmlNode& list, std::vector<Node>& out, ReadState& state, Node* owner, int depth) {
  if (depth > kMaxDepth) {
    throw read_error(damaged_tree_message());
  }
  for (const auto& child : list.children) {
    if (child.is_text()) {
      continue;
    }
    const auto name = local_name(child.name);
    if (name == "layer") {
      read_layer_element(child, out, state, depth);
    } else if (name == "mask" && owner != nullptr) {
      // A group lists its masks among its layers.
      read_mask_element(child, *owner, state);
    }
  }
}

// A pass-through group at full opacity with no masks or style is the same as its layers in
// its place. When its bottom layer inherits alpha, Krita clips that layer to what lies
// below the group, which only works in Patchy with the group's layers inline.
void inline_pass_through_groups(std::vector<Node>& nodes, ReadState& state) {
  for (auto& node : nodes) {
    inline_pass_through_groups(node.children, state);
  }
  for (std::size_t index = 0; index < nodes.size();) {
    auto& node = nodes[index];
    const bool candidate = node.type == NodeType::Group && node.passthrough && node.visible && node.opacity >= 1.0F &&
                           node.masks.empty() && node.layer_style.empty() && !node.children.empty() &&
                           node.children.back().inherit_alpha && !node.inherit_alpha;
    if (!candidate) {
      ++index;
      continue;
    }
    add_unique(state.inlined_groups, node.name);
    auto children = std::move(node.children);
    nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
    const auto count = children.size();
    nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(index), std::make_move_iterator(children.begin()),
                 std::make_move_iterator(children.end()));
    index += count;
  }
}

// ---------------------------------------------------------------------------------------
// Samples to Patchy buffers.

[[nodiscard]] float sample_value(const std::uint8_t* pixel, int channel, SampleType sample) {
  switch (sample) {
    case SampleType::U8:
      return static_cast<float>(pixel[channel]) / 255.0F;
    case SampleType::U16: {
      std::uint16_t value = 0;
      std::memcpy(&value, pixel + channel * 2, 2);
      return static_cast<float>(value) / 65535.0F;
    }
    case SampleType::F16: {
      std::uint16_t value = 0;
      std::memcpy(&value, pixel + channel * 2, 2);
      return half_to_float(value);
    }
    case SampleType::F32: {
      float value = 0.0F;
      std::memcpy(&value, pixel + channel * 4, 4);
      return std::isfinite(value) ? value : 0.0F;
    }
  }
  return 0.0F;
}

// A lookup from encoded integer samples to linear light, for the 32-bit target.
[[nodiscard]] const std::vector<float>& decode_table_u8() {
  static const std::vector<float> table = [] {
    std::vector<float> values(256);
    for (int i = 0; i < 256; ++i) {
      values[static_cast<std::size_t>(i)] = static_cast<float>(srgb_decode(i / 255.0));
    }
    return values;
  }();
  return table;
}

[[nodiscard]] const std::vector<float>& decode_table_u16() {
  static const std::vector<float> table = [] {
    std::vector<float> values(65536);
    for (int i = 0; i < 65536; ++i) {
      values[static_cast<std::size_t>(i)] = static_cast<float>(srgb_decode(i / 65535.0));
    }
    return values;
  }();
  return table;
}

// Converts a decoded paint device to RGBA at `depth`. Integer targets hold display-encoded
// values, the 32-bit target linear light (docs/high-bit-depth.md). CMYK goes through the
// layer's own profile; Lab through Little CMS's built-in Lab.
[[nodiscard]] std::optional<PixelBuffer> to_pixel_buffer(const RawImage& raw, const SampleSource& source,
                                                         BitDepth depth, ReadState& state) {
  const auto& space = source.space;
  const auto width = raw.bounds.width;
  const auto height = raw.bounds.height;
  PixelBuffer out(width, height, with_bit_depth(PixelFormat::rgba8(), depth));
  if (width <= 0 || height <= 0) {
    return out;
  }
  const auto pb = static_cast<std::size_t>(raw.pixel_bytes);
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  auto bytes = out.data();

  // Fast paths: integer RGB to the same integer depth.
  if (space.model == ColorModel::Rgb && !source.linear && space.sample == SampleType::U8 && depth == BitDepth::UInt8) {
    for (std::size_t i = 0; i < count; ++i) {
      const auto* in = raw.data.data() + i * pb;
      auto* px = bytes.data() + i * 4U;
      px[0] = in[2];
      px[1] = in[1];
      px[2] = in[0];
      px[3] = in[3];
    }
    return out;
  }
  if (space.model == ColorModel::Rgb && !source.linear && space.sample == SampleType::U16 &&
      depth == BitDepth::UInt16) {
    for (std::size_t i = 0; i < count; ++i) {
      const auto* in = raw.data.data() + i * pb;
      auto* px = bytes.data() + i * 8U;
      std::memcpy(px + 0, in + 4, 2);
      std::memcpy(px + 2, in + 2, 2);
      std::memcpy(px + 4, in + 0, 2);
      std::memcpy(px + 6, in + 6, 2);
    }
    return out;
  }

  // Everything else: straight RGBA floats per pixel, encoded (0..1) or linear light.
  std::vector<float> rgba(count * 4U);
  bool linear = source.linear || space.is_float();
  switch (space.model) {
    case ColorModel::Rgb: {
      const bool bgr = !space.is_float();
      for (std::size_t i = 0; i < count; ++i) {
        const auto* in = raw.data.data() + i * pb;
        auto* px = rgba.data() + i * 4U;
        px[0] = sample_value(in, bgr ? 2 : 0, space.sample);
        px[1] = sample_value(in, 1, space.sample);
        px[2] = sample_value(in, bgr ? 0 : 2, space.sample);
        px[3] = sample_value(in, 3, space.sample);
      }
      break;
    }
    case ColorModel::Gray: {
      for (std::size_t i = 0; i < count; ++i) {
        const auto* in = raw.data.data() + i * pb;
        auto* px = rgba.data() + i * 4U;
        px[0] = px[1] = px[2] = sample_value(in, 0, space.sample);
        px[3] = sample_value(in, 1, space.sample);
      }
      break;
    }
    case ColorModel::Cmyk: {
      // Krita stores ink amounts (0 = no ink); the transform takes inverted values.
      auto& transform = state.cmyk_transforms[std::string(source.icc.begin(), source.icc.end())];
      if (!transform.has_value()) {
        transform = CmykToRgbTransform::from_icc_profile(source.icc.empty() ? default_cmyk_profile() : source.icc);
        if (!transform.has_value()) {
          transform = CmykToRgbTransform::from_icc_profile(default_cmyk_profile());
        }
      }
      if (!transform.has_value()) {
        return std::nullopt;
      }
      linear = false;
      std::vector<std::uint16_t> cmyk(count * 4U);
      std::vector<std::uint16_t> rgb(count * 3U);
      for (std::size_t i = 0; i < count; ++i) {
        const auto* in = raw.data.data() + i * pb;
        for (int c = 0; c < 4; ++c) {
          const float ink = space.is_float() ? std::clamp(sample_value(in, c, space.sample) / 100.0F, 0.0F, 1.0F)
                                             : sample_value(in, c, space.sample);
          cmyk[i * 4U + static_cast<std::size_t>(c)] =
              static_cast<std::uint16_t>(std::lround((1.0F - std::clamp(ink, 0.0F, 1.0F)) * 65535.0F));
        }
      }
      transform->convert16(cmyk.data(), rgb.data(), count);
      for (std::size_t i = 0; i < count; ++i) {
        const auto* in = raw.data.data() + i * pb;
        auto* px = rgba.data() + i * 4U;
        for (int c = 0; c < 3; ++c) {
          px[c] = static_cast<float>(rgb[i * 3U + static_cast<std::size_t>(c)]) / 65535.0F;
        }
        px[3] = std::clamp(sample_value(in, 4, space.sample), 0.0F, 1.0F);
      }
      break;
    }
    case ColorModel::Lab: {
      const auto transform = LabToRgbTransform::create();
      if (!transform.has_value() || space.sample != SampleType::U16) {
        return std::nullopt;
      }
      linear = false;
      std::vector<std::uint16_t> lab(count * 3U);
      std::vector<std::uint16_t> rgb(count * 3U);
      for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(lab.data() + i * 3U, raw.data.data() + i * pb, 6);
      }
      transform->convert16(lab.data(), rgb.data(), count);
      for (std::size_t i = 0; i < count; ++i) {
        auto* px = rgba.data() + i * 4U;
        for (int c = 0; c < 3; ++c) {
          px[c] = static_cast<float>(rgb[i * 3U + static_cast<std::size_t>(c)]) / 65535.0F;
        }
        px[3] = sample_value(raw.data.data() + i * pb, 3, space.sample);
      }
      break;
    }
    default:
      return std::nullopt;
  }

  const bool integer_source = !space.is_float();
  for (std::size_t i = 0; i < count; ++i) {
    const auto* px = rgba.data() + i * 4U;
    const float alpha = std::clamp(px[3], 0.0F, 1.0F);
    switch (depth) {
      case BitDepth::UInt8: {
        auto* target = bytes.data() + i * 4U;
        for (int c = 0; c < 3; ++c) {
          const double value = linear ? srgb_encode(std::max(0.0F, px[c])) : std::clamp(px[c], 0.0F, 1.0F);
          target[c] = static_cast<std::uint8_t>(std::lround(value * 255.0));
        }
        target[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0F));
        break;
      }
      case BitDepth::UInt16: {
        auto* target = bytes.data() + i * 8U;
        for (int c = 0; c < 4; ++c) {
          double value = c == 3 ? alpha : (linear ? srgb_encode(std::max(0.0F, px[c])) : std::clamp(px[c], 0.0F, 1.0F));
          const auto sample = static_cast<std::uint16_t>(std::lround(value * 65535.0));
          std::memcpy(target + c * 2, &sample, 2);
        }
        break;
      }
      case BitDepth::Float32: {
        auto* target = bytes.data() + i * 16U;
        std::array<float, 4> values{};
        for (int c = 0; c < 3; ++c) {
          if (linear) {
            values[static_cast<std::size_t>(c)] = px[c];
          } else if (integer_source && space.model == ColorModel::Rgb && space.sample == SampleType::U8) {
            values[static_cast<std::size_t>(c)] =
                decode_table_u8()[static_cast<std::size_t>(std::lround(std::clamp(px[c], 0.0F, 1.0F) * 255.0F))];
          } else {
            values[static_cast<std::size_t>(c)] =
                decode_table_u16()[static_cast<std::size_t>(std::lround(std::clamp(px[c], 0.0F, 1.0F) * 65535.0F))];
          }
        }
        values[3] = alpha;
        std::memcpy(target, values.data(), 16);
        break;
      }
    }
  }
  return out;
}

[[nodiscard]] PixelBuffer selection_to_gray(const RawImage& raw) {
  PixelBuffer out(raw.bounds.width, raw.bounds.height, PixelFormat::gray8());
  if (!raw.data.empty()) {
    std::memcpy(out.data().data(), raw.data.data(), std::min(out.data().size(), raw.data.size()));
  }
  return out;
}

// A selection as a layer mask: the stored tiles, with the default value beyond them.
[[nodiscard]] std::optional<LayerMask> mask_from_selection(const RawImage& raw) {
  LayerMask mask;
  mask.default_color = raw.default_pixel.empty() ? 0 : raw.default_pixel[0];
  if (raw.bounds.width <= 0 || raw.bounds.height <= 0) {
    if (mask.default_color == 255) {
      return std::nullopt;  // selects everything: no mask needed
    }
    mask.bounds = Rect{0, 0, 1, 1};
    mask.pixels = PixelBuffer(1, 1, PixelFormat::gray8());
    mask.pixels.data()[0] = mask.default_color;
    return mask;
  }
  mask.bounds = raw.bounds;
  mask.pixels = selection_to_gray(raw);
  if (mask.default_color == 255 && std::all_of(raw.data.begin(), raw.data.end(), [](std::uint8_t v) { return v == 255; })) {
    return std::nullopt;
  }
  return mask;
}

// Multiplies two masks over the union of their bounds.
[[nodiscard]] LayerMask multiply_masks(const LayerMask& a, const LayerMask& b) {
  const auto left = std::min(a.bounds.x, b.bounds.x);
  const auto top = std::min(a.bounds.y, b.bounds.y);
  const auto right = std::max(a.bounds.x + a.bounds.width, b.bounds.x + b.bounds.width);
  const auto bottom = std::max(a.bounds.y + a.bounds.height, b.bounds.y + b.bounds.height);
  LayerMask out;
  out.bounds = Rect{left, top, right - left, bottom - top};
  out.default_color = static_cast<std::uint8_t>((a.default_color * b.default_color + 127) / 255);
  out.pixels = PixelBuffer(out.bounds.width, out.bounds.height, PixelFormat::gray8());
  const auto sample = [](const LayerMask& mask, std::int32_t x, std::int32_t y) -> int {
    const auto lx = x - mask.bounds.x;
    const auto ly = y - mask.bounds.y;
    if (lx < 0 || ly < 0 || lx >= mask.bounds.width || ly >= mask.bounds.height) {
      return mask.default_color;
    }
    return mask.pixels.pixel(lx, ly)[0];
  };
  for (std::int32_t y = 0; y < out.bounds.height; ++y) {
    auto row = out.pixels.row(y);
    for (std::int32_t x = 0; x < out.bounds.width; ++x) {
      const auto value = sample(a, left + x, top + y) * sample(b, left + x, top + y);
      row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>((value + 127) / 255);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------------------
// Decoding.

// Whether a layer's pixels cover the canvas with full alpha everywhere.
[[nodiscard]] bool opaque_over_canvas(const PixelBuffer& pixels, Rect bounds, std::int32_t width, std::int32_t height) {
  if (bounds.x > 0 || bounds.y > 0 || bounds.x + bounds.width < width || bounds.y + bounds.height < height) {
    return false;
  }
  const auto depth = pixels.format().bit_depth;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto row = pixels.row(y - bounds.y);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto at = static_cast<std::size_t>(x - bounds.x);
      bool full = false;
      if (depth == BitDepth::UInt8) {
        full = row[at * 4U + 3U] == 255;
      } else if (depth == BitDepth::UInt16) {
        std::uint16_t alpha = 0;
        std::memcpy(&alpha, row.data() + at * 8U + 6U, 2);
        full = alpha == 65535;
      } else {
        float alpha = 0.0F;
        std::memcpy(&alpha, row.data() + at * 16U + 12U, 4);
        full = alpha >= 1.0F;
      }
      if (!full) {
        return false;
      }
    }
  }
  return true;
}

// Whether a default pixel's alpha is full scale (alpha is the last channel everywhere).
[[nodiscard]] bool default_alpha_is_full(const std::vector<std::uint8_t>& pixel, const ColorSpace& space) {
  const auto bytes = static_cast<std::size_t>(space.sample_bytes());
  if (pixel.size() < bytes) {
    return false;
  }
  return sample_value(pixel.data() + pixel.size() - bytes, 0, space.sample) >= 1.0F;
}

void collect_paint(std::vector<Node>& nodes, std::vector<Node*>& out) {
  for (auto& node : nodes) {
    if (!node.encoded.empty()) {
      out.push_back(&node);
    }
    collect_paint(node.children, out);
  }
}

void decide_linear(std::vector<Node>& nodes, ReadState& state) {
  for (auto& node : nodes) {
    if (node.type == NodeType::Paint) {
      node.source.linear = !node.source.space.is_float() && profile_is_linear(node.source);
      if (node.source.space.is_float() || node.source.linear) {
        state.any_float = true;
      } else if (node.source.space.sample == SampleType::U16) {
        state.any_sixteen = true;
      }
      if (node.source.space.model == ColorModel::Gray) state.any_gray = true;
      if (node.source.space.model == ColorModel::Cmyk) state.any_cmyk = true;
      if (node.source.space.model == ColorModel::Lab) state.any_lab = true;
      if (node.source.space.model == ColorModel::Unsupported || node.source.space.model == ColorModel::Alpha) {
        add_unique(state.unsupported_spaces, node.name + " (" + node.source.space.name + ")");
      }
    }
    decide_linear(node.children, state);
  }
}

// Reads every paint layer's tiles, spread over workers. The tile header pass (which takes
// pixels from the budget) runs in file order first, so the caps never depend on timing.
void decode_paint_layers(std::vector<Node>& nodes, BitDepth depth, ReadState& state) {
  std::vector<Node*> pending;
  collect_paint(nodes, pending);
  std::vector<std::string> reasons(pending.size());
  // Default pixels are tiny entries: read them up front, in order.
  std::vector<std::vector<std::uint8_t>> defaults(pending.size());
  for (std::size_t i = 0; i < pending.size(); ++i) {
    if (auto value = extract_small(state, state.prefix + "layers/" + pending[i]->tile_file + ".defaultpixel");
        value.has_value()) {
      defaults[i] = std::move(*value);
    }
  }
  // The pixel budget is taken in file order before any decoding, from each file's tile count
  // (64 x 64 tiles, the size every Krita writes; read_tiles checks each layer's own size).
  for (const auto* node : pending) {
    const auto& encoded = node->encoded;
    std::uint64_t tiles = 0;
    std::size_t pos = 0;
    for (int line = 0; line < 8 && pos < encoded.size(); ++line) {
      const auto end = std::find(encoded.begin() + static_cast<std::ptrdiff_t>(pos), encoded.end(), '\n');
      const std::string_view text(reinterpret_cast<const char*>(encoded.data() + pos),
                                  static_cast<std::size_t>(end - encoded.begin()) - pos);
      if (text.substr(0, 5) == "DATA ") {
        tiles = static_cast<std::uint64_t>(std::max(0.0, parse_number(text.substr(5)).value_or(0.0)));
        break;
      }
      pos = static_cast<std::size_t>(end - encoded.begin()) + 1;
    }
    const auto pixels = std::min<std::uint64_t>(tiles, 1ULL << 32) * 64U * 64U;
    if (pixels > state.pixel_budget) {
      throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The Krita file is too large to open"));
    }
    state.pixel_budget -= pixels;
  }
  std::vector<std::uint64_t> budgets(pending.size(), std::numeric_limits<std::uint64_t>::max());

  std::atomic<std::size_t> next{0};
  const auto decode_some = [&] {
    for (auto index = next.fetch_add(1); index < pending.size(); index = next.fetch_add(1)) {
      auto& node = *pending[index];
      auto raw = read_tiles(node.encoded, node.source.space.pixel_bytes(), defaults[index], node.x, node.y,
                            TileReadLimits{kMaxSide, kMaxImagePixels}, budgets[index], reasons[index]);
      std::vector<std::uint8_t>().swap(node.encoded);
      if (!raw.has_value()) {
        continue;
      }
      node.raw = std::move(raw);
    }
  };
  const int workers = max_blocking_fanout_workers(
      static_cast<int>(std::min<std::size_t>(pending.size(), static_cast<std::size_t>(hardware_worker_threads()))));
  if (workers < 2) {
    decode_some();
  } else {
    std::vector<std::future<void>> futures;
    for (int worker = 1; worker < workers; ++worker) {
      futures.push_back(std::async(std::launch::async, decode_some));
    }
    decode_some();
    for (auto& future : futures) {
      future.get();
    }
  }
  // Conversion runs in order on this thread: CMYK transforms are cached in the state.
  for (std::size_t i = 0; i < pending.size(); ++i) {
    auto& node = *pending[i];
    if (!node.raw.has_value()) {
      add_unique(state.unreadable, node.name + " (" + reasons[i] + ")");
      continue;
    }
    auto& raw = *node.raw;
    if (!raw.default_is_transparent) {
      // A non-transparent default pixel fills the whole layer (Krita's white Background):
      // cover the canvas as well as the stored tiles.
      const auto left = std::min(raw.bounds.width > 0 ? raw.bounds.x : 0, 0);
      const auto top = std::min(raw.bounds.height > 0 ? raw.bounds.y : 0, 0);
      const auto right = std::max(raw.bounds.width > 0 ? raw.bounds.x + raw.bounds.width : 0, state.width);
      const auto bottom = std::max(raw.bounds.height > 0 ? raw.bounds.y + raw.bounds.height : 0, state.height);
      RawImage grown;
      grown.pixel_bytes = raw.pixel_bytes;
      grown.default_pixel = raw.default_pixel;
      grown.default_is_transparent = false;
      grown.bounds = Rect{left, top, right - left, bottom - top};
      const auto pb = static_cast<std::size_t>(raw.pixel_bytes);
      grown.data.resize(static_cast<std::size_t>(grown.bounds.width) * grown.bounds.height * pb);
      for (std::size_t p = 0; p < grown.data.size(); p += pb) {
        std::memcpy(grown.data.data() + p, raw.default_pixel.data(), pb);
      }
      for (std::int32_t y = 0; y < raw.bounds.height; ++y) {
        std::memcpy(grown.data.data() +
                        (static_cast<std::size_t>(raw.bounds.y - top + y) * grown.bounds.width + (raw.bounds.x - left)) * pb,
                    raw.data.data() + static_cast<std::size_t>(y) * raw.bounds.width * pb,
                    static_cast<std::size_t>(raw.bounds.width) * pb);
      }
      raw = std::move(grown);
      node.covers_canvas = node.source.space.model != ColorModel::Unsupported &&
                           raw.default_pixel.size() == static_cast<std::size_t>(raw.pixel_bytes) &&
                           default_alpha_is_full(raw.default_pixel, node.source.space);
    }
    if (auto pixels = to_pixel_buffer(raw, node.source, depth, state); pixels.has_value()) {
      node.pixel_bounds = raw.bounds;
      node.pixels = std::move(pixels);
      if (!node.covers_canvas) {
        node.covers_canvas = opaque_over_canvas(*node.pixels, node.pixel_bounds, state.width, state.height);
      }
    } else {
      add_unique(state.unreadable, node.name + " (" + node.source.space.name + ")");
    }
    node.raw.reset();
  }
}

// ---------------------------------------------------------------------------------------
// Building Patchy layers.

struct BuildState {
  Document& document;
  BitDepth depth;
  ReadState& read;
  const std::filesystem::path& source_directory;
  std::map<std::string, LayerStyle> styles;
  std::map<std::string, const Node*> by_uuid;
  std::map<std::string, const Node*> by_name;
  std::optional<LayerId> selected;
  // Inside a pass-through group: whether what lies below the group is opaque everywhere,
  // which the group's layers see as their backdrop.
  bool backdrop_opaque{false};
  // Set by build_list for the group being built: whether the stack below it is opaque.
  bool group_backdrop_opaque{false};
};

[[nodiscard]] std::string normalized_uuid(std::string id) {
  id.erase(std::remove_if(id.begin(), id.end(), [](char ch) { return ch == '{' || ch == '}'; }), id.end());
  std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return id;
}

void index_nodes(const std::vector<Node>& nodes, BuildState& state) {
  for (const auto& node : nodes) {
    if (!node.uuid.empty()) {
      state.by_uuid[normalized_uuid(node.uuid)] = &node;
    }
    state.by_name.emplace(node.name, &node);
    index_nodes(node.children, state);
  }
}

void apply_common(Layer& layer, const Node& node, BuildState& state, bool group) {
  layer.set_opacity(node.opacity);
  layer.set_visible(node.visible);
  layer.set_color_label(color_label_from_krita(node.color_label));
  LayerLockFlags locks = kLayerLockNone;
  if (node.locked) {
    locks |= kLayerLockAll;
  }
  if (node.alpha_locked && !group) {
    locks |= kLayerLockTransparentPixels;
  }
  layer.set_lock_flags(locks);
  if (node.restricted_channels != 0) {
    layer.set_restricted_channels(node.restricted_channels);
  }
  const bool filter_like = node.type == NodeType::Filter;
  if (!filter_like || node.composite_op != "copy") {
    bool approximate = false;
    if (const auto mode = blend_mode_from_krita(node.composite_op, approximate); mode.has_value()) {
      layer.set_blend_mode(*mode);
      if (approximate) {
        add_unique(state.read.approximated_ops, node.name + " (" + node.composite_op + ")");
      }
    } else {
      add_unique(state.read.unknown_ops, node.name + " (" + node.composite_op + ")");
    }
  }
  if (group && node.passthrough) {
    layer.set_blend_mode(BlendMode::PassThrough);
  }
  if (!node.layer_style.empty()) {
    if (const auto found = state.styles.find(normalized_uuid(node.layer_style)); found != state.styles.end()) {
      layer.layer_style() = found->second;
    } else {
      add_unique(state.read.missing_styles, node.name);
    }
  }
  if (node.selected && !state.selected.has_value()) {
    state.selected = layer.id();
  }
}

// The layer's transparency masks, combined into one Patchy mask.
void apply_transparency_masks(Layer& layer, const Node& node, BuildState& state) {
  std::optional<LayerMask> combined;
  int count = 0;
  for (const auto& mask : node.masks) {
    if (mask.type != MaskType::Transparency || !mask.selection.has_value()) {
      continue;
    }
    auto converted = mask_from_selection(*mask.selection);
    if (!converted.has_value()) {
      continue;
    }
    if (!mask.visible) {
      converted->disabled = true;
    }
    ++count;
    combined = combined.has_value() ? multiply_masks(*combined, *converted) : std::move(*converted);
  }
  if (count > 1) {
    add_unique(state.read.merged_masks, node.name);
  }
  if (combined.has_value()) {
    layer.set_mask(std::move(*combined));
  }
}

[[nodiscard]] Layer placeholder_layer(const Node& node, BuildState& state) {
  Layer layer(state.document.allocate_layer_id(), node.name,
              PixelBuffer(0, 0, with_bit_depth(PixelFormat::rgba8(), state.depth)));
  apply_common(layer, node, state, false);
  return layer;
}

// A Krita filter or mask filter as a Patchy adjustment layer, when one maps.
[[nodiscard]] std::optional<Layer> adjustment_layer(const std::string& name, const FilterConfig& config,
                                                    const std::optional<RawImage>& selection, BuildState& state,
                                                    const std::string& owner_name) {
  bool approximate = false;
  BlendMode blend = BlendMode::Normal;
  const auto settings = adjustment_from_filter(config, approximate, blend);
  const auto label = owner_name.empty() ? name : owner_name + " / " + name;
  if (!settings.has_value()) {
    add_unique(state.read.unsupported_filters, label + " (" + config.name + ")");
    return std::nullopt;
  }
  if (approximate) {
    add_unique(state.read.approximated_filters, label + " (" + config.name + ")");
  }
  Layer layer(state.document.allocate_layer_id(), name, LayerKind::Adjustment);
  configure_adjustment_layer(layer, *settings);
  layer.set_blend_mode(blend);
  if (selection.has_value()) {
    if (auto mask = mask_from_selection(*selection); mask.has_value()) {
      layer.set_mask(std::move(*mask));
    }
  }
  return layer;
}

[[nodiscard]] std::vector<Layer> build_list(const std::vector<Node>& nodes, BuildState& state);

// A file layer's image beside the .kra, decoded by stb_image (PNG, JPEG, BMP, TGA, GIF).
[[nodiscard]] std::optional<PixelBuffer> load_file_layer(const Node& node, BuildState& state) {
  if (state.source_directory.empty() || node.file_source.empty()) {
    return std::nullopt;
  }
  std::filesystem::path relative;
  try {
    relative = std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(node.file_source.data()), node.file_source.size()));
  } catch (const std::exception&) {
    return std::nullopt;
  }
  const auto path = relative.is_absolute() ? relative : state.source_directory / relative;
  std::vector<std::uint8_t> bytes;
  try {
    bytes = formats::read_file_bytes(path, "Krita file layer");
  } catch (const std::exception&) {
    return std::nullopt;
  }
  int width = 0;
  int height = 0;
  int components = 0;
  const auto length = static_cast<int>(std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(INT32_MAX)));
  if (stbi_info_from_memory(bytes.data(), length, &width, &height, &components) == 0 || width <= 0 || height <= 0 ||
      width > kMaxSide || height > kMaxSide ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxImagePixels) {
    return std::nullopt;
  }
  stbi_uc* decoded = stbi_load_from_memory(bytes.data(), length, &width, &height, &components, 4);
  if (decoded == nullptr) {
    return std::nullopt;
  }
  PixelBuffer rgba8(width, height, PixelFormat::rgba8());
  std::memcpy(rgba8.data().data(), decoded, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  stbi_image_free(decoded);
  if (state.depth == BitDepth::UInt8) {
    return rgba8;
  }
  return convert_pixel_buffer_depth(rgba8, state.depth, SampleKind::Color);
}

[[nodiscard]] Layer build_layer(const Node& node, BuildState& state, std::vector<Layer>& extra_above) {
  const auto canvas = Rect::from_size(state.document.width(), state.document.height());
  switch (node.type) {
    case NodeType::Group: {
      Layer group(state.document.allocate_layer_id(), node.name, LayerKind::Group);
      // A pass-through group's layers see the stack below the group as their backdrop.
      const bool outer = state.backdrop_opaque;
      state.backdrop_opaque = node.passthrough && state.group_backdrop_opaque;
      auto children = build_list(node.children, state);
      state.backdrop_opaque = outer;
      for (auto& child : children) {
        group.add_child(std::move(child));
      }
      // Filter masks on a group filter what the group composites: an adjustment on top.
      for (auto it = node.masks.rbegin(); it != node.masks.rend(); ++it) {
        if (it->type == MaskType::Filter && it->config.has_value()) {
          if (auto adjustment = adjustment_layer(it->name, *it->config, it->selection, state, node.name);
              adjustment.has_value()) {
            adjustment->set_visible(it->visible);
            group.add_child(std::move(*adjustment));
          }
        }
      }
      apply_common(group, node, state, true);
      apply_transparency_masks(group, node, state);
      return group;
    }
    case NodeType::Paint:
    case NodeType::Clone:
    case NodeType::File: {
      std::optional<PixelBuffer> pixels;
      Rect bounds{};
      if (node.type == NodeType::Paint && node.pixels.has_value()) {
        pixels = node.pixels;
        bounds = node.pixel_bounds;
      } else if (node.type == NodeType::Clone) {
        const Node* source = nullptr;
        if (const auto found = state.by_uuid.find(normalized_uuid(node.clone_from_uuid)); found != state.by_uuid.end()) {
          source = found->second;
        } else if (const auto named = state.by_name.find(node.clone_from_name); named != state.by_name.end()) {
          source = named->second;
        }
        if (source != nullptr && source->type == NodeType::Paint && source->pixels.has_value()) {
          pixels = source->pixels;
          bounds = source->pixel_bounds;
          bounds.x += node.x;
          bounds.y += node.y;
          add_unique(state.read.clone_layers, node.name);
        } else {
          add_unique(state.read.unsupported_nodes, node.name + " (clone of " + node.clone_from_name + ")");
        }
      } else if (node.type == NodeType::File) {
        pixels = load_file_layer(node, state);
        if (pixels.has_value()) {
          bounds = Rect{node.x, node.y, pixels->width(), pixels->height()};
          add_unique(state.read.loaded_files, node.name + " (" + node.file_source + ")");
        } else {
          add_unique(state.read.missing_files, node.name + " (" + node.file_source + ")");
        }
      }
      if (!pixels.has_value()) {
        Layer layer = placeholder_layer(node, state);
        apply_transparency_masks(layer, node, state);
        return layer;
      }
      Layer layer(state.document.allocate_layer_id(), node.name, std::move(*pixels));
      layer.set_bounds(bounds);
      apply_common(layer, node, state, false);
      apply_transparency_masks(layer, node, state);
      // Filter masks filter this layer's own pixels: clipped adjustments right above it.
      for (auto it = node.masks.rbegin(); it != node.masks.rend(); ++it) {
        if (it->type == MaskType::Filter && it->config.has_value()) {
          if (auto adjustment = adjustment_layer(it->name, *it->config, it->selection, state, node.name);
              adjustment.has_value()) {
            adjustment->set_clipped(true);
            adjustment->set_visible(it->visible);
            extra_above.push_back(std::move(*adjustment));
          }
        }
      }
      return layer;
    }
    case NodeType::Filter: {
      if (node.config.has_value()) {
        if (auto adjustment = adjustment_layer(node.name, *node.config, node.selection, state, {}); adjustment.has_value()) {
          apply_common(*adjustment, node, state, false);
          apply_transparency_masks(*adjustment, node, state);
          return std::move(*adjustment);
        }
      }
      return placeholder_layer(node, state);
    }
    case NodeType::Generator: {
      std::optional<VectorFill> fill;
      if (node.config.has_value()) {
        fill = fill_from_generator(*node.config);
      }
      if (!fill.has_value()) {
        add_unique(state.read.unsupported_generators, node.name + " (" + node.filter_name + ")");
        return placeholder_layer(node, state);
      }
      Layer layer(state.document.allocate_layer_id(), node.name, PixelBuffer(0, 0, PixelFormat::rgba8()));
      VectorShapeContent content;
      content.fill = *fill;
      layer.set_vector_shape(std::move(content));
      layer.metadata()[kLayerMetadataVectorShape] = "1";
      update_vector_shape_raster(layer, canvas, &state.document.metadata().patterns);
      apply_common(layer, node, state, false);
      if (node.selection.has_value()) {
        if (auto mask = mask_from_selection(*node.selection); mask.has_value()) {
          layer.set_mask(std::move(*mask));
        }
      }
      return layer;
    }
    case NodeType::Vector: {
      std::vector<Layer> shapes;
      if (!node.svg.empty()) {
        std::vector<std::string> notices;
        try {
          shapes = read_vector_layer(node.svg, state.document, notices);
        } catch (const std::exception& error) {
          notices.emplace_back(error.what());
          shapes.clear();
        }
        for (const auto& notice : notices) {
          add_unique(state.read.vector_notices, node.name + ": " + notice);
        }
      }
      if (shapes.size() == 1 && shapes.front().kind() != LayerKind::Group && node.masks.empty() &&
          node.layer_style.empty()) {
        // One shape: the shape layer is the vector layer, with the vector layer's settings.
        Layer layer = std::move(shapes.front());
        const float shape_opacity = layer.opacity();
        layer.set_name(node.name);
        apply_common(layer, node, state, false);
        layer.set_opacity(shape_opacity * node.opacity);
        return layer;
      }
      Layer group(state.document.allocate_layer_id(), node.name, LayerKind::Group);
      for (auto& shape : shapes) {
        group.add_child(std::move(shape));
      }
      apply_common(group, node, state, true);
      apply_transparency_masks(group, node, state);
      return group;
    }
    case NodeType::Unknown:
      break;
  }
  add_unique(state.read.unsupported_nodes, node.name + " (" + node.nodetype + ")");
  return placeholder_layer(node, state);
}

// Whether a layer below `index` in a sibling list (uppermost first) covers the whole canvas
// opaquely at Normal (Krita's white Background: an opaque default pixel), so the backdrop an
// inherit-alpha layer above it sees is opaque everywhere and inheriting its alpha changes
// nothing.
[[nodiscard]] bool opaque_cover_below(const std::vector<Node>& nodes, std::size_t index) {
  for (auto i = index + 1; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    if (node.type == NodeType::Paint && node.covers_canvas && node.visible && node.opacity >= 1.0F &&
        node.masks.empty() && node.composite_op == "normal" && !node.inherit_alpha) {
      return true;
    }
  }
  return false;
}

// Whether `layers` (bottom first) is one clipping run: an unclipped base with only clipped
// layers above it. A layer clipped onto such a stack clips to everything below it.
[[nodiscard]] bool is_single_clip_run(const std::vector<Layer>& layers) {
  if (layers.empty() || layers.front().clipped()) {
    return false;
  }
  return std::all_of(layers.begin() + 1, layers.end(), [](const Layer& layer) { return layer.clipped(); });
}

// A group's content composited into one pixel layer that keeps the group's own settings.
// Patchy never clips a group (Photoshop's rule), so an Inherit Alpha group opens this way.
[[nodiscard]] Layer flattened_group(Layer group, BuildState& state) {
  Document content(state.document.width(), state.document.height(), state.document.format());
  content.color_state().bit_depth = state.depth;
  content.metadata().patterns = state.document.metadata().patterns;
  for (auto& child : group.children()) {
    content.add_layer(std::move(child));
  }
  PixelBuffer pixels = state.depth == BitDepth::UInt8 ? flatten_document_rgba8(content)
                                                      : Compositor().flatten_rgba_deep(content);
  Layer layer(state.document.allocate_layer_id(), group.name(), std::move(pixels));
  layer.set_bounds(Rect::from_size(state.document.width(), state.document.height()));
  layer.set_opacity(group.opacity());
  layer.set_visible(group.visible());
  layer.set_blend_mode(group.blend_mode() == BlendMode::PassThrough ? BlendMode::Normal : group.blend_mode());
  layer.set_lock_flags(group.lock_flags());
  layer.set_color_label(group.color_label());
  if (group.mask().has_value()) {
    layer.set_mask(*group.mask());
  }
  layer.layer_style() = group.layer_style();
  return layer;
}

std::vector<Layer> build_list(const std::vector<Node>& nodes, BuildState& state) {
  // Built bottom first. Inherit Alpha keeps the layer inside the alpha of everything below
  // it in the same stack. Patchy's clipping is to the first unclipped layer below, which is
  // the same when the stack below is one clipping run (a single layer, say). Otherwise the
  // layers below are grouped first, so the clip takes all of them, exactly as Krita draws
  // it. Above an opaque full-canvas layer there is nothing to inherit.
  std::vector<Layer> out;
  for (auto index = nodes.size(); index-- > 0;) {
    const auto& node = nodes[index];
    std::vector<Layer> above;
    const bool below_opaque = opaque_cover_below(nodes, index) || state.backdrop_opaque;
    state.group_backdrop_opaque = below_opaque;
    Layer layer = build_layer(node, state, above);
    if (node.inherit_alpha && out.empty() && !below_opaque) {
      // Nothing below in its own stack: Krita keeps nothing of it in an isolated group, and
      // in a pass-through group clips it to what is below the group, which Patchy cannot.
      add_unique(state.read.inherit_alpha_unclipped, node.name);
    }
    if (node.inherit_alpha && !out.empty() && !below_opaque) {
      if (!is_single_clip_run(out)) {
        Layer base(state.document.allocate_layer_id(), "Inherit Alpha base", LayerKind::Group);
        for (auto& below : out) {
          base.add_child(std::move(below));
        }
        out.clear();
        out.push_back(std::move(base));
        add_unique(state.read.inherit_alpha_grouped, node.name);
      }
      if (layer.kind() == LayerKind::Group) {
        layer = flattened_group(std::move(layer), state);
        add_unique(state.read.inherit_alpha_flattened, node.name);
      }
      layer.set_clipped(true);
    }
    out.push_back(std::move(layer));
    for (auto& extra : above) {
      out.push_back(std::move(extra));
    }
  }
  return out;
}

// Layer styles: Krita keeps them in one ASL file, each style named by the uuid a layer's
// "layerstyle" attribute refers to.
void read_styles(ReadState& read, BuildState& build) {
  auto bytes = read.zip.extract(read.prefix + "annotations/layerstyles.asl", kMaxEntryBytes);
  if (!bytes.has_value()) {
    return;
  }
  std::string error;
  const auto parsed = psd::read_asl(*bytes, error);
  if (!parsed.has_value()) {
    add_unique(read.style_notices, "layer styles could not be read (" + error + ")");
    return;
  }
  for (const auto& pattern : parsed->patterns) {
    build.document.metadata().patterns.adopt(pattern);
  }
  // Effects set to "use global light" take the document's light: Krita keeps Photoshop's
  // global angle and altitude resources (1037, 1049) as annotations when it opened a PSD;
  // otherwise Photoshop's defaults apply (120 and 30 degrees).
  const auto resource_degrees = [&](const char* id, float fallback) {
    auto bytes = read.zip.extract(read.prefix + "annotations/PSD Resource Block_ " + id, 64);
    if (!bytes.has_value() || bytes->size() < 4) {
      return fallback;
    }
    const auto value = static_cast<std::int32_t>((static_cast<std::uint32_t>((*bytes)[0]) << 24U) |
                                                 (static_cast<std::uint32_t>((*bytes)[1]) << 16U) |
                                                 (static_cast<std::uint32_t>((*bytes)[2]) << 8U) | (*bytes)[3]);
    return std::abs(value) <= 360 ? static_cast<float>(value) : fallback;
  };
  const auto angle = resource_degrees("1037", 120.0F);
  const auto altitude = resource_degrees("1049", 30.0F);
  for (const auto& style : parsed->styles) {
    auto resolved = style.style;
    for (auto& shadow : resolved.drop_shadows) {
      if (shadow.use_global_light) {
        shadow.angle_degrees = angle;
        shadow.use_global_light = false;
      }
    }
    for (auto& shadow : resolved.inner_shadows) {
      if (shadow.use_global_light) {
        shadow.angle_degrees = angle;
        shadow.use_global_light = false;
      }
    }
    for (auto& bevel : resolved.bevels) {
      if (bevel.use_global_light) {
        bevel.angle_degrees = angle;
        bevel.altitude_degrees = altitude;
        bevel.use_global_light = false;
      }
    }
    build.styles[normalized_uuid(style.id)] = std::move(resolved);
  }
  for (const auto& warning : parsed->warnings) {
    add_unique(read.style_notices, warning);
  }
}

void read_guides(const svg::XmlNode& image, Document& document, double xres, double yres) {
  const auto* guides = find_child(image, "guides");
  if (guides == nullptr) {
    return;
  }
  // Positions are in points (1/72 inch), whatever unit the rulers show.
  const auto read_list = [&](std::string_view name, GuideOrientation orientation, double resolution) {
    const auto* list = find_child(*guides, name);
    if (list == nullptr) {
      return;
    }
    for (const auto& item : list->children) {
      if (item.is_text()) {
        continue;
      }
      const auto value = parse_number(attribute_or(item, "value"));
      if (!value.has_value()) {
        continue;
      }
      const double pixels = *value * resolution / 72.0;
      if (std::abs(pixels) > kMaxGuideOffsetPixels) {
        continue;
      }
      document.guides().push_back(DocumentGuide{orientation, static_cast<std::int32_t>(std::lround(pixels * 32.0))});
    }
  };
  read_list("horizontalGuides", GuideOrientation::Horizontal, yres);
  read_list("verticalGuides", GuideOrientation::Vertical, xres);
}

// The folder every layer entry sits in: the image's name, or failing that the folder that
// holds a "layers/" directory.
[[nodiscard]] std::string find_prefix(formats::ZipEntryReader& zip, const std::string& image_name) {
  const auto names = zip.entry_names();
  const auto has_prefix = [&](const std::string& prefix) {
    return std::any_of(names.begin(), names.end(),
                       [&](const std::string& name) { return name.rfind(prefix, 0) == 0; });
  };
  if (!image_name.empty() && has_prefix(image_name + "/layers/")) {
    return image_name + "/";
  }
  for (const auto& name : names) {
    const auto at = name.find("/layers/");
    if (at != std::string::npos && name.find('/') == at) {
      return name.substr(0, at + 1);
    }
  }
  return image_name + "/";
}

[[nodiscard]] std::optional<PixelBuffer> decode_merged_image(formats::ZipEntryReader& zip, BitDepth depth,
                                                            std::uint64_t& budget) {
  auto bytes = zip.extract("mergedimage.png", kMaxEntryBytes);
  if (!bytes.has_value() || bytes->empty()) {
    return std::nullopt;
  }
  int width = 0;
  int height = 0;
  int components = 0;
  const auto length = static_cast<int>(std::min<std::size_t>(bytes->size(), static_cast<std::size_t>(INT32_MAX)));
  if (stbi_info_from_memory(bytes->data(), length, &width, &height, &components) == 0 || width <= 0 ||
      height <= 0 || width > kMaxSide || height > kMaxSide ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > std::min(kMaxImagePixels, budget)) {
    return std::nullopt;
  }
  budget -= static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  stbi_uc* decoded = stbi_load_from_memory(bytes->data(), length, &width, &height, &components, 4);
  if (decoded == nullptr) {
    return std::nullopt;
  }
  PixelBuffer rgba8(width, height, PixelFormat::rgba8());
  std::memcpy(rgba8.data().data(), decoded, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  stbi_image_free(decoded);
  return depth == BitDepth::UInt8 ? rgba8 : convert_pixel_buffer_depth(rgba8, depth, SampleKind::Color);
}

}  // namespace

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  constexpr std::string_view kHead = "PK\x03\x04";
  constexpr std::string_view kName = "mimetype";
  if (bytes.size() < 30 + kName.size() + kMimetype.size()) {
    return false;
  }
  const auto text = [&](std::size_t at, std::size_t length) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data() + at), length);
  };
  const auto name_length = static_cast<std::size_t>(bytes[26] | (bytes[27] << 8));
  const auto extra_length = static_cast<std::size_t>(bytes[28] | (bytes[29] << 8));
  const auto method = bytes[8] | (bytes[9] << 8);
  const auto content = 30 + name_length + extra_length;
  return text(0, 4) == kHead && method == 0 && name_length == kName.size() && text(30, kName.size()) == kName &&
         bytes.size() >= content + kMimetype.size() && text(content, kMimetype.size()) == kMimetype;
}

const std::vector<std::string>& kra_extensions() {
  static const std::vector<std::string> extensions{".kra", ".krz"};
  return extensions;
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices,
                          const std::filesystem::path& source_directory) {
  if (bytes.size() < 4 || bytes[0] != 'P' || bytes[1] != 'K') {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not a Krita document"));
  }
  formats::ZipEntryReader zip(bytes, zip_messages(), {kMaxEntryBytes, kMaxTotalExtracted, kMaxEntries});
  if (auto mimetype = zip.extract("mimetype", 4096); mimetype.has_value()) {
    std::string text(mimetype->begin(), mimetype->end());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
      text.pop_back();
    }
    if (text != kMimetype) {
      throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not a Krita document"));
    }
  }
  auto maindoc = zip.extract("maindoc.xml", kMaxXmlBytes);
  if (!maindoc.has_value()) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not a Krita document"));
  }
  svg::XmlNode root;
  try {
    root = svg::parse_xml(*maindoc);
  } catch (const std::exception&) {
    throw read_error(damaged_tree_message());
  }
  maindoc.reset();
  const svg::XmlNode* image = local_name(root.name) == "DOC" ? find_child(root, "IMAGE") : nullptr;
  if (image == nullptr) {
    throw read_error(damaged_tree_message());
  }
  ReadState state(zip);
  state.width = attribute_int(*image, "width", 0);
  state.height = attribute_int(*image, "height", 0);
  if (state.width <= 0 || state.height <= 0) {
    throw read_error(damaged_tree_message());
  }
  if (static_cast<std::uint64_t>(state.width) * static_cast<std::uint64_t>(state.height) > kMaxTotalPixels) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The Krita file is too large to open"));
  }
  const auto xres = parse_number(attribute_or(*image, "x-res", "72")).value_or(72.0);
  const auto yres = parse_number(attribute_or(*image, "y-res", "72")).value_or(72.0);
  state.xres = xres >= 1.0 && xres <= 1e6 ? xres : 72.0;
  state.yres = yres >= 1.0 && yres <= 1e6 ? yres : 72.0;
  state.image_space = parse_color_space(attribute_or(*image, "colorspacename", "RGBA"));
  state.prefix = find_prefix(zip, attribute_or(*image, "name"));
  if (const auto* animation = find_child(*image, "animation"); animation != nullptr) {
    if (const auto* current = find_child(*animation, "currentTime"); current != nullptr) {
      state.current_time = attribute_int(*current, "value", 0);
    }
  }
  if (auto icc = zip.extract(state.prefix + "annotations/icc", kMaxSmallEntryBytes); icc.has_value()) {
    state.image_icc = std::move(*icc);
  }

  std::vector<Node> nodes;
  if (const auto* layers = find_child(*image, "layers"); layers != nullptr) {
    read_layers(*layers, nodes, state, nullptr, 0);
  }
  decide_linear(nodes, state);
  inline_pass_through_groups(nodes, state);
  SampleSource image_source{state.image_space, state.image_icc, false};
  image_source.linear = profile_is_linear(image_source);

  BitDepth depth = state.any_float ? BitDepth::Float32 : state.any_sixteen ? BitDepth::UInt16 : BitDepth::UInt8;
  const bool deep_off = depth != BitDepth::UInt8 && !deep_editing_enabled();
  if (deep_off) {
    depth = BitDepth::UInt8;
  } else if (depth == BitDepth::Float32 && !depth_supported_on_platform(depth)) {
    depth = BitDepth::UInt16;
  }
  decode_paint_layers(nodes, depth, state);

  Document document(state.width, state.height, with_bit_depth(PixelFormat::rgba8(), depth));
  document.color_state().bit_depth = depth;
  document.print_settings().horizontal_ppi = state.xres;
  document.print_settings().vertical_ppi = state.yres;
  // The file's RGB profile, when it is not sRGB: the open path converts the pixels
  // (docs/rgb-profiles.md). Linear-light samples are already Patchy's 32-bit values, so
  // their profile only matters when its primaries are not sRGB's.
  if (state.image_space.model == ColorModel::Rgb && !state.image_icc.empty() && is_usable_rgb_profile(state.image_icc) &&
      !is_srgb_profile(state.image_icc)) {
    document.color_state().embedded_icc_profile = state.image_icc;
  }

  BuildState build{document, depth, state, source_directory, {}, {}, {}, std::nullopt};
  read_styles(state, build);
  index_nodes(nodes, build);
  for (auto& layer : build_list(nodes, build)) {
    document.add_layer(std::move(layer));
  }
  read_guides(*image, document, state.xres, state.yres);

  if (document.layers().empty()) {
    // No layers: Krita's own render (or a transparent layer) keeps the document openable.
    if (auto merged = decode_merged_image(zip, depth, state.pixel_budget); merged.has_value() &&
        merged->width() == state.width && merged->height() == state.height) {
      document.add_pixel_layer("Background", std::move(*merged));
    } else {
      document.add_pixel_layer("Layer 1", PixelBuffer(state.width, state.height,
                                                      with_bit_depth(PixelFormat::rgba8(), depth)));
    }
  }
  if (build.selected.has_value()) {
    document.set_active_layer(*build.selected);
  }

  if (notices != nullptr) {
    const auto note = [&](const std::vector<std::string>& items, const std::string& text) {
      if (!items.empty()) {
        notices->push_back(text + joined(items));
      }
    };
    note(state.unknown_ops, "Krita blend modes Patchy does not have opened as Normal: ");
    note(state.approximated_ops, "Krita blend modes opened as the nearest Patchy mode: ");
    note(state.unreadable, "Layers whose pixels could not be read opened empty: ");
    note(state.unsupported_spaces, "Layers in a color space Patchy cannot read opened empty: ");
    note(state.unsupported_nodes, "Krita layer types Patchy does not have opened as empty layers: ");
    note(state.unsupported_filters, "Krita filters Patchy has no adjustment for were left out: ");
    note(state.approximated_filters, "Krita filters opened as the nearest Patchy adjustment: ");
    note(state.unsupported_generators, "Krita fill layers Patchy cannot draw opened as empty layers: ");
    note(state.merged_masks, "Several transparency masks were combined into one layer mask: ");
    note(state.inherit_alpha_grouped,
         "Layers below an Inherit Alpha layer were grouped as \"Inherit Alpha base\" so the layer clips to all of "
         "them, as in Krita: ");
    note(state.inherit_alpha_flattened,
         "Groups with Inherit Alpha were merged into one clipped layer (Patchy cannot clip a group): ");
    note(state.inlined_groups,
         "Pass-through groups whose bottom layer inherits alpha opened as their layers, so it clips to the layers "
         "below: ");
    note(state.inherit_alpha_unclipped,
         "Inherit Alpha layers with nothing below them in their group opened unclipped: ");
    note(state.clone_layers, "Clone layers opened as copies of their source layer: ");
    note(state.loaded_files, "File layers were loaded from their files as pixel layers: ");
    note(state.missing_files, "File layers whose file was not found opened empty: ");
    note(state.missing_styles, "Layer styles that are missing from the file were left out: ");
    note(state.style_notices, "Layer styles: ");
    note(state.vector_notices, "Vector layers: ");
    std::vector<std::string> unsupported_masks;
    const auto collect = [&](auto&& self, const std::vector<Node>& list) -> void {
      for (const auto& node : list) {
        for (const auto& mask : node.masks) {
          if (mask.type == MaskType::Selection) {
            add_unique(unsupported_masks, node.name + " / " + mask.name + " (selection mask)");
          } else if (mask.type == MaskType::Transform || mask.type == MaskType::Colorize ||
                     mask.type == MaskType::Unknown) {
            add_unique(unsupported_masks, node.name + " / " + mask.name + " (" + mask.nodetype + ")");
          }
        }
        self(self, node.children);
      }
    };
    collect(collect, nodes);
    note(unsupported_masks, "Krita masks Patchy does not have were left out: ");
    if (state.animated) {
      notices->push_back("Animated layers opened at the current frame; their other frames were left out");
    }
    if (state.any_gray) {
      notices->push_back("Grayscale layers opened as RGB");
    }
    if (state.any_cmyk) {
      notices->push_back("CMYK layers were converted to RGB through their color profile");
    }
    if (state.any_lab) {
      notices->push_back("Lab layers were converted to RGB");
    }
    if (deep_off) {
      notices->push_back("16-bit and 32-bit layers were converted to 8 bits per channel");
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  return read(formats::read_file_bytes(path, "Krita"), notices, path.parent_path());
}

}  // namespace patchy::kra
