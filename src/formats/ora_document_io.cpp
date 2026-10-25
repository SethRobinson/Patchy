// OpenRaster (.ora) reader and writer (docs/ora.md). Written from the published spec
// (https://www.openraster.org, layer stack and file layout pages) and from the files GIMP 3
// and Krita 5 write; no other application's source was consulted.
//
// Ordering: stack.xml lists the uppermost element first, Patchy's layers()[0] is the
// bottom layer, so both directions reverse each sibling list.
//
// Determinism: the zip container is written here (fixed 1980-01-01 timestamps, fixed
// entry order, no extra fields outside Zip64), PNGs go through one encoder with a fixed
// filter rule and deflate level, numbers are formatted in the classic locale, and the
// layer files are numbered in document order. Two writes of one document are identical.

#include "formats/ora_document_io.hpp"

#include "core/artboard.hpp"
#include "core/document_depth.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "core/smart_object.hpp"
#include "core/vector_shape.hpp"
#include "core/worker_budget.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/stb/stb_image.h"
#include "formats/svg_xml.hpp"
#include "formats/zip_entry_reader.hpp"
#include "formats/vector_export_plan.hpp"
#include "render/compositor.hpp"
#include "render/layer_knockout.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <future>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace patchy::ora {

namespace {

constexpr std::string_view kMimetype = "image/openraster";

// Hostile-input caps (docs/ora.md "Limits"). A canvas or layer side past the PSB limit, an
// image past 2^28 pixels (stb_image decodes at most 2^30 bytes of RGBA), more than 2^31
// decoded pixels in all, or more than 4 GiB of extracted entries is refused before anything
// is allocated for it.
constexpr std::int64_t kMaxSide = 300000;
constexpr std::uint64_t kMaxImagePixels = 1ULL << 28;
constexpr std::uint64_t kMaxTotalPixels = 1ULL << 31;
constexpr std::uint64_t kMaxEntryBytes = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
constexpr std::uint64_t kMaxTotalExtracted = 1ULL << 32;
constexpr std::uint64_t kMaxStackXmlBytes = 64ULL << 20;
constexpr std::size_t kMaxEntries = 200000;
constexpr int kThumbnailSide = 256;
constexpr int kPngDeflateLevel = 6;

// ---------------------------------------------------------------------------------------
// Blend modes

struct CompositeOp {
  std::string_view name;
  BlendMode mode;
};

// The spec's blending functions with source-over, svg:plus as Linear Dodge (Add), and the
// names Krita and Drawpile write for modes the spec does not list (seen in their files).
constexpr std::array kReadOps = {
    CompositeOp{"svg:src-over", BlendMode::Normal},
    CompositeOp{"svg:multiply", BlendMode::Multiply},
    CompositeOp{"svg:screen", BlendMode::Screen},
    CompositeOp{"svg:overlay", BlendMode::Overlay},
    CompositeOp{"svg:darken", BlendMode::Darken},
    CompositeOp{"svg:lighten", BlendMode::Lighten},
    CompositeOp{"svg:color-dodge", BlendMode::ColorDodge},
    CompositeOp{"svg:color-burn", BlendMode::ColorBurn},
    CompositeOp{"svg:hard-light", BlendMode::HardLight},
    CompositeOp{"svg:soft-light", BlendMode::SoftLight},
    CompositeOp{"svg:difference", BlendMode::Difference},
    CompositeOp{"svg:exclusion", BlendMode::Exclusion},
    CompositeOp{"svg:color", BlendMode::Color},
    CompositeOp{"svg:luminosity", BlendMode::Luminosity},
    CompositeOp{"svg:hue", BlendMode::Hue},
    CompositeOp{"svg:saturation", BlendMode::Saturation},
    CompositeOp{"svg:plus", BlendMode::LinearDodge},
    CompositeOp{"krita:soft_light", BlendMode::SoftLight},
    CompositeOp{"krita:exclusion", BlendMode::Exclusion},
    CompositeOp{"krita:linear_burn", BlendMode::LinearBurn},
    CompositeOp{"krita:linear_dodge", BlendMode::LinearDodge},
    CompositeOp{"krita:vivid_light", BlendMode::VividLight},
    CompositeOp{"krita:linear light", BlendMode::LinearLight},
    CompositeOp{"krita:pin_light", BlendMode::PinLight},
    CompositeOp{"krita:hard_mix_photoshop", BlendMode::HardMix},
    CompositeOp{"krita:subtract", BlendMode::Subtract},
    CompositeOp{"krita:divide", BlendMode::Divide},
    CompositeOp{"krita:dissolve", BlendMode::Dissolve},
    CompositeOp{"krita:darker color", BlendMode::DarkerColor},
    CompositeOp{"krita:lighter color", BlendMode::LighterColor},
    CompositeOp{"krita:soft_light_svg", BlendMode::SoftLightSvg},
    CompositeOp{"krita:additive_subtractive", BlendMode::AdditiveSubtractive},
    CompositeOp{"krita:arc_tangent", BlendMode::ArcTangent},
    CompositeOp{"krita:geometric_mean", BlendMode::GeometricMean},
    CompositeOp{"krita:allanon", BlendMode::Allanon},
    CompositeOp{"krita:parallel", BlendMode::Parallel},
    CompositeOp{"krita:hard overlay", BlendMode::HardOverlay},
    CompositeOp{"krita:interpolation", BlendMode::Interpolation},
    CompositeOp{"krita:easy dodge", BlendMode::EasyDodge},
    // Drawpile's own extension names (seen in its test recordings).
    CompositeOp{"svg:-dp-divide", BlendMode::Divide},
    CompositeOp{"svg:-dp-minus", BlendMode::Subtract},
};

[[nodiscard]] std::optional<BlendMode> blend_mode_from_op(std::string_view op) {
  for (const auto& entry : kReadOps) {
    if (entry.name == op) {
      return entry.mode;
    }
  }
  return std::nullopt;
}

// The op a blend mode writes as: a spec name where one exists, Krita's name otherwise
// (krita = true), nothing for modes neither has (they merge with the layers below).
struct WriteOp {
  std::string_view name;
  bool krita{false};
};

[[nodiscard]] std::optional<WriteOp> op_for_blend_mode(BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
    case BlendMode::Normal:
      return WriteOp{"svg:src-over"};
    case BlendMode::Multiply:
      return WriteOp{"svg:multiply"};
    case BlendMode::Screen:
      return WriteOp{"svg:screen"};
    case BlendMode::Overlay:
      return WriteOp{"svg:overlay"};
    case BlendMode::Darken:
      return WriteOp{"svg:darken"};
    case BlendMode::Lighten:
      return WriteOp{"svg:lighten"};
    case BlendMode::ColorDodge:
      return WriteOp{"svg:color-dodge"};
    case BlendMode::ColorBurn:
      return WriteOp{"svg:color-burn"};
    case BlendMode::HardLight:
      return WriteOp{"svg:hard-light"};
    case BlendMode::SoftLight:
      return WriteOp{"svg:soft-light"};
    case BlendMode::Difference:
      return WriteOp{"svg:difference"};
    case BlendMode::Hue:
      return WriteOp{"svg:hue"};
    case BlendMode::Saturation:
      return WriteOp{"svg:saturation"};
    case BlendMode::Color:
      return WriteOp{"svg:color"};
    case BlendMode::Luminosity:
      return WriteOp{"svg:luminosity"};
    case BlendMode::LinearDodge:
      return WriteOp{"svg:plus"};
    case BlendMode::Exclusion:
      return WriteOp{"krita:exclusion", true};
    case BlendMode::LinearBurn:
      return WriteOp{"krita:linear_burn", true};
    case BlendMode::VividLight:
      return WriteOp{"krita:vivid_light", true};
    case BlendMode::LinearLight:
      return WriteOp{"krita:linear light", true};
    case BlendMode::PinLight:
      return WriteOp{"krita:pin_light", true};
    case BlendMode::HardMix:
      return WriteOp{"krita:hard_mix_photoshop", true};
    case BlendMode::Subtract:
      return WriteOp{"krita:subtract", true};
    case BlendMode::Divide:
      return WriteOp{"krita:divide", true};
    case BlendMode::Dissolve:
      return WriteOp{"krita:dissolve", true};
    case BlendMode::DarkerColor:
      return WriteOp{"krita:darker color", true};
    case BlendMode::LighterColor:
      return WriteOp{"krita:lighter color", true};
    case BlendMode::SoftLightSvg:
      return WriteOp{"krita:soft_light_svg", true};
    case BlendMode::AdditiveSubtractive:
      return WriteOp{"krita:additive_subtractive", true};
    case BlendMode::ArcTangent:
      return WriteOp{"krita:arc_tangent", true};
    case BlendMode::GeometricMean:
      return WriteOp{"krita:geometric_mean", true};
    case BlendMode::Allanon:
      return WriteOp{"krita:allanon", true};
    case BlendMode::Parallel:
      return WriteOp{"krita:parallel", true};
    case BlendMode::HardOverlay:
      return WriteOp{"krita:hard overlay", true};
    case BlendMode::Interpolation:
      return WriteOp{"krita:interpolation", true};
    case BlendMode::EasyDodge:
      return WriteOp{"krita:easy dodge", true};
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
      return std::nullopt;  // Paint.NET's and GIMP's modes: no name GIMP's ORA writer keeps
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------------------
// Numbers (classic locale both ways: an app locale with a decimal comma must not leak in)

[[nodiscard]] std::optional<double> parse_number(std::string_view text) {
  std::istringstream stream{std::string(text)};
  stream.imbue(std::locale::classic());
  double value = 0.0;
  stream >> value;
  if (stream.fail() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::int64_t> parse_integer(std::string_view text) {
  const auto value = parse_number(text);
  if (!value.has_value() || std::abs(*value) > 1e15) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(std::llround(*value));
}

// The shortest classic-locale form (up to 9 significant digits) that reads back as the
// same float, so opacities round-trip exactly.
[[nodiscard]] std::string format_opacity(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  std::string text;
  for (int precision = 1; precision <= 9; ++precision) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(precision) << static_cast<double>(value);
    text = stream.str();
    const auto back = parse_number(text);
    if (back.has_value() && static_cast<float>(*back) == value) {
      break;
    }
  }
  return text;
}

// ---------------------------------------------------------------------------------------
// XML text

void append_escaped(std::string& out, std::string_view text) {
  for (const char raw : text) {
    const auto ch = static_cast<unsigned char>(raw);
    switch (raw) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&apos;";
        break;
      case '\t':
        out += "&#9;";
        break;
      case '\n':
        out += "&#10;";
        break;
      case '\r':
        out += "&#13;";
        break;
      default:
        if (ch >= 0x20 && ch != 0x7F) {
          out.push_back(raw);
        }
        // Other C0 controls cannot appear in XML 1.0 at all; they are dropped.
        break;
    }
  }
}

void append_attribute(std::string& out, std::string_view name, std::string_view value) {
  out.push_back(' ');
  out += name;
  out += "=\"";
  append_escaped(out, value);
  out.push_back('"');
}

// ---------------------------------------------------------------------------------------
// PNG

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24));
  out.push_back(static_cast<std::uint8_t>(value >> 16));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

void append_png_chunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data) {
  append_be32(out, static_cast<std::uint32_t>(data.size()));
  const auto crc_start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  append_be32(out, static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, out.data() + crc_start, out.size() - crc_start)));
}

// One row of the buffer as big-endian RGBA samples (PNG color type 6).
void png_row(const PixelBuffer& pixels, std::int32_t y, bool sixteen, std::vector<std::uint8_t>& out) {
  const auto format = pixels.format();
  const auto channels = static_cast<int>(format.channels);
  const auto row = pixels.row(y);
  const auto width = pixels.width();
  std::size_t at = 0;
  for (std::int32_t x = 0; x < width; ++x) {
    std::array<std::uint16_t, 4> rgba{};
    for (int c = 0; c < channels && c < 4; ++c) {
      const auto index = static_cast<std::size_t>(x) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c);
      if (format.bit_depth == BitDepth::UInt16) {
        std::uint16_t value = 0;
        std::memcpy(&value, row.data() + index * 2U, sizeof(value));
        rgba[static_cast<std::size_t>(c)] = value;
      } else {
        rgba[static_cast<std::size_t>(c)] = row[index];
      }
    }
    if (channels <= 2) {  // gray (+ alpha)
      rgba[3] = channels == 2 ? rgba[1] : 0;
      rgba[1] = rgba[0];
      rgba[2] = rgba[0];
    }
    if (channels == 1 || channels == 3) {
      rgba[3] = format.bit_depth == BitDepth::UInt16 ? 0xFFFF : 0xFF;
    }
    for (const auto value : rgba) {
      if (sixteen) {
        out[at++] = static_cast<std::uint8_t>(value >> 8);
        out[at++] = static_cast<std::uint8_t>(value);
      } else {
        out[at++] = static_cast<std::uint8_t>(value);
      }
    }
  }
}

[[nodiscard]] std::uint8_t paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) {
    return static_cast<std::uint8_t>(a);
  }
  return static_cast<std::uint8_t>(pb <= pc ? b : c);
}

// ---------------------------------------------------------------------------------------
// Zip

void put16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}
void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

constexpr std::uint32_t kDosDate19800101 = (0U << 9) | (1U << 5) | 1U;
constexpr std::uint64_t kZip32Limit = 0xFFFFFFFFULL;

[[nodiscard]] bool name_is_ascii(std::string_view name) {
  return std::all_of(name.begin(), name.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x80; });
}

// ---------------------------------------------------------------------------------------
// Reading

[[nodiscard]] std::runtime_error read_error(const char* message) {
  return std::runtime_error(message);
}

struct DecodedImage {
  std::int32_t width{0};
  std::int32_t height{0};
  bool sixteen{false};
  std::vector<std::uint8_t> rgba8;
  std::vector<std::uint16_t> rgba16;
};

// The OpenRaster messages and caps for the shared zip reader.
[[nodiscard]] formats::ZipReaderMessages zip_messages() {
  return {PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster file is damaged: its zip directory is missing or cut short"),
          PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster file is too large to open"),
          PATCHY_TRANSLATE_NOOP(
              "QObject", "The OpenRaster file uses zip features Patchy cannot read (encryption or an unknown compression)"),
          PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster file is damaged: an entry cannot be unpacked")};
}

class ZipReader : public formats::ZipEntryReader {
public:
  explicit ZipReader(std::span<const std::uint8_t> bytes)
      : formats::ZipEntryReader(bytes, zip_messages(), {kMaxEntryBytes, kMaxTotalExtracted, kMaxEntries}) {}
};

// Checks PNG (or JPEG) bytes before any decoding: false with a reason when the bytes are
// not an image stb_image reads; throws past the size caps; takes the image's pixels from the
// budget. Runs in file order, so the caps and the budget never depend on thread timing.
[[nodiscard]] bool check_image(std::span<const std::uint8_t> bytes, std::uint64_t& pixel_budget, std::string& reason) {
  if (bytes.empty() || bytes.size() > kMaxEntryBytes) {
    reason = "empty";
    return false;
  }
  const auto* data = bytes.data();
  const auto length = static_cast<int>(bytes.size());
  // The size check reads the PNG header itself: stb_image refuses an image past its own
  // limit without saying how large it is.
  constexpr std::array<std::uint8_t, 16> kPngStart = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                                      0, 0, 0, 13, 'I', 'H', 'D', 'R'};
  if (bytes.size() >= 24 && std::equal(kPngStart.begin(), kPngStart.end(), bytes.begin())) {
    const auto be32 = [&](std::size_t at) {
      return (static_cast<std::uint64_t>(bytes[at]) << 24) | (static_cast<std::uint64_t>(bytes[at + 1]) << 16) |
             (static_cast<std::uint64_t>(bytes[at + 2]) << 8) | bytes[at + 3];
    };
    const auto claimed_width = be32(16);
    const auto claimed_height = be32(20);
    if (claimed_width > static_cast<std::uint64_t>(kMaxSide) || claimed_height > static_cast<std::uint64_t>(kMaxSide) ||
        claimed_width * claimed_height > kMaxImagePixels || claimed_width * claimed_height > pixel_budget) {
      throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster file is too large to open"));
    }
  }
  int width = 0;
  int height = 0;
  int components = 0;
  if (stbi_info_from_memory(data, length, &width, &height, &components) == 0 || width <= 0 || height <= 0) {
    reason = "not a readable PNG";
    return false;
  }
  const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (width > kMaxSide || height > kMaxSide || pixels > kMaxImagePixels || pixels > pixel_budget) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster file is too large to open"));
  }
  pixel_budget -= pixels;
  return true;
}

// Decodes bytes check_image accepted, as RGBA at their own depth; nullopt with a reason when
// stb_image fails on the data. Thread-safe: layers decode in parallel.
[[nodiscard]] std::optional<DecodedImage> decode_checked_image(std::span<const std::uint8_t> bytes,
                                                               std::string& reason) {
  const auto* data = bytes.data();
  const auto length = static_cast<int>(bytes.size());
  int width = 0;
  int height = 0;
  int components = 0;
  if (stbi_info_from_memory(data, length, &width, &height, &components) == 0 || width <= 0 || height <= 0) {
    reason = "not a readable PNG";
    return std::nullopt;
  }
  const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  DecodedImage image;
  image.width = width;
  image.height = height;
  image.sixteen = stbi_is_16_bit_from_memory(data, length) != 0;
  const auto count = static_cast<std::size_t>(pixels) * 4U;
  if (image.sixteen) {
    stbi_us* decoded = stbi_load_16_from_memory(data, length, &width, &height, &components, 4);
    if (decoded == nullptr) {
      reason = "a damaged PNG";
      return std::nullopt;
    }
    image.rgba16.assign(decoded, decoded + count);
    stbi_image_free(decoded);
  } else {
    stbi_uc* decoded = stbi_load_from_memory(data, length, &width, &height, &components, 4);
    if (decoded == nullptr) {
      reason = "a damaged PNG";
      return std::nullopt;
    }
    image.rgba8.assign(decoded, decoded + count);
    stbi_image_free(decoded);
  }
  return image;
}

[[nodiscard]] std::optional<DecodedImage> decode_image(std::span<const std::uint8_t> bytes, std::uint64_t& pixel_budget,
                                                       std::string& reason) {
  if (!check_image(bytes, pixel_budget, reason)) {
    return std::nullopt;
  }
  return decode_checked_image(bytes, reason);
}

[[nodiscard]] PixelBuffer image_to_buffer(const DecodedImage& image, BitDepth depth) {
  PixelBuffer buffer(image.width, image.height, with_bit_depth(PixelFormat::rgba8(), depth));
  auto bytes = buffer.data();
  const auto count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4U;
  if (depth == BitDepth::UInt16) {
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint16_t value = image.sixteen ? image.rgba16[i] : widen_u8_to_u16(image.rgba8[i]);
      std::memcpy(bytes.data() + i * 2U, &value, sizeof(value));
    }
  } else if (image.sixteen) {
    for (std::size_t i = 0; i < count; ++i) {
      bytes[i] = narrow_u16_to_u8(image.rgba16[i]);
    }
  } else {
    std::memcpy(bytes.data(), image.rgba8.data(), count);
  }
  return buffer;
}

// The stack.xml tree read into plain records first, so the document depth is known
// (any 16-bit layer makes a 16-bit document) before any PixelBuffer is made.
struct Node {
  bool group{false};
  std::string name;
  std::int64_t x{0};
  std::int64_t y{0};
  float opacity{1.0F};
  bool visible{true};
  BlendMode blend{BlendMode::Normal};
  bool clipped{false};
  bool selected{false};
  LayerLockFlags locks{kLayerLockNone};
  std::optional<DecodedImage> image;
  std::vector<std::uint8_t> encoded;  // the checked layer file, until the parallel decode
  std::string src;
  std::vector<Node> children;  // uppermost first, as in the file
};

struct ReadState {
  explicit ReadState(ZipReader& reader) : zip(reader) {}

  ZipReader& zip;
  std::uint64_t pixel_budget{kMaxTotalPixels};
  std::vector<std::string> unknown_ops;
  std::vector<std::string> approximated_ops;
  std::vector<std::string> missing_files;
  std::vector<std::string> unreadable_files;
  std::vector<std::string> unsafe_paths;
  int skipped_elements{0};
  int layer_count{0};
  int group_count{0};
  bool any_sixteen{false};
};

void add_unique(std::vector<std::string>& list, std::string value) {
  if (std::find(list.begin(), list.end(), value) == list.end()) {
    list.push_back(std::move(value));
  }
}

[[nodiscard]] std::string attribute_or(const svg::XmlNode& node, std::string_view name, std::string fallback) {
  const auto* value = node.attribute(name);
  return value != nullptr ? *value : std::move(fallback);
}

void read_common(const svg::XmlNode& element, Node& node, ReadState& state) {
  if (const auto* opacity = element.attribute("opacity"); opacity != nullptr) {
    if (const auto value = parse_number(*opacity); value.has_value()) {
      node.opacity = static_cast<float>(std::clamp(*value, 0.0, 1.0));
    }
  }
  node.visible = attribute_or(element, "visibility", "visible") != "hidden";
  node.selected = attribute_or(element, "selected", "false") == "true";
  // edit-locked (the spec's extension, an xsd:boolean on layers; GIMP and Krita also write it
  // on stacks) opens as Lock Image Pixels: GIMP writes it for its content lock.
  const auto locked = attribute_or(element, "edit-locked", "false");
  if (locked == "true" || locked == "1") {
    node.locks |= kLayerLockImagePixels;
  }
  const auto op = attribute_or(element, "composite-op", "svg:src-over");
  // alpha-preserve (a proposal Krita implements) is not a lock: the layer blends without
  // changing the backdrop's alpha, which Krita renders like source-atop. It opens as a
  // clipped layer with its own blend mode, under the same base rule as svg:src-atop.
  const auto preserve = attribute_or(element, "alpha-preserve", "false");
  if (preserve == "true" || preserve == "1") {
    node.clipped = true;
  }
  if (const auto mode = blend_mode_from_op(op); mode.has_value()) {
    node.blend = *mode;
  } else if (op == "svg:src-atop") {
    // Source-atop over the layers below in the same stack is Photoshop's clipping when
    // the layer below is the bottom of its stack (fixed up once the siblings are known).
    node.clipped = true;
  } else if (op == "svg:dst-in" || op == "svg:dst-out" || op == "svg:dst-atop") {
    add_unique(state.approximated_ops, node.name + " (" + op + ")");
  } else {
    add_unique(state.unknown_ops, node.name + " (" + op + ")");
  }
}

void read_stack(const svg::XmlNode& stack, std::vector<Node>& out, ReadState& state, std::int64_t offset_x,
                std::int64_t offset_y, int depth);

void read_layer_element(const svg::XmlNode& element, std::vector<Node>& out, ReadState& state, std::int64_t offset_x,
                        std::int64_t offset_y) {
  Node node;
  ++state.layer_count;
  node.name = attribute_or(element, "name", "");
  if (node.name.empty()) {
    node.name = "Layer " + std::to_string(state.layer_count);
  }
  node.x = offset_x + parse_integer(attribute_or(element, "x", "0")).value_or(0);
  node.y = offset_y + parse_integer(attribute_or(element, "y", "0")).value_or(0);
  if (std::abs(node.x) > kMaxSide || std::abs(node.y) > kMaxSide) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster layer list (stack.xml) is damaged or not valid XML"));
  }
  read_common(element, node, state);
  const auto src = attribute_or(element, "src", "");
  if (!formats::safe_zip_entry_path(src)) {
    add_unique(state.unsafe_paths, node.name);
  } else if (auto bytes = state.zip.extract(src, kMaxEntryBytes); !bytes.has_value()) {
    add_unique(state.missing_files, node.name + " (" + src + ")");
  } else {
    std::string reason;
    if (check_image(*bytes, state.pixel_budget, reason)) {
      node.encoded = std::move(*bytes);  // decoded with the others once the tree is read
      node.src = src;
    } else {
      add_unique(state.unreadable_files, node.name + " (" + src + ": " + reason + ")");
    }
  }
  out.push_back(std::move(node));
}

void read_stack(const svg::XmlNode& stack, std::vector<Node>& out, ReadState& state, std::int64_t offset_x,
                std::int64_t offset_y, int depth) {
  if (depth > 64) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster layer list (stack.xml) is damaged or not valid XML"));
  }
  for (const auto& child : stack.children) {
    if (child.is_text()) {
      continue;
    }
    if (child.name == "layer") {
      read_layer_element(child, out, state, offset_x, offset_y);
    } else if (child.name == "stack") {
      Node group;
      group.group = true;
      ++state.group_count;
      group.name = attribute_or(child, "name", "");
      if (group.name.empty()) {
        group.name = "Group " + std::to_string(state.group_count);
      }
      read_common(child, group, state);
      if (group.clipped) {  // source-atop on a whole group: kept as a plain group
        group.clipped = false;
        add_unique(state.approximated_ops, group.name + " (svg:src-atop)");
      }
      if (attribute_or(child, "isolation", "isolate") == "auto") {
        // Non-isolated: the stack's own op is ignored (spec) and its layers blend
        // straight onto the backdrop, Photoshop's Pass Through.
        group.blend = BlendMode::PassThrough;
      }
      // Stack x/y were dropped in OpenRaster 0.0.6; older files that set them still
      // offset the stack's layers.
      const auto stack_x = parse_integer(attribute_or(child, "x", "0")).value_or(0);
      const auto stack_y = parse_integer(attribute_or(child, "y", "0")).value_or(0);
      read_stack(child, group.children, state, offset_x + stack_x, offset_y + stack_y, depth + 1);
      out.push_back(std::move(group));
    } else {
      ++state.skipped_elements;  // <text> and filter elements are not baseline OpenRaster
    }
  }
  // Source-atop clips to everything below it in the stack. Patchy's clipping is to the
  // first unclipped layer below, which is the same only when that layer is the bottom of
  // the stack; a clip above other layers, or with nothing below it, opens unclipped.
  // `out` is uppermost first here.
  for (std::size_t i = 0; i < out.size(); ++i) {
    if (!out[i].clipped) {
      continue;
    }
    std::size_t base = i + 1;
    while (base < out.size() && out[base].clipped) {
      ++base;
    }
    if (base + 1 != out.size()) {
      add_unique(state.approximated_ops, out[i].name + " (svg:src-atop)");
      out[i].clipped = false;
    }
  }
}

void collect_encoded(std::vector<Node>& nodes, std::vector<Node*>& out) {
  for (auto& node : nodes) {
    if (!node.encoded.empty()) {
      out.push_back(&node);
    }
    collect_encoded(node.children, out);
  }
}

// Decodes every checked layer file, spread over workers for files with several layers
// (stb_image keeps no shared state between decodes). Notes and the depth are gathered in
// file order afterwards, so the result never depends on thread timing.
void decode_layer_images(std::vector<Node>& nodes, ReadState& state) {
  std::vector<Node*> pending;
  collect_encoded(nodes, pending);
  std::vector<std::string> reasons(pending.size());
  std::atomic<std::size_t> next{0};
  const auto decode_some = [&] {
    for (auto index = next.fetch_add(1); index < pending.size(); index = next.fetch_add(1)) {
      pending[index]->image = decode_checked_image(pending[index]->encoded, reasons[index]);
      std::vector<std::uint8_t>().swap(pending[index]->encoded);
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
  for (std::size_t i = 0; i < pending.size(); ++i) {
    const auto* node = pending[i];
    if (!node->image.has_value()) {
      add_unique(state.unreadable_files, node->name + " (" + node->src + ": " + reasons[i] + ")");
    } else if (node->image->sixteen) {
      state.any_sixteen = true;
    }
  }
}

struct BuildState {
  Document& document;
  BitDepth depth;
  std::optional<LayerId> selected;
};

[[nodiscard]] Layer build_layer(Node& node, BuildState& state) {
  if (node.group) {
    Layer group(state.document.allocate_layer_id(), node.name, LayerKind::Group);
    group.set_opacity(node.opacity);
    group.set_visible(node.visible);
    group.set_blend_mode(node.blend);
    group.set_lock_flags(node.locks);
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
      group.add_child(build_layer(*it, state));
    }
    if (node.selected && !state.selected.has_value()) {
      state.selected = group.id();
    }
    return group;
  }
  PixelBuffer pixels = node.image.has_value()
                           ? image_to_buffer(*node.image, state.depth)
                           : PixelBuffer(0, 0, with_bit_depth(PixelFormat::rgba8(), state.depth));
  node.image.reset();
  const auto width = pixels.width();
  const auto height = pixels.height();
  Layer layer(state.document.allocate_layer_id(), node.name, std::move(pixels));
  layer.set_bounds(Rect{static_cast<std::int32_t>(node.x), static_cast<std::int32_t>(node.y), width, height});
  layer.set_opacity(node.opacity);
  layer.set_visible(node.visible);
  layer.set_blend_mode(node.blend);
  layer.set_clipped(node.clipped);
  layer.set_lock_flags(node.locks);
  if (node.selected && !state.selected.has_value()) {
    state.selected = layer.id();
  }
  return layer;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& items) {
  std::string text;
  for (const auto& item : items) {
    text += (text.empty() ? "" : ", ") + item;
  }
  return text;
}

// ---------------------------------------------------------------------------------------
// Writing

[[nodiscard]] bool layer_has_blending_options(const Layer& layer) {
  if (std::abs(layer.fill_opacity() - 1.0F) > 0.0001F || layer.restricted_channels() != 0 ||
      render_detail::layer_knockout_mode(layer) != 0) {
    return true;
  }
  const auto status = layer.blend_if_payload_status();
  return status == BlendIfPayloadStatus::Unsupported ? !layer.raw_psd_blending_ranges().empty()
                                                     : !blend_if_is_identity(layer.blend_if());
}

// A mask that only carries a flat file's alpha (the document-alpha marker) bakes into the
// layer's alpha without loss; any other raster or vector mask is a loss.
[[nodiscard]] bool layer_has_real_mask(const Layer& layer) {
  return (layer.mask().has_value() && !layer_mask_is_document_alpha(layer)) || layer.vector_mask() != nullptr;
}

[[nodiscard]] bool layer_has_any_mask(const Layer& layer) {
  return layer.mask().has_value() || layer.vector_mask() != nullptr;
}

// A group that stays a <stack>: nothing on it that only rendering can express.
[[nodiscard]] bool group_is_container(const Layer& group) {
  return group.layer_style().empty() && !layer_has_any_mask(group) && !layer_has_blending_options(group) &&
         op_for_blend_mode(group.blend_mode()).has_value();
}

// A layer whose own pixels are what the file needs (no rendering in between).
[[nodiscard]] bool layer_is_plain_pixels(const Layer& layer) {
  return layer.kind() == LayerKind::Pixel && !layer_pixels_are_procedural(layer) && layer.layer_style().empty() &&
         !layer_has_any_mask(layer) && !layer_has_blending_options(layer) && layer.smart_filter_stack() == nullptr;
}

// Does this unit need the pixels below it in a way ORA cannot express? Adjustment layers
// and blend modes without an ORA name merge everything below them into one image; a
// pass-through group carries a barrier inside it (or its own bake) to this level.
[[nodiscard]] bool unit_is_barrier(const std::vector<Layer>& siblings, const vector_export::Unit& unit) {
  const Layer& base = siblings[unit.begin];
  if (base.kind() == LayerKind::Adjustment) {
    return true;
  }
  if (!op_for_blend_mode(base.blend_mode()).has_value()) {
    return true;
  }
  if (base.kind() == LayerKind::Group && base.blend_mode() == BlendMode::PassThrough) {
    if (!group_is_container(base)) {
      return true;
    }
    for (const auto& child : vector_export::build_units(base.children())) {
      if (unit_is_barrier(base.children(), child)) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::optional<Rect> alpha_bounds(const PixelBuffer& pixels) {
  std::int32_t left = pixels.width();
  std::int32_t top = pixels.height();
  std::int32_t right = -1;
  std::int32_t bottom = -1;
  const auto format = pixels.format();
  const bool rgba8 = format.channels == 4 && format.bit_depth == BitDepth::UInt8;
  const bool rgba16 = format.channels == 4 && format.bit_depth == BitDepth::UInt16;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto row = pixels.row(y);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      bool covered = false;
      if (rgba8) {
        covered = row[static_cast<std::size_t>(x) * 4U + 3U] != 0;
      } else if (rgba16) {
        const auto* alpha = row.data() + static_cast<std::size_t>(x) * 8U + 6U;
        covered = alpha[0] != 0 || alpha[1] != 0;
      } else {
        covered = pixel_alpha_at(pixels, x, y) > 0.0F;
      }
      if (covered) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left) {
    return std::nullopt;
  }
  return Rect{left, top, right - left + 1, bottom - top + 1};
}

[[nodiscard]] PixelBuffer crop(const PixelBuffer& pixels, Rect rect) {
  PixelBuffer out(rect.width, rect.height, pixels.format());
  const auto pixel_bytes = bytes_per_pixel(pixels.format());
  for (std::int32_t y = 0; y < rect.height; ++y) {
    const auto source = pixels.row(rect.y + y);
    auto target = out.row(y);
    std::memcpy(target.data(), source.data() + static_cast<std::size_t>(rect.x) * pixel_bytes,
                static_cast<std::size_t>(rect.width) * pixel_bytes);
  }
  return out;
}

// Area average of the 8-bit merged image (premultiplied, integer math) down to fit
// 256 x 256; never upscales.
[[nodiscard]] PixelBuffer thumbnail_from(const PixelBuffer& merged) {
  const auto width = merged.width();
  const auto height = merged.height();
  const double scale = std::min({1.0, static_cast<double>(kThumbnailSide) / width,
                                 static_cast<double>(kThumbnailSide) / height});
  const auto target_width = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(width * scale)));
  const auto target_height = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(height * scale)));
  PixelBuffer thumbnail(target_width, target_height, PixelFormat::rgba8());
  for (std::int32_t ty = 0; ty < target_height; ++ty) {
    const auto y0 = static_cast<std::int32_t>(static_cast<std::int64_t>(ty) * height / target_height);
    const auto y1 = std::max(y0 + 1, static_cast<std::int32_t>(static_cast<std::int64_t>(ty + 1) * height / target_height));
    auto out = thumbnail.row(ty);
    for (std::int32_t tx = 0; tx < target_width; ++tx) {
      const auto x0 = static_cast<std::int32_t>(static_cast<std::int64_t>(tx) * width / target_width);
      const auto x1 = std::max(x0 + 1, static_cast<std::int32_t>(static_cast<std::int64_t>(tx + 1) * width / target_width));
      std::uint64_t red = 0;
      std::uint64_t green = 0;
      std::uint64_t blue = 0;
      std::uint64_t alpha = 0;
      for (std::int32_t y = y0; y < y1; ++y) {
        const auto row = merged.row(y);
        for (std::int32_t x = x0; x < x1; ++x) {
          const auto* px = row.data() + static_cast<std::size_t>(x) * 4U;
          red += static_cast<std::uint64_t>(px[0]) * px[3];
          green += static_cast<std::uint64_t>(px[1]) * px[3];
          blue += static_cast<std::uint64_t>(px[2]) * px[3];
          alpha += px[3];
        }
      }
      const auto count = static_cast<std::uint64_t>(x1 - x0) * static_cast<std::uint64_t>(y1 - y0);
      auto* px = out.data() + static_cast<std::size_t>(tx) * 4U;
      if (alpha == 0) {
        px[0] = px[1] = px[2] = px[3] = 0;
        continue;
      }
      px[0] = static_cast<std::uint8_t>((red + alpha / 2) / alpha);
      px[1] = static_cast<std::uint8_t>((green + alpha / 2) / alpha);
      px[2] = static_cast<std::uint8_t>((blue + alpha / 2) / alpha);
      px[3] = static_cast<std::uint8_t>((alpha + count / 2) / count);
    }
  }
  return thumbnail;
}

[[nodiscard]] PixelBuffer narrowed_to_8(const PixelBuffer& pixels) {
  if (pixels.format().bit_depth == BitDepth::UInt8) {
    return pixels;
  }
  return convert_pixel_buffer_depth(pixels, BitDepth::UInt8, SampleKind::Color);
}

class Writer {
public:
  Writer(const Document& document, std::vector<Loss>* losses, bool dry_run)
      : document_(document), losses_(losses), dry_run_(dry_run),
        depth_(document.color_state().bit_depth == BitDepth::UInt8 ? BitDepth::UInt8 : BitDepth::UInt16),
        scratch_(document) {
    scratch_.layers().clear();
    scratch_.channels().clear();
  }

  // stack.xml for the whole document; images() holds the layer files in order.
  [[nodiscard]] std::string run() {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<image version=\"0.0.6\"";
    append_attribute(xml, "w", std::to_string(document_.width()));
    append_attribute(xml, "h", std::to_string(document_.height()));
    const auto ppi = [](double value) {
      return std::to_string(std::max<long long>(1, std::llround(std::isfinite(value) ? value : 72.0)));
    };
    append_attribute(xml, "xres", ppi(document_.print_settings().horizontal_ppi));
    append_attribute(xml, "yres", ppi(document_.print_settings().vertical_ppi));
    xml += ">\n <stack>\n";
    xml += emit_siblings(document_.layers(), 2);
    xml += " </stack>\n</image>\n";
    return xml;
  }

  [[nodiscard]] std::vector<PixelBuffer>& images() noexcept { return images_; }

private:
  void lose(LossKind kind, const std::string& name) {
    if (losses_ != nullptr) {
      losses_->push_back(Loss{kind, name});
    }
  }

  // Composites `layers` (one sibling slice, bottom first) alone and returns the visible
  // part with its canvas position; nullopt when nothing is visible.
  [[nodiscard]] std::optional<std::pair<PixelBuffer, Rect>> render(std::vector<Layer> layers) {
    Document scratch = scratch_;
    for (auto& layer : layers) {
      scratch.add_layer(std::move(layer));
    }
    const PixelBuffer flat = depth_ == BitDepth::UInt8 ? flatten_document_rgba8(scratch)
                                                       : Compositor{}.flatten_rgba_deep(scratch);
    const auto bounds = alpha_bounds(flat);
    if (!bounds.has_value()) {
      return std::nullopt;
    }
    return std::make_pair(crop(flat, *bounds), *bounds);
  }

  [[nodiscard]] std::string image_reference(PixelBuffer pixels) {
    const auto name = "data/layer" + std::to_string(images_.size()) + ".png";
    images_.push_back(std::move(pixels));
    return name;
  }

  void append_layer_attributes(std::string& xml, const Layer& layer) {
    append_attribute(xml, "name", layer.name());
    append_attribute(xml, "opacity", format_opacity(layer.opacity()));
    append_attribute(xml, "visibility", layer.visible() ? "visible" : "hidden");
    const auto op = op_for_blend_mode(layer.blend_mode());
    append_attribute(xml, "composite-op", op.has_value() ? op->name : "svg:src-over");
    if (op.has_value() && op->krita) {
      lose(LossKind::BlendModeKrita, layer.name());
    }
    if (document_.active_layer_id() == layer.id()) {
      append_attribute(xml, "selected", "true");
    }
    // Lock Image Pixels (alone or within Lock All) is edit-locked, which GIMP and Krita read
    // back; the transparency and position locks have no OpenRaster attribute.
    if ((layer.lock_flags() & kLayerLockImagePixels) != 0U) {
      append_attribute(xml, "edit-locked", "true");
    }
  }

  [[nodiscard]] std::string layer_element(const Layer& attributes, std::optional<std::pair<PixelBuffer, Rect>> image,
                                          int indent) {
    std::string xml(static_cast<std::size_t>(indent), ' ');
    xml += "<layer";
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::string src;
    if (dry_run_) {
      src = "data/layer.png";
    } else if (image.has_value()) {
      x = image->second.x;
      y = image->second.y;
      src = image_reference(std::move(image->first));
    } else {
      // An empty layer still needs a file: one transparent pixel, as Krita writes.
      src = image_reference(PixelBuffer(1, 1, with_bit_depth(PixelFormat::rgba8(), depth_)));
    }
    append_attribute(xml, "src", src);
    append_attribute(xml, "x", std::to_string(x));
    append_attribute(xml, "y", std::to_string(y));
    append_layer_attributes(xml, attributes);
    xml += "/>\n";
    return xml;
  }

  // One layer, clip run or group rendered on its own at Normal and full opacity, written
  // with the base's opacity, visibility and blend mode so it still blends with what is
  // below it.
  [[nodiscard]] std::string baked_element(std::span<const Layer> run, int indent) {
    const Layer& base = run.front();
    std::optional<std::pair<PixelBuffer, Rect>> image;
    if (!dry_run_) {
      std::vector<Layer> copies(run.begin(), run.end());
      copies.front().set_visible(true);
      copies.front().set_opacity(1.0F);
      copies.front().set_blend_mode(BlendMode::Normal);
      image = render(std::move(copies));
    }
    return layer_element(base, std::move(image), indent);
  }

  void record_bake_reason(const Layer& layer) {
    if (layer.kind() == LayerKind::Text || layer_is_text(layer)) {
      lose(LossKind::TextLayer, layer.name());
    } else if (layer_is_vector_shape(layer) || layer.kind() == LayerKind::Vector) {
      lose(LossKind::ShapeLayer, layer.name());
    } else if (layer.kind() == LayerKind::SmartObject || layer_is_smart_object(layer)) {
      lose(LossKind::SmartObjectLayer, layer.name());
    } else if (!layer.layer_style().empty()) {
      lose(LossKind::LayerStyle, layer.name());
    } else if (layer_has_real_mask(layer)) {
      lose(LossKind::LayerMask, layer.name());
    } else if (layer_has_blending_options(layer)) {
      lose(LossKind::BlendingOptions, layer.name());
    }
  }

  // What merges into one image under a barrier, by cause; the layers that merge only
  // because they sit below one are covered by the barrier's "and the layers below it".
  void record_merged(const std::vector<Layer>& siblings, std::size_t end) {
    for (std::size_t i = 0; i < end; ++i) {
      const Layer& layer = siblings[i];
      if (layer.kind() == LayerKind::Adjustment) {
        lose(LossKind::AdjustmentLayer, layer.name());
      } else if (!op_for_blend_mode(layer.blend_mode()).has_value()) {
        lose(LossKind::BlendModeMerged, layer.name());
      } else if (layer.kind() == LayerKind::Group && layer.blend_mode() == BlendMode::PassThrough) {
        if (!group_is_container(layer)) {
          lose(LossKind::Group, layer.name());
        } else {
          record_merged(layer.children(), layer.children().size());
        }
      }
    }
  }

  [[nodiscard]] std::string emit_siblings(const std::vector<Layer>& siblings, int indent) {
    const auto units = vector_export::build_units(siblings);
    std::size_t barrier_end = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
      if (unit_is_barrier(siblings, units[i])) {
        barrier_end = i + 1;
      }
    }
    std::vector<std::string> elements;  // bottom first
    if (barrier_end > 0) {
      const auto end = units[barrier_end - 1].end;
      record_merged(siblings, end);
      std::optional<std::pair<PixelBuffer, Rect>> image;
      if (!dry_run_) {
        image = render(std::vector<Layer>(siblings.begin(), siblings.begin() + static_cast<std::ptrdiff_t>(end)));
      }
      Layer merged(LayerId{0}, "Merged", LayerKind::Pixel);
      elements.push_back(layer_element(merged, std::move(image), indent));
    }
    for (std::size_t i = barrier_end; i < units.size(); ++i) {
      const auto& unit = units[i];
      const Layer& base = siblings[unit.begin];
      const std::span<const Layer> run(siblings.data() + unit.begin, unit.end - unit.begin);
      if (run.size() > 1) {
        lose(LossKind::ClippingGroup, base.name());
        elements.push_back(baked_element(run, indent));
      } else if (base.kind() == LayerKind::Group) {
        if (group_is_container(base)) {
          elements.push_back(group_element(base, indent));
        } else {
          lose(LossKind::Group, base.name());
          elements.push_back(baked_element(run, indent));
        }
      } else if (layer_is_plain_pixels(base)) {
        std::optional<std::pair<PixelBuffer, Rect>> image;
        if (!dry_run_ && base.pixels().width() > 0 && base.pixels().height() > 0) {
          const auto& pixels = base.pixels();
          image = std::make_pair(pixels.format().bit_depth == depth_
                                     ? pixels
                                     : convert_pixel_buffer_depth(pixels, depth_, SampleKind::Color),
                                 base.bounds());
        }
        elements.push_back(layer_element(base, std::move(image), indent));
      } else {
        record_bake_reason(base);
        elements.push_back(baked_element(run, indent));
      }
    }
    std::string xml;
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
      xml += *it;
    }
    return xml;
  }

  [[nodiscard]] std::string group_element(const Layer& group, int indent) {
    std::string xml(static_cast<std::size_t>(indent), ' ');
    xml += "<stack";
    append_layer_attributes(xml, group);
    append_attribute(xml, "isolation", group.blend_mode() == BlendMode::PassThrough ? "auto" : "isolate");
    xml += ">\n";
    xml += emit_siblings(group.children(), indent + 1);
    xml.append(static_cast<std::size_t>(indent), ' ');
    xml += "</stack>\n";
    return xml;
  }

  const Document& document_;
  std::vector<Loss>* losses_;
  bool dry_run_;
  BitDepth depth_;
  Document scratch_;
  std::vector<PixelBuffer> images_;
};

// The document the writer walks: artboards as plain groups, 32 bits at 16 (the dry run
// skips that pixel conversion; it only needs the structure).
[[nodiscard]] Document prepared_for_write(const Document& document, std::vector<Loss>* losses, bool convert_pixels) {
  Document prepared = document;
  if (document_has_artboards(prepared)) {
    convert_artboards_to_plain_groups(prepared);
    if (losses != nullptr) {
      losses->push_back(Loss{LossKind::Artboards, {}});
    }
  }
  if (prepared.color_state().bit_depth == BitDepth::Float32) {
    if (convert_pixels) {
      convert_document_depth(prepared, BitDepth::UInt16);
    }
    if (losses != nullptr) {
      losses->push_back(Loss{LossKind::Depth32, {}});
    }
  }
  if (losses != nullptr) {
    if (document.color_state().working_mode != ColorMode::RGB) {
      losses->push_back(Loss{LossKind::ColorMode, {}});
    }
    if (document.palette_editing().has_value()) {
      losses->push_back(Loss{LossKind::PaletteMode, {}});
    }
    if (!document.guides().empty()) {
      losses->push_back(Loss{LossKind::Guides, {}});
    }
    if (!document.paths().empty()) {
      losses->push_back(Loss{LossKind::Paths, {}});
    }
  }
  return prepared;
}

[[nodiscard]] std::string loss_sentence(const Loss& loss) {
  const auto& name = loss.layer_name;
  switch (loss.kind) {
    case LossKind::TextLayer:
      return "Text layer '" + name + "' was rasterized for OpenRaster";
    case LossKind::ShapeLayer:
      return "Shape layer '" + name + "' was rasterized for OpenRaster";
    case LossKind::SmartObjectLayer:
      return "Smart object '" + name + "' was rasterized for OpenRaster";
    case LossKind::AdjustmentLayer:
      return "Adjustment layer '" + name + "' was merged with the layers below it for OpenRaster";
    case LossKind::BlendModeMerged:
      return "The blend mode of '" + name + "' has no OpenRaster name; it was merged with the layers below it";
    case LossKind::BlendModeKrita:
      return "The blend mode of '" + name + "' is saved under Krita's name; other apps show it as Normal";
    case LossKind::LayerStyle:
      return "The layer style of '" + name + "' was baked into its pixels for OpenRaster";
    case LossKind::LayerMask:
      return "The mask of '" + name + "' was baked into its pixels for OpenRaster";
    case LossKind::ClippingGroup:
      return "Clipping mask group '" + name + "' was baked into one layer for OpenRaster";
    case LossKind::BlendingOptions:
      return "The blending options of '" + name + "' were baked into its pixels for OpenRaster";
    case LossKind::Group:
      return "Group '" + name + "' was baked into one layer for OpenRaster (its styles or masks)";
    case LossKind::Depth32:
      return "The 32-bit channels were saved at 16 bits per channel for OpenRaster";
    case LossKind::ColorMode:
      return "The document was saved in RGB for OpenRaster";
    case LossKind::PaletteMode:
      return "The palette is not stored in OpenRaster; the colors were saved as RGB";
    case LossKind::Guides:
      return "Guides are not stored in OpenRaster";
    case LossKind::Paths:
      return "Paths are not stored in OpenRaster";
    case LossKind::Artboards:
      return "Artboards were saved as groups for OpenRaster";
  }
  return {};
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

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  if (bytes.size() < 4 || bytes[0] != 'P' || bytes[1] != 'K') {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not an OpenRaster image"));
  }
  ZipReader zip(bytes);
  if (auto mimetype = zip.extract("mimetype", 4096); mimetype.has_value()) {
    std::string text(mimetype->begin(), mimetype->end());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
      text.pop_back();
    }
    if (text != kMimetype) {
      throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not an OpenRaster image"));
    }
  }
  auto stack_bytes = zip.extract("stack.xml", kMaxStackXmlBytes);
  if (!stack_bytes.has_value()) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not an OpenRaster image"));
  }
  svg::XmlNode root;
  try {
    root = svg::parse_xml(*stack_bytes);
  } catch (const std::exception&) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster layer list (stack.xml) is damaged or not valid XML"));
  }
  if (root.name != "image") {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster layer list (stack.xml) is damaged or not valid XML"));
  }
  const auto width = parse_integer(attribute_or(root, "w", "")).value_or(0);
  const auto height = parse_integer(attribute_or(root, "h", "")).value_or(0);
  if (width <= 0 || height <= 0) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster layer list (stack.xml) is damaged or not valid XML"));
  }
  if (width > kMaxSide || height > kMaxSide ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxTotalPixels) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster file is too large to open"));
  }
  const svg::XmlNode* root_stack = nullptr;
  for (const auto& child : root.children) {
    if (child.name == "stack") {
      root_stack = &child;
      break;
    }
  }
  if (root_stack == nullptr) {
    throw read_error(PATCHY_TRANSLATE_NOOP("QObject", "The OpenRaster layer list (stack.xml) is damaged or not valid XML"));
  }

  ReadState state(zip);
  std::vector<Node> nodes;
  read_stack(*root_stack, nodes, state, 0, 0, 0);
  decode_layer_images(nodes, state);

  const auto depth = state.any_sixteen && deep_editing_enabled() ? BitDepth::UInt16 : BitDepth::UInt8;
  Document document(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                    with_bit_depth(PixelFormat::rgba8(), depth));
  document.color_state().bit_depth = depth;
  // xres/yres are pixels per inch; the spec's default is 72.
  const auto xres = parse_number(attribute_or(root, "xres", "72")).value_or(72.0);
  const auto yres = parse_number(attribute_or(root, "yres", "72")).value_or(72.0);
  document.print_settings().horizontal_ppi = xres >= 1.0 && xres <= 1e6 ? xres : 72.0;
  document.print_settings().vertical_ppi = yres >= 1.0 && yres <= 1e6 ? yres : 72.0;

  BuildState build{document, depth, std::nullopt};
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    document.add_layer(build_layer(*it, build));
  }
  if (document.layers().empty()) {
    // No layers: the merged image (or a transparent layer) keeps the document openable.
    std::optional<DecodedImage> merged;
    if (auto merged_bytes = zip.extract("mergedimage.png", kMaxEntryBytes); merged_bytes.has_value()) {
      std::string reason;
      merged = decode_image(*merged_bytes, state.pixel_budget, reason);
    }
    if (merged.has_value()) {
      document.add_pixel_layer("Background", image_to_buffer(*merged, depth));
    } else {
      document.add_pixel_layer("Layer 1", PixelBuffer(document.width(), document.height(),
                                                      with_bit_depth(PixelFormat::rgba8(), depth)));
    }
  }
  if (build.selected.has_value()) {
    document.set_active_layer(*build.selected);
  }

  if (notices != nullptr) {
    if (!state.unknown_ops.empty()) {
      notices->push_back("Unknown blend modes opened as Normal: " + joined(state.unknown_ops));
    }
    if (!state.approximated_ops.empty()) {
      notices->push_back("Compositing operators Patchy approximates opened as Normal or unclipped: " +
                         joined(state.approximated_ops));
    }
    if (!state.missing_files.empty()) {
      notices->push_back("Layers whose image file is missing opened empty: " + joined(state.missing_files));
    }
    if (!state.unreadable_files.empty()) {
      notices->push_back("Layers whose image could not be read opened empty: " + joined(state.unreadable_files));
    }
    if (!state.unsafe_paths.empty()) {
      notices->push_back("Layers that point outside the file opened empty: " + joined(state.unsafe_paths));
    }
    if (state.skipped_elements > 0) {
      notices->push_back("Skipped " + std::to_string(state.skipped_elements) +
                         " elements that are not OpenRaster layers or groups (text or filters)");
    }
    if (state.any_sixteen && depth == BitDepth::UInt8) {
      notices->push_back("16-bit layers were converted to 8 bits per channel");
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  return read(formats::read_file_bytes(path, "OpenRaster"), notices);
}

std::vector<std::uint8_t> DocumentIo::write(const Document& document, std::vector<std::string>* notices) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot save an empty document as OpenRaster"));
  }
  std::vector<Loss> losses;
  const Document prepared = prepared_for_write(document, &losses, /*convert_pixels*/ true);
  Writer writer(prepared, &losses, /*dry_run*/ false);
  const auto xml = writer.run();
  if (notices != nullptr) {
    for (const auto& loss : losses) {
      notices->push_back(loss_sentence(loss));
    }
  }

  // The merged image at the document's depth (16-bit PNG for deep documents, which the
  // spec allows) and the 8-bit thumbnail from it.
  PixelBuffer merged = prepared.color_state().bit_depth == BitDepth::UInt8 ? flatten_document_rgba8(prepared)
                                                                          : Compositor{}.flatten_rgba_deep(prepared);
  PixelBuffer thumbnail = thumbnail_from(narrowed_to_8(merged));

  // Every PNG encodes on its own: large documents spread them over workers. Each worker
  // only reads its buffers through const access, so copy-on-write storage shared with the
  // document is never detached off this thread (AGENTS.md).
  std::vector<const PixelBuffer*> sources;
  for (const auto& image : writer.images()) {
    sources.push_back(&image);
  }
  sources.push_back(&merged);
  sources.push_back(&thumbnail);
  std::vector<std::vector<std::uint8_t>> encoded(sources.size());
  std::uint64_t total_pixels = 0;
  for (const auto* source : sources) {
    total_pixels += static_cast<std::uint64_t>(source->width()) * static_cast<std::uint64_t>(source->height());
  }
  std::atomic<std::size_t> next{0};
  const auto encode_some = [&] {
    for (auto index = next.fetch_add(1); index < sources.size(); index = next.fetch_add(1)) {
      encoded[index] = detail::encode_png(*sources[index]);
    }
  };
  const int wanted = total_pixels >= (1ULL << 21)
                         ? static_cast<int>(std::min<std::size_t>(sources.size(),
                                                                  static_cast<std::size_t>(hardware_worker_threads())))
                         : 1;
  const int workers = max_blocking_fanout_workers(wanted);
  if (workers < 2) {
    encode_some();
  } else {
    std::vector<std::future<void>> futures;
    for (int worker = 1; worker < workers; ++worker) {
      futures.push_back(std::async(std::launch::async, encode_some));
    }
    encode_some();
    for (auto& future : futures) {
      future.get();
    }
  }

  std::vector<detail::ZipEntry> entries;
  entries.push_back({"mimetype", std::vector<std::uint8_t>(kMimetype.begin(), kMimetype.end()), false});
  entries.push_back({"stack.xml", std::vector<std::uint8_t>(xml.begin(), xml.end()), true});
  const auto layer_count = writer.images().size();
  for (std::size_t i = 0; i < layer_count; ++i) {
    entries.push_back({"data/layer" + std::to_string(i) + ".png", std::move(encoded[i]), false});
  }
  entries.push_back({"mergedimage.png", std::move(encoded[layer_count]), false});
  entries.push_back({"Thumbnails/thumbnail.png", std::move(encoded[layer_count + 1]), false});
  return detail::zip_archive(entries);
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path,
                            std::vector<std::string>* notices) {
  formats::write_file_bytes(path, write(document, notices), "OpenRaster");
}

std::vector<Loss> DocumentIo::lost_content(const Document& document) {
  std::vector<Loss> losses;
  if (document.width() <= 0 || document.height() <= 0) {
    return losses;
  }
  const Document prepared = prepared_for_write(document, &losses, /*convert_pixels*/ false);
  Writer writer(prepared, &losses, /*dry_run*/ true);
  (void)writer.run();
  return losses;
}

namespace detail {

std::optional<BlendMode> blend_mode_from_composite_op(std::string_view op) {
  return blend_mode_from_op(op);
}

namespace {

void icc_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

void icc_tag_type(std::vector<std::uint8_t>& out, const char (&type)[5]) {
  out.insert(out.end(), type, type + 4);
  append_be32(out, 0);  // reserved
}

// s15Fixed16Number from a value given in 1/65536 units, so the profile holds exact,
// toolchain-independent integers.
void icc_s15(std::vector<std::uint8_t>& out, std::int32_t units) {
  append_be32(out, static_cast<std::uint32_t>(units));
}

std::vector<std::uint8_t> icc_xyz(std::int32_t x, std::int32_t y, std::int32_t z) {
  std::vector<std::uint8_t> out;
  icc_tag_type(out, "XYZ ");
  icc_s15(out, x);
  icc_s15(out, y);
  icc_s15(out, z);
  return out;
}

std::vector<std::uint8_t> icc_text(std::string_view ascii) {
  std::vector<std::uint8_t> out;
  icc_tag_type(out, "mluc");
  append_be32(out, 1);   // one record
  append_be32(out, 12);  // record size
  out.insert(out.end(), {'e', 'n', 'U', 'S'});
  append_be32(out, static_cast<std::uint32_t>(ascii.size() * 2));
  append_be32(out, 28);  // string offset from the tag start
  for (const char ch : ascii) {
    icc_u16(out, static_cast<unsigned char>(ch));  // UTF-16BE
  }
  return out;
}

}  // namespace

const std::vector<std::uint8_t>& srgb_icc_profile() {
  static const std::vector<std::uint8_t> profile = [] {
    // Values in 1/65536 units: D50 white, the Bradford D65-to-D50 matrix and the D50-adapted
    // sRGB colorants as ICC profiles publish them; the sRGB curve as parametric type 3,
    // Y = (aX + b)^g above d, cX below (g 2.4, a 1/1.055, b 0.055/1.055, c 1/12.92,
    // d 0.04045).
    std::vector<std::uint8_t> trc;
    icc_tag_type(trc, "para");
    icc_u16(trc, 3);
    icc_u16(trc, 0);
    for (const std::int32_t value : {157286, 62119, 3417, 5072, 2651}) {
      icc_s15(trc, value);
    }
    std::vector<std::uint8_t> chad;
    icc_tag_type(chad, "sf32");
    for (const std::int32_t value : {68674, 1502, -3291, 1939, 64912, -1119, -606, 988, 49262}) {
      icc_s15(chad, value);
    }
    struct Tag {
      const char* signature;
      std::vector<std::uint8_t> body;
    };
    const std::vector<Tag> tags = {
        {"desc", icc_text("sRGB")},
        {"cprt", icc_text("No copyright, use freely")},
        {"wtpt", icc_xyz(63190, 65536, 54061)},
        {"chad", chad},
        {"rXYZ", icc_xyz(28578, 14581, 912)},
        {"gXYZ", icc_xyz(25241, 46981, 6362)},
        {"bXYZ", icc_xyz(9376, 3972, 46799)},
        {"rTRC", trc},
    };
    constexpr std::size_t kSharedCurves = 2;  // gTRC and bTRC point at rTRC's data
    const std::size_t count = tags.size() + kSharedCurves;
    const std::size_t data_start = 128 + 4 + 12 * count;
    std::vector<std::uint8_t> data;
    std::vector<std::uint8_t> table;
    std::uint32_t curve_offset = 0;
    std::uint32_t curve_size = 0;
    for (const auto& tag : tags) {
      const auto offset = static_cast<std::uint32_t>(data_start + data.size());
      const auto size = static_cast<std::uint32_t>(tag.body.size());
      table.insert(table.end(), tag.signature, tag.signature + 4);
      append_be32(table, offset);
      append_be32(table, size);
      data.insert(data.end(), tag.body.begin(), tag.body.end());
      while (data.size() % 4 != 0) {
        data.push_back(0);
      }
      if (std::string_view(tag.signature) == "rTRC") {
        curve_offset = offset;
        curve_size = size;
      }
    }
    for (const char* signature : {"gTRC", "bTRC"}) {
      table.insert(table.end(), signature, signature + 4);
      append_be32(table, curve_offset);
      append_be32(table, curve_size);
    }
    std::vector<std::uint8_t> out;
    append_be32(out, static_cast<std::uint32_t>(data_start + data.size()));
    append_be32(out, 0);           // preferred CMM: none
    append_be32(out, 0x04300000);  // version 4.3
    out.insert(out.end(), {'m', 'n', 't', 'r', 'R', 'G', 'B', ' ', 'X', 'Y', 'Z', ' '});
    for (const std::uint32_t field : {2026U, 1U, 1U, 0U, 0U, 0U}) {  // fixed creation date
      icc_u16(out, field);
    }
    out.insert(out.end(), {'a', 'c', 's', 'p'});
    out.resize(64, 0);    // platform, flags, manufacturer, model, attributes
    append_be32(out, 0);  // perceptual intent
    icc_s15(out, 63190);  // D50 illuminant
    icc_s15(out, 65536);
    icc_s15(out, 54061);
    out.resize(128, 0);  // creator, profile ID (none) and reserved
    append_be32(out, static_cast<std::uint32_t>(count));
    out.insert(out.end(), table.begin(), table.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
  }();
  return profile;
}

std::vector<std::uint8_t> encode_png(const PixelBuffer& pixels) {
  const auto format = pixels.format();
  if (pixels.width() <= 0 || pixels.height() <= 0 || format.bit_depth == BitDepth::Float32 || format.channels < 1 ||
      format.channels > 4) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not write the OpenRaster file"));
  }
  const bool sixteen = format.bit_depth == BitDepth::UInt16;
  const std::size_t pixel_bytes = sixteen ? 8 : 4;
  const auto stride = static_cast<std::size_t>(pixels.width()) * pixel_bytes;
  const auto height = static_cast<std::size_t>(pixels.height());

  // Adaptive filtering: per row, the filter with the smallest sum of absolute signed
  // bytes (the PNG spec's suggested heuristic); ties go to the lower filter number.
  std::vector<std::uint8_t> filtered;
  filtered.reserve((stride + 1) * height);
  std::vector<std::uint8_t> previous(stride, 0);
  std::vector<std::uint8_t> current(stride, 0);
  std::array<std::vector<std::uint8_t>, 5> candidates;
  for (auto& candidate : candidates) {
    candidate.resize(stride);
  }
  for (std::size_t y = 0; y < height; ++y) {
    png_row(pixels, static_cast<std::int32_t>(y), sixteen, current);
    std::array<std::uint64_t, 5> cost{};
    for (std::size_t i = 0; i < stride; ++i) {
      const int raw = current[i];
      const int left = i >= pixel_bytes ? current[i - pixel_bytes] : 0;
      const int up = previous[i];
      const int up_left = i >= pixel_bytes ? previous[i - pixel_bytes] : 0;
      const std::array<std::uint8_t, 5> value = {
          static_cast<std::uint8_t>(raw), static_cast<std::uint8_t>(raw - left), static_cast<std::uint8_t>(raw - up),
          static_cast<std::uint8_t>(raw - ((left + up) >> 1)), static_cast<std::uint8_t>(raw - paeth(left, up, up_left))};
      for (std::size_t f = 0; f < 5; ++f) {
        candidates[f][i] = value[f];
        cost[f] += static_cast<std::uint64_t>(std::abs(static_cast<int>(static_cast<std::int8_t>(value[f]))));
      }
    }
    std::size_t best = 0;
    for (std::size_t f = 1; f < 5; ++f) {
      if (cost[f] < cost[best]) {
        best = f;
      }
    }
    filtered.push_back(static_cast<std::uint8_t>(best));
    filtered.insert(filtered.end(), candidates[best].begin(), candidates[best].end());
    std::swap(previous, current);
  }

  mz_ulong compressed_size = mz_compressBound(static_cast<mz_ulong>(filtered.size()));
  std::vector<std::uint8_t> compressed(compressed_size);
  if (mz_compress2(compressed.data(), &compressed_size, filtered.data(), static_cast<mz_ulong>(filtered.size()),
                   kPngDeflateLevel) != MZ_OK) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not write the OpenRaster file"));
  }
  compressed.resize(compressed_size);

  std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<std::uint8_t> header;
  append_be32(header, static_cast<std::uint32_t>(pixels.width()));
  append_be32(header, static_cast<std::uint32_t>(pixels.height()));
  header.push_back(sixteen ? 16 : 8);
  header.push_back(6);  // truecolor with alpha
  header.push_back(0);
  header.push_back(0);
  header.push_back(0);
  append_png_chunk(png, "IHDR", header);
  // The sRGB profile, so readers that want one for deep PNGs (Krita) do not ask.
  static const std::vector<std::uint8_t> iccp = [] {
    const auto& profile = srgb_icc_profile();
    std::vector<std::uint8_t> chunk = {'s', 'R', 'G', 'B', 0, 0};  // name, NUL, zlib method
    mz_ulong size = mz_compressBound(static_cast<mz_ulong>(profile.size()));
    std::vector<std::uint8_t> packed(size);
    if (mz_compress2(packed.data(), &size, profile.data(), static_cast<mz_ulong>(profile.size()), 9) != MZ_OK) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not write the OpenRaster file"));
    }
    chunk.insert(chunk.end(), packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(size));
    return chunk;
  }();
  append_png_chunk(png, "iCCP", iccp);
  // IDAT in chunks of at most 1 MiB, as most encoders write.
  constexpr std::size_t kIdatChunk = 1U << 20;
  for (std::size_t at = 0; at < compressed.size(); at += kIdatChunk) {
    const auto size = std::min(kIdatChunk, compressed.size() - at);
    append_png_chunk(png, "IDAT", std::span<const std::uint8_t>(compressed.data() + at, size));
  }
  append_png_chunk(png, "IEND", {});
  return png;
}

std::vector<std::uint8_t> zip_archive(const std::vector<ZipEntry>& entries, bool force_zip64) {
  struct Written {
    std::uint64_t offset{0};
    std::uint32_t crc{0};
    std::uint64_t compressed{0};
    std::uint64_t uncompressed{0};
    bool deflated{false};
  };
  std::vector<std::uint8_t> out;
  std::vector<Written> written;
  for (const auto& entry : entries) {
    Written record;
    record.offset = out.size();
    record.uncompressed = entry.data.size();
    record.crc = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, entry.data.data(), entry.data.size()));
    std::vector<std::uint8_t> packed;
    if (entry.deflated && !entry.data.empty()) {
      std::size_t size = 0;
      void* raw = tdefl_compress_mem_to_heap(entry.data.data(), entry.data.size(), &size,
                                             static_cast<int>(tdefl_create_comp_flags_from_zip_params(
                                                 kPngDeflateLevel, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY)));
      if (raw == nullptr) {
        throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not write the OpenRaster file"));
      }
      packed.assign(static_cast<std::uint8_t*>(raw), static_cast<std::uint8_t*>(raw) + size);
      mz_free(raw);
      record.deflated = true;
    }
    const auto& payload = record.deflated ? packed : entry.data;
    record.compressed = payload.size();
    const bool zip64 = force_zip64 || record.uncompressed >= kZip32Limit || record.compressed >= kZip32Limit;
    put32(out, 0x04034b50U);
    put16(out, zip64 ? 45 : 20);
    put16(out, name_is_ascii(entry.name) ? 0U : (1U << 11));  // UTF-8 names flag
    put16(out, record.deflated ? 8 : 0);
    put16(out, 0);  // 00:00:00
    put16(out, kDosDate19800101);
    put32(out, record.crc);
    put32(out, zip64 ? kZip32Limit : record.compressed);
    put32(out, zip64 ? kZip32Limit : record.uncompressed);
    put16(out, static_cast<std::uint32_t>(entry.name.size()));
    put16(out, zip64 ? 20 : 0);
    out.insert(out.end(), entry.name.begin(), entry.name.end());
    if (zip64) {
      put16(out, 0x0001);
      put16(out, 16);
      put64(out, record.uncompressed);
      put64(out, record.compressed);
    }
    out.insert(out.end(), payload.begin(), payload.end());
    written.push_back(record);
  }

  const std::uint64_t directory_offset = out.size();
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    const auto& record = written[i];
    const bool big_sizes = force_zip64 || record.uncompressed >= kZip32Limit || record.compressed >= kZip32Limit;
    const bool big_offset = force_zip64 || record.offset >= kZip32Limit;
    std::vector<std::uint8_t> extra;
    if (big_sizes || big_offset) {
      std::vector<std::uint8_t> fields;
      if (big_sizes) {
        put64(fields, record.uncompressed);
        put64(fields, record.compressed);
      }
      if (big_offset) {
        put64(fields, record.offset);
      }
      put16(extra, 0x0001);
      put16(extra, static_cast<std::uint32_t>(fields.size()));
      extra.insert(extra.end(), fields.begin(), fields.end());
    }
    const bool zip64 = !extra.empty();
    put32(out, 0x02014b50U);
    put16(out, zip64 ? 45 : 20);  // made by: MS-DOS host, spec version
    put16(out, zip64 ? 45 : 20);
    put16(out, name_is_ascii(entry.name) ? 0U : (1U << 11));
    put16(out, record.deflated ? 8 : 0);
    put16(out, 0);
    put16(out, kDosDate19800101);
    put32(out, record.crc);
    put32(out, big_sizes ? kZip32Limit : record.compressed);
    put32(out, big_sizes ? kZip32Limit : record.uncompressed);
    put16(out, static_cast<std::uint32_t>(entry.name.size()));
    put16(out, static_cast<std::uint32_t>(extra.size()));
    put16(out, 0);  // comment
    put16(out, 0);  // disk
    put16(out, 0);  // internal attributes
    put32(out, 0);  // external attributes
    put32(out, big_offset ? kZip32Limit : record.offset);
    out.insert(out.end(), entry.name.begin(), entry.name.end());
    out.insert(out.end(), extra.begin(), extra.end());
  }
  const std::uint64_t directory_size = out.size() - directory_offset;
  const std::uint64_t count = entries.size();
  const bool zip64_end = force_zip64 || count >= 0xFFFF || directory_offset >= kZip32Limit ||
                         directory_size >= kZip32Limit;
  if (zip64_end) {
    const std::uint64_t record_offset = out.size();
    put32(out, 0x06064b50U);
    put64(out, 44);
    put16(out, 45);
    put16(out, 45);
    put32(out, 0);
    put32(out, 0);
    put64(out, count);
    put64(out, count);
    put64(out, directory_size);
    put64(out, directory_offset);
    put32(out, 0x07064b50U);
    put32(out, 0);
    put64(out, record_offset);
    put32(out, 1);
  }
  put32(out, 0x06054b50U);
  put16(out, 0);
  put16(out, 0);
  put16(out, zip64_end ? 0xFFFF : static_cast<std::uint32_t>(count));
  put16(out, zip64_end ? 0xFFFF : static_cast<std::uint32_t>(count));
  put32(out, zip64_end ? kZip32Limit : directory_size);
  put32(out, zip64_end ? kZip32Limit : directory_offset);
  put16(out, 0);
  return out;
}

}  // namespace detail

}  // namespace patchy::ora
