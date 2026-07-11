#include "psd/psd_document_io.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_smart_objects.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace patchy::psd {

namespace {

constexpr std::uint16_t kColorModeRgb = 3;
constexpr std::uint16_t kColorModeCmyk = 4;
constexpr std::uint16_t kCompressionRaw = 0;
constexpr std::uint16_t kCompressionRle = 1;
constexpr std::uint16_t kChannelRed = 0;
constexpr std::uint16_t kChannelGreen = 1;
constexpr std::uint16_t kChannelBlue = 2;
constexpr std::uint16_t kChannelBlack = 3;
constexpr std::uint16_t kChannelTransparency = 0xFFFFU;
constexpr std::uint16_t kChannelUserMask = 0xFFFEU;
constexpr std::uint16_t kImageResourceAlphaChannelNames = 1006;
constexpr std::uint16_t kImageResourceDisplayInfo = 1007;
constexpr std::uint16_t kImageResourceResolutionInfo = 1005;
constexpr std::uint16_t kImageResourceGridAndGuidesInfo = 1032;
constexpr std::uint16_t kImageResourceGlobalLightAngle = 1037;
constexpr std::uint16_t kImageResourceIccProfile = 1039;
constexpr std::uint16_t kImageResourceUnicodeAlphaChannelNames = 1045;
constexpr std::uint16_t kImageResourceGlobalLightAltitude = 1049;
constexpr std::uint16_t kImageResourceAlphaIdentifiers = 1053;
constexpr std::uint16_t kImageResourceDisplayInfoFloat = 1077;
// Patchy-private resource (Photoshop plug-in id range 4000-4999): the document
// palette for the palettized editing mode. Photoshop and pre-feature Patchy
// builds preserve unknown resource ids verbatim, so the palette round-trips and
// the file stays a plain RGB PSD everywhere else. Payload, big-endian: magic
// 'PtcP', u16 version = 1, u16 flags (bit0 = palette mode active), u8 alpha
// threshold, u8 reserved, u16 color count, then count RGB byte triples.
constexpr std::uint16_t kImageResourcePatchyPalette = 4210;
constexpr std::uint32_t kPatchyPaletteMagic = 0x50746350U;  // 'PtcP'
constexpr float kDefaultGlobalLightAngle = 120.0F;
constexpr float kDefaultGlobalLightAltitude = 30.0F;
constexpr std::int32_t kDefaultGridCycle32 = 576;
constexpr std::array<char, 4> kPatchyLayerStyleBlockKey{'p', 'l', 'F', 'X'};
constexpr std::array<char, 4> kPatchyLayerStylePayloadSignature{'P', 'L', 'F', 'X'};
constexpr std::uint16_t kPatchyLayerStyleVersion = 1;
constexpr std::uint16_t kMaxPatchyLayerStyleEntries = 512;
constexpr std::array<char, 4> kPatchyAdjustmentBlockKey{'p', 'l', 'A', 'D'};
constexpr std::array<char, 4> kPatchyAdjustmentPayloadSignature{'P', 'L', 'A', 'D'};
constexpr std::uint16_t kPatchyAdjustmentVersion = 4;
constexpr std::array<char, 4> kPatchyCurvesExtensionSignature{'C', 'R', 'V', '2'};
constexpr std::uint16_t kPatchyCurvesExtensionVersion = 1;
constexpr std::uint16_t kPatchyCurvesExtensionChannelCount = 4;
constexpr std::size_t kPatchyCurvesExtensionMaxPayloadSize =
    2U + 2U + kPatchyCurvesExtensionChannelCount * (1U + 1U + 2U + 19U * 4U);
constexpr std::array<char, 4> kPhotoshopLevelsAdjustmentBlockKey{'l', 'e', 'v', 'l'};
constexpr std::uint16_t kPhotoshopLevelsAdjustmentVersion = 2;
constexpr int kPhotoshopLevelsRecordCount = 29;
constexpr std::array<char, 4> kPhotoshopCurvesAdjustmentBlockKey{'c', 'u', 'r', 'v'};
constexpr std::array<char, 4> kPhotoshopCurvesExtraMarker{'C', 'r', 'v', ' '};
constexpr std::array<char, 4> kPhotoshopHueSaturationBlockKey{'h', 'u', 'e', '2'};
constexpr std::uint16_t kPhotoshopHueSaturationVersion = 2;
// version u16, colorize u8, pad u8, colorize h/s/l i16 x3, master h/s/l i16 x3.
constexpr std::size_t kPhotoshopHueSaturationHeaderSize = 16;
constexpr int kMaxTextSizePixels = 8192;
constexpr std::uint32_t kPsdProtectTransparency = 1U << 0U;
constexpr std::uint32_t kPsdProtectComposite = 1U << 1U;
constexpr std::uint32_t kPsdProtectPosition = 1U << 2U;
// Photoshop's PSD/PSB dimension caps; a document over the PSD cap must be saved as PSB.
constexpr std::int32_t kMaxPsdDimension = 30000;
constexpr std::int32_t kMaxPsbDimension = 300000;

// Tagged-block keys Photoshop stores with the '8B64' signature and an 8-byte length in
// PSB files. The spec documents the first thirteen; 'cinf' was pinned empirically (July
// 2026): Photoshop 2026 writes it 8B64+u64 in PSBs and its parser expects that width by
// KEY, so a PSB carrying a narrow 'cinf' fails to open ("open options are incorrect").
// The reader never consults this list — it trusts each block's own signature — but the
// writer uses it to upgrade blocks (authored, or preserved from a PSD) on PSB saves.
[[nodiscard]] bool tagged_block_length_is_u64(std::string_view key) noexcept {
  return key == "LMsk" || key == "Lr16" || key == "Lr32" || key == "Layr" || key == "Mt16" ||
         key == "Mt32" || key == "Mtrn" || key == "Alph" || key == "FMsk" || key == "lnk2" ||
         key == "FEid" || key == "FXid" || key == "PxSD" || key == "cinf";
}

struct LayerChannelInfo {
  std::uint16_t id{0};
  std::uint64_t length{0};  // 4 bytes in PSD records, 8 in PSB
};

struct LayerMaskInfo {
  Rect bounds{};
  std::uint8_t default_color{255};
  bool disabled{false};
  bool linked{true};
};

struct PsdTextBoundsD {
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
};

struct PsdTextGeometry {
  std::array<double, 6> transform{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  PsdTextBoundsD bounds{};
  PsdTextBoundsD bounding_box{};
  PsdTextBoundsD box_bounds{};
  std::array<int, 4> tail_bounds{0, 0, 0, 0};
  int text_index{0};
  // Non-identity Warp Text settings from the TySh warp descriptor (box = 'bounds').
  std::optional<TextWarp> warp;
};

struct LayerRecord {
  Rect bounds;
  std::vector<LayerChannelInfo> channels;
  std::vector<std::uint8_t> blending_ranges;
  BlendMode blend_mode{BlendMode::Normal};
  std::uint8_t opacity{255};
  bool visible{true};
  bool clipping{false};
  std::string name;
  std::uint32_t section_divider_type{0};
  std::optional<LayerMaskInfo> mask;
  std::vector<UnknownPsdBlock> additional_blocks;
  // Smart-object placement parsed from 'SoLd'/'SoLE' (authoritative) or 'PlLd'.
  std::optional<PlacedLayerInfo> placed;
  std::string placed_source_block;
  bool placed_from_sold{false};
  bool placed_parse_failed{false};
  std::optional<std::string> text;
  std::optional<std::string> text_html;
  std::optional<std::string> text_runs;
  std::optional<std::string> text_paragraph_runs;
  std::optional<Rect> text_box;
  std::optional<std::string> text_font;
  std::optional<int> text_size;
  std::optional<RgbColor> text_color;
  std::optional<bool> text_bold;
  std::optional<bool> text_italic;
  std::optional<int> text_anti_alias;
  std::optional<std::string> text_source_block;
  bool text_patchy_generated_type_block{false};
  std::optional<PsdTextGeometry> text_geometry;
  std::uint32_t protection_flags{0};
  bool layer_mask_hides_effects{false};
  LayerStyle layer_style;
};

struct DecodedLayer {
  Layer layer;
  std::uint32_t section_divider_type{0};
};

enum class EncodedLayerKind {
  Pixel,
  Adjustment,
  Group,
  GroupBoundary
};

struct EncodedChannel {
  std::uint16_t id{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::uint16_t compression{kCompressionRaw};
  std::vector<std::uint8_t> data;
};

struct EncodedLayer {
  const Layer* layer{nullptr};
  EncodedLayerKind kind{EncodedLayerKind::Pixel};
  Rect bounds;
  std::vector<EncodedChannel> channels;
  const std::vector<std::uint8_t>* blending_ranges{nullptr};
};

struct ImageResource {
  std::array<char, 4> signature{'8', 'B', 'I', 'M'};
  std::uint16_t id{0};
  std::string name;
  std::vector<std::uint8_t> payload;
};

// Metadata for one extra plane in the composite image data. Photoshop stores a
// DisplayInfo record and a name for merged transparency as well as saved
// alpha/spot channels, but merged transparency deliberately has no alpha ID.
struct CompositeChannelInfo {
  std::string_view name;
  bool merged_transparency{false};
  bool alpha_identifier_eligible{false};
  std::optional<std::uint32_t> photoshop_identifier;
  DocumentChannelDisplayInfo display_info;
  std::span<const std::uint8_t> raw_display_info;
};

struct ParsedCompositeChannelResources {
  std::vector<std::string> legacy_names;
  std::vector<std::string> unicode_names;
  std::vector<std::uint32_t> identifiers;
  std::vector<std::vector<std::uint8_t>> display_records;
};

struct PsdTextStyleRun {
  int start{0};
  int length{0};
  std::string family{"Arial"};
  double size{36.0};
  RgbColor color{0, 0, 0};
  bool bold{false};
  bool italic{false};
  // Fixed leading in engine units (document pixels through the TySh transform). Unset when the
  // run uses Photoshop auto leading (auto_leading), which is paragraph AutoLeading fraction x size.
  std::optional<double> leading;
  bool auto_leading{false};
  // Photoshop tracking: 1/1000 em added after every inter-glyph gap.
  double tracking{0.0};
  // Character-panel glyph scales: width x horizontal_scale, height x vertical_scale. Leading
  // stays FontSize-based (COM-calibrated: VerticalScale does not change auto leading).
  double horizontal_scale{1.0};
  double vertical_scale{1.0};
};

struct PsdTextParagraphRun {
  int start{0};
  int length{0};
  int justification{0};
  double first_line_indent{0.0};
  double start_indent{0.0};
  double end_indent{0.0};
  double space_before{0.0};
  double space_after{0.0};
  // Auto-leading fraction (Photoshop default 1.2): auto leading = fraction x font size.
  double auto_leading_fraction{1.2};
};

// Defaults from the engine data's ResourceDict "normal" style/paragraph sheets. Style runs omit
// every property that matches these document defaults, so run parsing must fall back here (the
// restaurant-menu bug: dish names omitted /FontSize because they used the default 12.0, and the
// old code fell back to the first /FontSize found anywhere in the engine data instead).
struct PsdTextEngineDefaults {
  double font_size{12.0};
  bool auto_leading{true};
  double leading{0.0};
  double tracking{0.0};
  double horizontal_scale{1.0};
  double vertical_scale{1.0};
  std::optional<int> font_index;
  bool faux_bold{false};
  bool faux_italic{false};
  std::optional<RgbColor> fill_color;
  double auto_leading_fraction{1.2};
};

std::uint32_t checked_u32(std::size_t value, const char* field) {
  if (value > 0xFFFFFFFFULL) {
    throw std::runtime_error(std::string("PSD field is too large: ") + field);
  }
  return static_cast<std::uint32_t>(value);
}

std::uint16_t checked_u16(std::size_t value, const char* field) {
  if (value > 0xFFFFULL) {
    throw std::runtime_error(std::string("PSD field is too large: ") + field);
  }
  return static_cast<std::uint16_t>(value);
}

void check_write_dimensions(const Document& document, bool large_document) {
  const auto limit = large_document ? kMaxPsbDimension : kMaxPsdDimension;
  if (document.width() > limit || document.height() > limit) {
    throw std::runtime_error(large_document
                                 ? "PSB documents are limited to 300,000 pixels per side"
                                 : "Documents over 30,000 pixels per side must be saved as PSB (.psb)");
  }
}

void skip_length_block(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u32();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  reader.skip(length);
}

std::vector<std::uint8_t> read_length_block(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u32();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  return reader.read_bytes(length);
}

PixelFormat format_from_header(const Header& header) {
  if (header.depth != 8) {
    throw std::runtime_error("The starter PSD reader currently supports 8-bit files only");
  }
  if (header.color_mode != kColorModeRgb && header.color_mode != kColorModeCmyk) {
    throw std::runtime_error("The starter PSD reader currently supports RGB and CMYK files only");
  }
  if (header.channels > kMaximumPhotoshopChannelCount) {
    throw std::runtime_error("PSD files cannot contain more than 56 channels");
  }
  if (header.color_mode == kColorModeRgb && header.channels < 3) {
    throw std::runtime_error("RGB PSD file must contain at least 3 channels");
  }
  if (header.color_mode == kColorModeCmyk && header.channels < 4) {
    throw std::runtime_error("CMYK PSD file must contain at least 4 channels");
  }
  return PixelFormat::rgb8();
}

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open PSD file for reading");
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}

void write_file_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open PSD file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// encode_packbits_row moved to psd_descriptor.{hpp,cpp} (shared with the ILBM writer).

// RLE row byte counts are u16 in PSD and u32 in PSB (wide_rle_counts).
std::vector<std::uint8_t> encode_packbits_rows(std::span<const std::uint8_t> planar_channels,
                                               std::int32_t width, std::int32_t height,
                                               std::uint16_t channel_count, bool wide_rle_counts) {
  if (width < 0 || height < 0) {
    throw std::runtime_error("PSD channel dimensions cannot be negative");
  }
  const auto row_width = static_cast<std::size_t>(width);
  const auto row_count = static_cast<std::size_t>(height) * static_cast<std::size_t>(channel_count);
  const auto channel_pixels = row_width * static_cast<std::size_t>(height);
  const auto expected_size = channel_pixels * static_cast<std::size_t>(channel_count);
  if (planar_channels.size() != expected_size) {
    throw std::runtime_error("PSD channel data length does not match its dimensions");
  }

  std::vector<std::vector<std::uint8_t>> rows;
  rows.reserve(row_count);
  const auto max_row_bytes = wide_rle_counts ? 0xFFFFFFFFULL : 0xFFFFULL;
  for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
    const auto channel_offset = static_cast<std::size_t>(channel) * channel_pixels;
    for (std::int32_t y = 0; y < height; ++y) {
      const auto row_offset = channel_offset + static_cast<std::size_t>(y) * row_width;
      auto encoded = encode_packbits_row(planar_channels.subspan(row_offset, row_width));
      if (encoded.size() > max_row_bytes) {
        throw std::runtime_error("PSD PackBits row is too large");
      }
      rows.push_back(std::move(encoded));
    }
  }

  BigEndianWriter writer;
  for (const auto& row : rows) {
    if (wide_rle_counts) {
      writer.write_u32(static_cast<std::uint32_t>(row.size()));
    } else {
      writer.write_u16(static_cast<std::uint16_t>(row.size()));
    }
  }
  for (const auto& row : rows) {
    writer.write_bytes(row);
  }
  return writer.bytes();
}

EncodedChannel encode_channel(std::uint16_t id, std::int32_t width, std::int32_t height,
                              std::span<const std::uint8_t> raw_data, bool wide_rle_counts) {
  auto rle_data = encode_packbits_rows(raw_data, width, height, 1, wide_rle_counts);
  if (rle_data.size() < raw_data.size()) {
    return EncodedChannel{id, width, height, kCompressionRle, std::move(rle_data)};
  }

  return EncodedChannel{id, width, height, kCompressionRaw,
                        std::vector<std::uint8_t>(raw_data.begin(), raw_data.end())};
}

std::vector<std::uint8_t> planar_rgb8_data(const PixelBuffer& pixels) {
  if (pixels.format() != PixelFormat::rgb8()) {
    throw std::runtime_error("PSD composite export requires RGB8 pixels");
  }

  const auto channel_pixels = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  std::vector<std::uint8_t> planar(channel_pixels * 3U);
  for (std::uint16_t channel = 0; channel < 3; ++channel) {
    const auto channel_offset = static_cast<std::size_t>(channel) * channel_pixels;
    for (std::size_t i = 0; i < channel_pixels; ++i) {
      planar[channel_offset + i] = pixels.data()[i * 3U + channel];
    }
  }
  return planar;
}

void write_rgb8_image_data(BigEndianWriter& writer, const PixelBuffer& pixels, bool wide_rle_counts) {
  const auto raw_data = planar_rgb8_data(pixels);
  const auto rle_data = encode_packbits_rows(raw_data, pixels.width(), pixels.height(), 3, wide_rle_counts);
  if (rle_data.size() < raw_data.size()) {
    writer.write_u16(kCompressionRle);
    writer.write_bytes(rle_data);
    return;
  }

  writer.write_u16(kCompressionRaw);
  writer.write_bytes(raw_data);
}

// A flat export whose single pixel layer carries an enabled imported-alpha mask
// preserves that mask as a saved composite channel ("Alpha 1"). On PSD reopen it is
// a DocumentChannel, never an applied layer mask. This is the eligibility check.
[[nodiscard]] const Layer* document_alpha_mask_layer(const Document& document) noexcept {
  if (document.layers().size() != 1) {
    return nullptr;
  }
  const Layer& layer = document.layers().front();
  if (layer.kind() != LayerKind::Pixel || !layer.children().empty() || !layer_mask_is_document_alpha(layer)) {
    return nullptr;
  }
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled || mask->pixels.empty() ||
      mask->pixels.format() != PixelFormat::gray8()) {
    return nullptr;
  }
  const auto width = document.width();
  const auto height = document.height();
  if (layer.pixels().width() != width || layer.pixels().height() != height) {
    return nullptr;
  }
  const auto pixel_format = layer.pixels().format();
  if (pixel_format.bit_depth != BitDepth::UInt8 ||
      (pixel_format != PixelFormat::rgb8() && pixel_format != PixelFormat::rgba8())) {
    return nullptr;
  }
  return &layer;
}

struct DocumentAlphaComposite {
  PixelBuffer rgb;                  // canvas-sized RGB8 (unmasked, original colors)
  std::vector<std::uint8_t> alpha;  // canvas-sized grayscale, row-major
  std::string_view channel_name;    // 1006 label: "Alpha 1" (saved channel) or "Transparency"
};

void append_document_channels_for_write(
    const Document& document, std::vector<std::span<const std::uint8_t>>& planes,
    std::vector<CompositeChannelInfo>& channel_info) {
  planes.reserve(planes.size() + document.channels().size());
  channel_info.reserve(channel_info.size() + document.channels().size());
  for (const auto& channel : document.channels()) {
    const auto& pixels = channel.pixels();
    if (pixels.format() != PixelFormat::gray8() || pixels.width() != document.width() ||
        pixels.height() != document.height()) {
      throw std::runtime_error("PSD saved channels must be full-canvas 8-bit grayscale images");
    }
    planes.emplace_back(pixels.data());
    channel_info.push_back(CompositeChannelInfo{channel.name(), false,
                                                channel.kind() == DocumentChannelKind::Alpha,
                                                channel.photoshop_identifier(),
                                                channel.display_info(),
                                                channel.raw_photoshop_display_info()});
  }
}

void check_composite_channel_limit(std::size_t extra_channel_count) {
  if (3U + extra_channel_count > kMaximumPhotoshopChannelCount) {
    throw std::runtime_error("PSD files support at most 56 total channels, including merged transparency");
  }
}

// Builds the composite RGB (with the layer's original colors, NOT the masked flatten) and
// the canvas-sized alpha plane sampled from the layer mask, honoring its bounds and
// default color. Returns nullopt when the document is not eligible (see eligibility above).
[[nodiscard]] std::optional<DocumentAlphaComposite> document_alpha_composite(const Document& document) {
  const Layer* layer = document_alpha_mask_layer(document);
  if (layer == nullptr) {
    return std::nullopt;
  }

  const auto width = document.width();
  const auto height = document.height();
  const auto channel_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  // RGB comes straight from the layer's own (unmasked) pixels so the colors beneath the
  // mask are preserved. pixel()[0..2] is the RGB triple for both rgb8 and rgba8 sources.
  PixelBuffer rgb(width, height, PixelFormat::rgb8());
  const PixelBuffer& source = layer->pixels();
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const std::uint8_t* src = source.pixel(x, y);
      std::uint8_t* dst = rgb.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    }
  }

  const LayerMask& mask = *layer->mask();
  std::vector<std::uint8_t> alpha(channel_pixels, mask.default_color);
  for (std::int32_t my = 0; my < mask.pixels.height(); ++my) {
    const std::int32_t doc_y = mask.bounds.y + my;
    if (doc_y < 0 || doc_y >= height) {
      continue;
    }
    for (std::int32_t mx = 0; mx < mask.pixels.width(); ++mx) {
      const std::int32_t doc_x = mask.bounds.x + mx;
      if (doc_x < 0 || doc_x >= width) {
        continue;
      }
      alpha[static_cast<std::size_t>(doc_y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(doc_x)] = mask.pixels.pixel(mx, my)[0];
    }
  }
  return DocumentAlphaComposite{std::move(rgb), std::move(alpha), "Alpha 1"};
}

// A document whose merged flatten has any transparent pixel writes its composite the
// way Photoshop does: four channels, with the merged coverage as the extra channel and
// resource 1006 naming it "Transparency". Without this, the stored composite mattes
// the canvas onto black, and readers that trust it (Patchy's own smart-object preview
// decode, external thumbnailers) show opaque black where the canvas was transparent.
// A fully opaque flatten returns an empty channel_name: those saves keep the
// historical 3-channel bytes bit for bit (requesting the alpha plane does not change
// the compositor's RGB output).
[[nodiscard]] DocumentAlphaComposite merged_flatten_composite(const Document& document) {
  std::vector<std::uint8_t> alpha;
  auto rgb = Compositor{}.flatten_rgb8(document, &alpha);
  const auto transparent =
      std::any_of(alpha.begin(), alpha.end(), [](std::uint8_t coverage) { return coverage != 255; });
  if (!transparent) {
    alpha.clear();
    return DocumentAlphaComposite{std::move(rgb), std::move(alpha), std::string_view{}};
  }
  return DocumentAlphaComposite{std::move(rgb), std::move(alpha), "Transparency"};
}

// Writes RGB followed by any number of full-canvas grayscale planes. The merged
// image RLE layout has one compression marker, all row counts for all planes, then
// all encoded rows. Build the RLE candidate row-by-row so adding many saved
// channels does not require another full planar copy of every channel.
void write_rgb8_image_data_with_extra_channels(
    BigEndianWriter& writer, const PixelBuffer& pixels,
    std::span<const std::span<const std::uint8_t>> extra_channels, bool wide_rle_counts) {
  if (pixels.format() != PixelFormat::rgb8()) {
    throw std::runtime_error("PSD composite export requires RGB8 pixels");
  }
  const auto width = static_cast<std::size_t>(pixels.width());
  const auto height = static_cast<std::size_t>(pixels.height());
  const auto channel_pixels = width * height;
  for (const auto channel : extra_channels) {
    if (channel.size() != channel_pixels) {
      throw std::runtime_error("PSD saved channel dimensions do not match the document");
    }
  }

  const auto channel_count = 3U + extra_channels.size();
  std::vector<std::uint32_t> row_lengths;
  row_lengths.reserve(height * channel_count);
  std::vector<std::uint8_t> encoded_rows;
  std::vector<std::uint8_t> rgb_row(width);
  const auto max_row_bytes = wide_rle_counts ? 0xFFFFFFFFULL : 0xFFFFULL;
  const auto append_encoded_row = [&](std::span<const std::uint8_t> row) {
    auto encoded = encode_packbits_row(row);
    if (encoded.size() > max_row_bytes) {
      throw std::runtime_error("PSD PackBits row is too large");
    }
    row_lengths.push_back(static_cast<std::uint32_t>(encoded.size()));
    encoded_rows.insert(encoded_rows.end(), encoded.begin(), encoded.end());
  };

  for (std::size_t component = 0; component < 3U; ++component) {
    for (std::size_t y = 0; y < height; ++y) {
      const auto first_pixel = y * width;
      for (std::size_t x = 0; x < width; ++x) {
        rgb_row[x] = pixels.data()[(first_pixel + x) * 3U + component];
      }
      append_encoded_row(rgb_row);
    }
  }
  for (const auto channel : extra_channels) {
    for (std::size_t y = 0; y < height; ++y) {
      append_encoded_row(channel.subspan(y * width, width));
    }
  }

  const auto count_width = wide_rle_counts ? 4U : 2U;
  const auto rle_size = row_lengths.size() * count_width + encoded_rows.size();
  const auto raw_size = channel_pixels * channel_count;
  if (rle_size < raw_size) {
    writer.write_u16(kCompressionRle);
    for (const auto length : row_lengths) {
      if (wide_rle_counts) {
        writer.write_u32(length);
      } else {
        writer.write_u16(static_cast<std::uint16_t>(length));
      }
    }
    writer.write_bytes(encoded_rows);
    return;
  }

  writer.write_u16(kCompressionRaw);
  for (std::size_t component = 0; component < 3U; ++component) {
    for (std::size_t pixel = 0; pixel < channel_pixels; ++pixel) {
      writer.write_u8(pixels.data()[pixel * 3U + component]);
    }
  }
  for (const auto channel : extra_channels) {
    writer.write_bytes(channel);
  }
}

std::vector<std::uint8_t> read_channel_data(BigEndianReader& reader, std::uint16_t compression, std::int32_t width,
                                            std::int32_t height, bool wide_rle_counts) {
  if (compression == kCompressionRaw) {
    const auto byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return reader.read_bytes(byte_count);
  }

  if (compression != kCompressionRle) {
    throw std::runtime_error("Unsupported PSD channel compression");
  }

  std::vector<std::uint32_t> row_lengths;
  row_lengths.reserve(static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    row_lengths.push_back(wide_rle_counts ? reader.read_u32() : reader.read_u16());
  }

  std::vector<std::uint8_t> channel;
  channel.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    const auto row = reader.read_bytes(row_lengths[static_cast<std::size_t>(y)]);
    auto decoded = decode_packbits(row, static_cast<std::size_t>(width));
    channel.insert(channel.end(), decoded.begin(), decoded.end());
  }
  return channel;
}

std::vector<std::uint8_t> read_rle_channel_from_counts(BigEndianReader& reader,
                                                       std::span<const std::uint32_t> row_lengths,
                                                       std::int32_t width) {
  std::vector<std::uint8_t> channel;
  channel.reserve(static_cast<std::size_t>(width) * row_lengths.size());
  for (const auto row_length : row_lengths) {
    const auto row = reader.read_bytes(row_length);
    auto decoded = decode_packbits(row, static_cast<std::size_t>(width));
    channel.insert(channel.end(), decoded.begin(), decoded.end());
  }
  return channel;
}

bool is_cmyk_color_mode(std::uint16_t color_mode) noexcept {
  return color_mode == kColorModeCmyk;
}

std::uint8_t photoshop_cmyk_to_rgb_component(std::uint8_t colorant, std::uint8_t black) noexcept {
  // Photoshop stores CMYK channels as inverted ink values: 255 is 0% ink and 0 is 100% ink.
  return static_cast<std::uint8_t>((static_cast<int>(colorant) * static_cast<int>(black)) / 255);
}

void write_rgb_from_cmyk(PixelBuffer& pixels, std::size_t pixel_index, std::uint8_t cyan, std::uint8_t magenta,
                         std::uint8_t yellow, std::uint8_t black) {
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data() + pixel_index * channels;
  target[0] = photoshop_cmyk_to_rgb_component(cyan, black);
  target[1] = photoshop_cmyk_to_rgb_component(magenta, black);
  target[2] = photoshop_cmyk_to_rgb_component(yellow, black);
}

// CMYK-mode documents also carry CMYK colors in descriptors (lfx2 effect colors) and text
// engine data, as ink fractions. Convert with the same naive mix as the pixel decode above
// so effect/text colors keep their relationship to the converted pixels.
RgbColor rgb_from_cmyk_ink_fractions(double cyan, double magenta, double yellow, double black) {
  const auto paper = 1.0 - std::clamp(black, 0.0, 1.0);
  const auto component = [paper](double ink) {
    return static_cast<std::uint8_t>(
        std::clamp(std::lround((1.0 - std::clamp(ink, 0.0, 1.0)) * paper * 255.0), 0L, 255L));
  };
  return RgbColor{component(cyan), component(magenta), component(yellow)};
}

// Threaded through the descriptor and text-engine parsers so their CMYK colors convert
// through the SAME transform as the pixel decode (ink fractions are quantized to the
// inverted 8-bit channel convention first); without a usable profile both fall back to
// the same naive mix. Keeping the two paths identical preserves the relationship between
// effect/text colors and the converted pixels.
struct CmykColorConverter {
  const CmykToRgbTransform* icc{nullptr};

  [[nodiscard]] RgbColor rgb_from_ink(double cyan, double magenta, double yellow,
                                      double black) const {
    if (icc != nullptr) {
      const auto inverted = [](double ink) {
        return static_cast<std::uint8_t>(
            std::clamp(std::lround((1.0 - std::clamp(ink, 0.0, 1.0)) * 255.0), 0L, 255L));
      };
      return icc->convert_single(inverted(cyan), inverted(magenta), inverted(yellow),
                                 inverted(black));
    }
    return rgb_from_cmyk_ink_fractions(cyan, magenta, yellow, black);
  }
};

// Converts decoded planar inverted-CMYK channels into the RGB(A) pixel buffer: through the
// document's embedded ICC profile when one is usable (matching Photoshop), the naive ink
// mix otherwise. Large buffers convert in parallel strips; the ICC transform is cache-free
// fixed-point math, so the output is byte-identical regardless of chunking or thread count.
void convert_cmyk_planes_to_rgb(PixelBuffer& pixels, const std::uint8_t* cyan,
                                const std::uint8_t* magenta, const std::uint8_t* yellow,
                                const std::uint8_t* black, std::size_t pixel_count,
                                const CmykToRgbTransform* icc) {
  if (icc == nullptr) {
    for (std::size_t i = 0; i < pixel_count; ++i) {
      write_rgb_from_cmyk(pixels, i, cyan[i], magenta[i], yellow[i], black[i]);
    }
    return;
  }
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data();
  const auto convert_range = [&](std::size_t begin, std::size_t end) {
    constexpr std::size_t kChunkPixels = 65536;
    std::vector<std::uint8_t> cmyk(std::min(kChunkPixels, end - begin) * 4U);
    std::vector<std::uint8_t> rgb(std::min(kChunkPixels, end - begin) * 3U);
    for (std::size_t start = begin; start < end; start += kChunkPixels) {
      const auto count = std::min(kChunkPixels, end - start);
      for (std::size_t i = 0; i < count; ++i) {
        cmyk[i * 4U + 0U] = cyan[start + i];
        cmyk[i * 4U + 1U] = magenta[start + i];
        cmyk[i * 4U + 2U] = yellow[start + i];
        cmyk[i * 4U + 3U] = black[start + i];
      }
      icc->convert(cmyk.data(), rgb.data(), count);
      for (std::size_t i = 0; i < count; ++i) {
        auto* pixel = target + (start + i) * channels;
        pixel[0] = rgb[i * 3U + 0U];
        pixel[1] = rgb[i * 3U + 1U];
        pixel[2] = rgb[i * 3U + 2U];
      }
    }
  };
  constexpr std::size_t kParallelThresholdPixels = 4U << 20U;
  if (pixel_count < kParallelThresholdPixels) {
    convert_range(0, pixel_count);
    return;
  }
  const auto worker_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
  const auto strip = (pixel_count + worker_count - 1) / worker_count;
  std::vector<std::future<void>> workers;
  workers.reserve(worker_count);
  for (std::size_t begin = 0; begin < pixel_count; begin += strip) {
    const auto end = std::min(pixel_count, begin + strip);
    workers.push_back(std::async(std::launch::async, convert_range, begin, end));
  }
  for (auto& worker : workers) {
    worker.get();
  }
}

std::vector<std::vector<std::uint8_t>> read_flat_image_channels(BigEndianReader& reader, const Header& header,
                                                                std::uint16_t compression) {
  std::vector<std::vector<std::uint8_t>> channels;
  channels.reserve(header.channels);
  const auto width = static_cast<std::int32_t>(header.width);
  const auto height = static_cast<std::int32_t>(header.height);

  if (compression == kCompressionRaw) {
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      channels.push_back(read_channel_data(reader, compression, width, height, header.large_document));
    }
    return channels;
  }

  if (compression == kCompressionRle) {
    std::vector<std::uint32_t> row_lengths;
    row_lengths.reserve(static_cast<std::size_t>(header.channels) * static_cast<std::size_t>(header.height));
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      for (std::uint32_t y = 0; y < header.height; ++y) {
        row_lengths.push_back(header.large_document ? reader.read_u32() : reader.read_u16());
      }
    }
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      const auto offset = static_cast<std::size_t>(channel) * static_cast<std::size_t>(header.height);
      const auto rows =
          std::span<const std::uint32_t>(row_lengths.data() + offset, static_cast<std::size_t>(header.height));
      channels.push_back(read_rle_channel_from_counts(reader, rows, width));
    }
    return channels;
  }

  throw std::runtime_error("Unsupported PSD composite compression");
}

// Reads only a contiguous suffix of the composite planes. Raw data can skip the
// color planes directly; RLE still needs the complete row-count table, but the
// encoded rows for unwanted planes are skipped without decoding or storing them.
std::vector<std::vector<std::uint8_t>> read_flat_image_channels_from(
    BigEndianReader& reader, const Header& header, std::uint16_t compression,
    std::uint16_t first_channel) {
  if (first_channel > header.channels) {
    throw std::runtime_error("Invalid PSD saved channel index");
  }
  const auto width = static_cast<std::int32_t>(header.width);
  const auto height = static_cast<std::int32_t>(header.height);
  const auto channel_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<std::vector<std::uint8_t>> channels;
  channels.reserve(static_cast<std::size_t>(header.channels - first_channel));

  if (compression == kCompressionRaw) {
    const auto skip_bytes = channel_pixels * static_cast<std::size_t>(first_channel);
    if (skip_bytes > reader.remaining()) {
      throw std::runtime_error("PSD composite channel data is truncated");
    }
    reader.skip(skip_bytes);
    for (std::uint16_t channel = first_channel; channel < header.channels; ++channel) {
      channels.push_back(read_channel_data(reader, compression, width, height, header.large_document));
    }
    return channels;
  }

  if (compression == kCompressionRle) {
    std::vector<std::uint32_t> row_lengths;
    row_lengths.reserve(static_cast<std::size_t>(header.channels) * static_cast<std::size_t>(header.height));
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      for (std::uint32_t y = 0; y < header.height; ++y) {
        row_lengths.push_back(header.large_document ? reader.read_u32() : reader.read_u16());
      }
    }
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      const auto offset = static_cast<std::size_t>(channel) * static_cast<std::size_t>(header.height);
      const auto rows =
          std::span<const std::uint32_t>(row_lengths.data() + offset, static_cast<std::size_t>(header.height));
      if (channel < first_channel) {
        std::size_t encoded_size = 0;
        for (const auto row_length : rows) {
          if (encoded_size > reader.remaining() ||
              static_cast<std::size_t>(row_length) > reader.remaining() - encoded_size) {
            throw std::runtime_error("PSD composite channel data is truncated");
          }
          encoded_size += row_length;
        }
        if (encoded_size > reader.remaining()) {
          throw std::runtime_error("PSD composite channel data is truncated");
        }
        reader.skip(encoded_size);
      } else {
        channels.push_back(read_rle_channel_from_counts(reader, rows, width));
      }
    }
    return channels;
  }

  throw std::runtime_error("Unsupported PSD composite compression");
}

bool is_source_color_channel(std::uint16_t channel_id, std::uint16_t source_color_mode) noexcept {
  return is_cmyk_color_mode(source_color_mode) ? channel_id <= kChannelBlack : channel_id <= kChannelBlue;
}


std::uint32_t read_section_length(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u32();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  return length;
}

// PSB widens a few section lengths (layer-and-mask info, layer info, per-layer channel
// data) to 8 bytes; large_document selects the width at each such call site.
std::uint64_t read_section_length_u64(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u64();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  return length;
}

std::uint32_t write_length_prefixed_block(BigEndianWriter& writer, const std::vector<std::uint8_t>& payload) {
  writer.write_u32(checked_u32(payload.size(), "section length"));
  writer.write_bytes(payload);
  return static_cast<std::uint32_t>(payload.size());
}

void write_signature(BigEndianWriter& writer, const std::array<char, 4>& signature) {
  for (const auto ch : signature) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
}

std::vector<std::uint8_t> unescape_engine_bytes(std::span<const std::uint8_t> bytes) {
  std::vector<std::uint8_t> unescaped;
  unescaped.reserve(bytes.size());
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto byte = bytes[index];
    const auto ch = static_cast<char>(byte);
    if (ch != '\\') {
      unescaped.push_back(byte);
      continue;
    }
    if (index + 1 >= bytes.size()) {
      break;
    }

    const auto next = static_cast<char>(bytes[++index]);
    if (next >= '0' && next <= '7') {
      int value = next - '0';
      for (int digit = 0; digit < 2 && index + 1 < bytes.size(); ++digit) {
        const auto octal = static_cast<char>(bytes[index + 1]);
        if (octal < '0' || octal > '7') {
          break;
        }
        value = value * 8 + (octal - '0');
        ++index;
      }
      unescaped.push_back(static_cast<std::uint8_t>(value & 0xFF));
      continue;
    }

    switch (next) {
      case 'r':
        unescaped.push_back('\n');
        break;
      case 'n':
        unescaped.push_back('\n');
        break;
      case 't':
        unescaped.push_back('\t');
        break;
      case '\\':
      case '(':
      case ')':
        unescaped.push_back(static_cast<std::uint8_t>(next));
        break;
      default:
        unescaped.push_back(static_cast<std::uint8_t>(next));
        break;
    }
  }
  return unescaped;
}

std::vector<std::uint16_t> utf8_to_utf16(std::string_view text) {
  std::vector<std::uint16_t> units;
  units.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = 0x3FU;
    std::size_t consumed = 1;
    if (lead < 0x80U) {
      codepoint = lead;
    } else if ((lead & 0xE0U) == 0xC0U && index + 1 < text.size()) {
      codepoint = ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(text[index + 1]) & 0x3FU);
      consumed = 2;
    } else if ((lead & 0xF0U) == 0xE0U && index + 2 < text.size()) {
      codepoint = ((lead & 0x0FU) << 12U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 2]) & 0x3FU);
      consumed = 3;
    } else if ((lead & 0xF8U) == 0xF0U && index + 3 < text.size()) {
      codepoint = ((lead & 0x07U) << 18U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12U) |
                  ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 3]) & 0x3FU);
      consumed = 4;
    }

    if (codepoint <= 0xFFFFU) {
      units.push_back(static_cast<std::uint16_t>(codepoint));
    } else {
      codepoint -= 0x10000U;
      units.push_back(static_cast<std::uint16_t>(0xD800U + (codepoint >> 10U)));
      units.push_back(static_cast<std::uint16_t>(0xDC00U + (codepoint & 0x3FFU)));
    }
    index += consumed;
  }
  return units;
}

std::optional<std::string> read_unicode_string_payload(std::span<const std::uint8_t> payload) {
  if (payload.size() < 4) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  const auto code_unit_count = reader.read_u32();
  if (code_unit_count > reader.remaining() / 2U) {
    return std::nullopt;
  }

  std::string decoded;
  for (std::uint32_t index = 0; index < code_unit_count; ++index) {
    auto codepoint = static_cast<std::uint32_t>(reader.read_u16());
    if (codepoint == 0) {
      continue;
    }
    if (codepoint >= 0xD800U && codepoint <= 0xDBFFU && index + 1 < code_unit_count) {
      const auto low = static_cast<std::uint32_t>(reader.read_u16());
      ++index;
      if (low >= 0xDC00U && low <= 0xDFFFU) {
        codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
      } else {
        codepoint = '?';
      }
    }
    append_utf8(decoded, codepoint);
  }
  if (decoded.empty()) {
    return std::nullopt;
  }
  return decoded;
}

std::vector<std::uint8_t> unicode_string_payload(std::string_view text) {
  const auto units = utf8_to_utf16(text);
  BigEndianWriter writer;
  writer.write_u32(checked_u32(units.size(), "unicode string length"));
  for (const auto unit : units) {
    writer.write_u16(unit);
  }
  return writer.bytes();
}

std::string decode_engine_string(std::span<const std::uint8_t> bytes) {
  const auto unescaped = unescape_engine_bytes(bytes);
  if (unescaped.size() >= 2 && unescaped[0] == 0xFEU && unescaped[1] == 0xFFU) {
    std::string decoded;
    for (std::size_t index = 2; index + 1 < unescaped.size(); index += 2) {
      const auto codepoint = (static_cast<std::uint32_t>(unescaped[index]) << 8U) |
                             static_cast<std::uint32_t>(unescaped[index + 1]);
      append_utf8(decoded, codepoint);
    }
    return decoded;
  }
  if (unescaped.size() >= 2 && unescaped[0] == 0xFFU && unescaped[1] == 0xFEU) {
    std::string decoded;
    for (std::size_t index = 2; index + 1 < unescaped.size(); index += 2) {
      const auto codepoint = (static_cast<std::uint32_t>(unescaped[index + 1]) << 8U) |
                             static_cast<std::uint32_t>(unescaped[index]);
      append_utf8(decoded, codepoint);
    }
    return decoded;
  }
  return std::string(unescaped.begin(), unescaped.end());
}

std::string normalize_photoshop_text(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto ch = text[index];
    if (ch == '\r') {
      normalized.push_back('\n');
      if (index + 1 < text.size() && text[index + 1] == '\n') {
        ++index;
      }
      continue;
    }
    if (ch == '\n' || ch == '\x03') {
      normalized.push_back('\n');
      continue;
    }
    normalized.push_back(ch);
  }
  while (!normalized.empty() && normalized.back() == '\n') {
    normalized.pop_back();
  }
  return normalized;
}

bool payload_contains_ascii(std::span<const std::uint8_t> payload, std::string_view marker) {
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  return std::search(begin, end, marker.begin(), marker.end()) != end;
}

bool payload_has_patchy_generated_text_signature(std::span<const std::uint8_t> payload) {
  return payload_contains_ascii(
      payload, "/KinsokuSet [ ] /MojiKumiSet [ ] /TheNormalStyleSheet 0 /TheNormalParagraphSheet 0");
}

std::optional<std::string> extract_engine_data_text(std::span<const std::uint8_t> payload) {
  constexpr std::string_view marker = "/Text";
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  auto found = std::search(begin, end, marker.begin(), marker.end());
  while (found != end) {
    auto cursor = found + static_cast<std::ptrdiff_t>(marker.size());
    while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
      ++cursor;
    }
    if (cursor < end && *cursor == '(') {
      ++cursor;
      const auto text_begin = cursor;
      int depth = 1;
      bool escaped = false;
      while (cursor < end && depth > 0) {
        const auto ch = *cursor;
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '(') {
          ++depth;
        } else if (ch == ')') {
          --depth;
          if (depth == 0) {
            break;
          }
        }
        ++cursor;
      }
      if (cursor > text_begin) {
        auto text =
            decode_engine_string(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text_begin),
                                                               static_cast<std::size_t>(cursor - text_begin)));
        text = normalize_photoshop_text(text);
        if (!text.empty()) {
          return text;
        }
      }
    }
    found = std::search(cursor, end, marker.begin(), marker.end());
  }
  return std::nullopt;
}

std::optional<int> extract_engine_data_font_size(std::span<const std::uint8_t> payload) {
  constexpr std::string_view marker = "/FontSize";
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  auto found = std::search(begin, end, marker.begin(), marker.end());
  while (found != end) {
    auto cursor = found + static_cast<std::ptrdiff_t>(marker.size());
    while (cursor < end &&
           (std::isspace(static_cast<unsigned char>(*cursor)) != 0 || *cursor == '[' || *cursor == '(')) {
      ++cursor;
    }
    if (cursor < end) {
      const auto remaining = static_cast<std::size_t>(end - cursor);
      const std::string number(cursor, cursor + std::min<std::size_t>(remaining, 48U));
      char* parsed_end = nullptr;
      const auto parsed = std::strtod(number.c_str(), &parsed_end);
      if (parsed_end != number.c_str() && std::isfinite(parsed) && parsed > 0.0) {
        return std::clamp(static_cast<int>(std::lround(parsed)), 1, kMaxTextSizePixels);
      }
    }
    found = std::search(cursor, end, marker.begin(), marker.end());
  }
  return std::nullopt;
}

std::optional<double> first_engine_number_after(std::string_view text, std::string_view marker) {
  const auto found = text.find(marker);
  if (found == std::string_view::npos) {
    return std::nullopt;
  }

  auto cursor = found + marker.size();
  while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
  if (cursor >= text.size()) {
    return std::nullopt;
  }

  const std::string number(text.substr(cursor, std::min<std::size_t>(text.size() - cursor, 48U)));
  char* parsed_end = nullptr;
  const auto parsed = std::strtod(number.c_str(), &parsed_end);
  if (parsed_end == number.c_str() || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::vector<double> parse_engine_number_array(std::string_view text) {
  std::vector<double> numbers;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    while (cursor < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == ',')) {
      ++cursor;
    }
    if (cursor >= text.size()) {
      break;
    }

    const std::string number(text.substr(cursor, std::min<std::size_t>(text.size() - cursor, 48U)));
    char* parsed_end = nullptr;
    const auto parsed = std::strtod(number.c_str(), &parsed_end);
    if (parsed_end == number.c_str()) {
      ++cursor;
      continue;
    }
    if (std::isfinite(parsed)) {
      numbers.push_back(parsed);
    }
    cursor += static_cast<std::size_t>(parsed_end - number.c_str());
  }
  return numbers;
}

std::uint8_t engine_color_component(double value, bool normalized) {
  const auto scaled = normalized ? value * 255.0 : value;
  return static_cast<std::uint8_t>(std::clamp(std::lround(scaled), 0L, 255L));
}

// Engine-data color /Type 1 is [alpha, red, green, blue]; /Type 2 (CMYK-mode documents)
// is [alpha, cyan, magenta, yellow, black] as 0-1 ink fractions.
std::optional<RgbColor> rgb_color_from_engine_values(int type, const std::vector<double>& values,
                                                     const CmykColorConverter& cmyk) {
  if (type == 2) {
    if (values.size() < 5U ||
        std::any_of(values.begin() + 1, values.begin() + 5, [](double value) { return !std::isfinite(value); })) {
      return std::nullopt;
    }
    return cmyk.rgb_from_ink(values[1], values[2], values[3], values[4]);
  }
  if (values.size() < 4U) {
    return std::nullopt;
  }

  const auto red = values[1];
  const auto green = values[2];
  const auto blue = values[3];
  if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
    return std::nullopt;
  }

  const auto normalized = red <= 1.0 && green <= 1.0 && blue <= 1.0;
  return RgbColor{engine_color_component(red, normalized), engine_color_component(green, normalized),
                  engine_color_component(blue, normalized)};
}

std::optional<RgbColor> extract_engine_data_fill_color(std::span<const std::uint8_t> payload,
                                                       const CmykColorConverter& cmyk) {
  constexpr std::string_view marker = "/FillColor";
  constexpr std::string_view values_marker = "/Values";
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());

  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    const auto block_start = found + marker.size();
    const auto block_close = text.find(">>", block_start);
    const auto block = block_close == std::string_view::npos
                           ? text.substr(found)
                           : text.substr(found, block_close + 2U - found);
    const auto type = first_engine_number_after(block, "/Type");
    const auto type_value = type.has_value() ? static_cast<int>(std::lround(*type)) : 1;
    if (type_value != 1 && type_value != 2) {
      found = text.find(marker, block_start);
      continue;
    }

    const auto values = block.find(values_marker);
    if (values != std::string_view::npos) {
      const auto open = block.find('[', values + values_marker.size());
      const auto close = open == std::string_view::npos ? std::string_view::npos : block.find(']', open + 1U);
      if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
        if (auto color = rgb_color_from_engine_values(
                type_value, parse_engine_number_array(block.substr(open + 1U, close - open - 1U)), cmyk);
            color.has_value()) {
          return color;
        }
      }
    }

    found = text.find(marker, block_start);
  }
  return std::nullopt;
}

std::optional<int> extract_engine_data_anti_alias(std::span<const std::uint8_t> payload) {
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  if (const auto value = first_engine_number_after(text, "/AntiAlias"); value.has_value()) {
    return std::clamp(static_cast<int>(std::lround(*value)), 0, 16);
  }
  return std::nullopt;
}

std::string rgb_hex_color(RgbColor color) {
  constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string result = "#000000";
  result[1] = digits[color.red >> 4U];
  result[2] = digits[color.red & 0x0FU];
  result[3] = digits[color.green >> 4U];
  result[4] = digits[color.green & 0x0FU];
  result[5] = digits[color.blue >> 4U];
  result[6] = digits[color.blue & 0x0FU];
  return result;
}

int hex_digit_value(char ch) noexcept {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + ch - 'A';
  }
  return -1;
}

std::optional<RgbColor> rgb_color_from_hex(std::string_view text) {
  if (text.size() != 7U || text[0] != '#') {
    return std::nullopt;
  }
  const auto pair_value = [](char high, char low) -> std::optional<std::uint8_t> {
    const auto hi = hex_digit_value(high);
    const auto lo = hex_digit_value(low);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    return static_cast<std::uint8_t>((hi << 4) | lo);
  };
  const auto red = pair_value(text[1], text[2]);
  const auto green = pair_value(text[3], text[4]);
  const auto blue = pair_value(text[5], text[6]);
  if (!red.has_value() || !green.has_value() || !blue.has_value()) {
    return std::nullopt;
  }
  return RgbColor{*red, *green, *blue};
}

std::string percent_decode(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '%' && index + 2 < text.size()) {
      const auto hi = hex_digit_value(text[index + 1]);
      const auto lo = hex_digit_value(text[index + 2]);
      if (hi >= 0 && lo >= 0) {
        decoded.push_back(static_cast<char>((hi << 4) | lo));
        index += 2;
        continue;
      }
    }
    decoded.push_back(text[index]);
  }
  return decoded;
}

std::string percent_encode(std::string_view text) {
  constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string encoded;
  encoded.reserve(text.size());
  for (const auto byte : text) {
    const auto value = static_cast<unsigned char>(byte);
    if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
        value == '-' || value == '_' || value == '.' || value == '~') {
      encoded.push_back(static_cast<char>(value));
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(digits[value >> 4U]);
    encoded.push_back(digits[value & 0x0FU]);
  }
  return encoded;
}

std::optional<double> engine_number_after_key(std::string_view text, std::string_view marker) {
  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    auto cursor = found + marker.size();
    if (cursor < text.size() &&
        (std::isalnum(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '_' || text[cursor] == '-')) {
      found = text.find(marker, cursor);
      continue;
    }
    while (cursor < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '[' || text[cursor] == '(')) {
      ++cursor;
    }
    if (cursor < text.size()) {
      const std::string number(text.substr(cursor, std::min<std::size_t>(text.size() - cursor, 48U)));
      char* parsed_end = nullptr;
      const auto parsed = std::strtod(number.c_str(), &parsed_end);
      if (parsed_end != number.c_str() && std::isfinite(parsed)) {
        return parsed;
      }
    }
    found = text.find(marker, cursor);
  }
  return std::nullopt;
}

bool engine_bool_after_key(std::string_view text, std::string_view marker, bool fallback = false) {
  const auto found = text.find(marker);
  if (found == std::string_view::npos) {
    return fallback;
  }
  auto cursor = found + marker.size();
  while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
  if (text.substr(cursor, 4) == "true") {
    return true;
  }
  if (text.substr(cursor, 5) == "false") {
    return false;
  }
  return fallback;
}

std::optional<std::pair<std::size_t, std::size_t>> balanced_range_after(std::string_view text,
                                                                        std::string_view marker,
                                                                        char open, char close) {
  const auto marker_pos = text.find(marker);
  if (marker_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto open_pos = text.find(open, marker_pos + marker.size());
  if (open_pos == std::string_view::npos) {
    return std::nullopt;
  }
  int depth = 0;
  bool escaped = false;
  for (std::size_t index = open_pos; index < text.size(); ++index) {
    const auto ch = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == open) {
      ++depth;
    } else if (ch == close) {
      --depth;
      if (depth == 0) {
        return std::pair{open_pos + 1U, index};
      }
    }
  }
  return std::nullopt;
}

std::vector<std::string_view> engine_dictionary_ranges(std::string_view text) {
  std::vector<std::string_view> ranges;
  std::size_t cursor = 0;
  while (cursor + 1 < text.size()) {
    const auto start = text.find("<<", cursor);
    if (start == std::string_view::npos) {
      break;
    }
    int depth = 0;
    for (std::size_t index = start; index + 1 < text.size(); ++index) {
      if (text[index] == '<' && text[index + 1] == '<') {
        ++depth;
        ++index;
        continue;
      }
      if (text[index] == '>' && text[index + 1] == '>') {
        --depth;
        ++index;
        if (depth == 0) {
          ranges.push_back(text.substr(start, index + 1U - start));
          cursor = index + 1U;
          break;
        }
      }
    }
    if (cursor <= start) {
      break;
    }
  }
  return ranges;
}

std::optional<std::vector<std::uint8_t>> engine_parenthesized_bytes_after(std::string_view text,
                                                                          std::string_view marker) {
  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    auto cursor = found + marker.size();
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }
    if (cursor < text.size() && text[cursor] == '(') {
      ++cursor;
      const auto begin = cursor;
      int depth = 1;
      bool escaped = false;
      while (cursor < text.size() && depth > 0) {
        const auto ch = text[cursor];
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '(') {
          ++depth;
        } else if (ch == ')') {
          --depth;
          if (depth == 0) {
            break;
          }
        }
        ++cursor;
      }
      if (cursor > begin) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(text.data() + begin);
        return std::vector<std::uint8_t>(data, data + (cursor - begin));
      }
    }
    found = text.find(marker, cursor);
  }
  return std::nullopt;
}

std::optional<RgbColor> extract_engine_fill_color_from_text(std::string_view text,
                                                            const CmykColorConverter& cmyk) {
  constexpr std::string_view marker = "/FillColor";
  constexpr std::string_view values_marker = "/Values";
  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    const auto block_start = found + marker.size();
    const auto block_close = text.find(">>", block_start);
    const auto block = block_close == std::string_view::npos ? text.substr(found)
                                                             : text.substr(found, block_close + 2U - found);
    const auto type = engine_number_after_key(block, "/Type");
    const auto type_value = type.has_value() ? static_cast<int>(std::lround(*type)) : 1;
    if (type_value != 1 && type_value != 2) {
      found = text.find(marker, block_start);
      continue;
    }

    const auto values = block.find(values_marker);
    if (values != std::string_view::npos) {
      const auto open = block.find('[', values + values_marker.size());
      const auto close = open == std::string_view::npos ? std::string_view::npos : block.find(']', open + 1U);
      if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
        if (auto color = rgb_color_from_engine_values(
                type_value, parse_engine_number_array(block.substr(open + 1U, close - open - 1U)), cmyk);
            color.has_value()) {
          return color;
        }
      }
    }
    found = text.find(marker, block_start);
  }
  return std::nullopt;
}

std::vector<std::string> extract_engine_font_names(std::span<const std::uint8_t> payload) {
  std::vector<std::string> fonts;
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto range = balanced_range_after(text, "/FontSet", '[', ']');
  if (!range.has_value()) {
    return fonts;
  }
  const auto block = text.substr(range->first, range->second - range->first);
  for (const auto dictionary : engine_dictionary_ranges(block)) {
    auto bytes = engine_parenthesized_bytes_after(dictionary, "/Name");
    if (!bytes.has_value()) {
      continue;
    }
    auto decoded = decode_engine_string(*bytes);
    if (!decoded.empty()) {
      fonts.push_back(std::move(decoded));
    }
  }
  return fonts;
}

struct ResolvedPhotoshopFont {
  std::string family{"Arial"};
  bool bold{false};
  bool italic{false};
};

std::string compact_font_key(std::string_view value) {
  std::string compact;
  compact.reserve(value.size());
  for (const auto ch : ascii_lower_copy(std::string(value))) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      compact.push_back(ch);
    }
  }
  return compact;
}

std::string compact_font_key_with_bt_suffix(std::string key) {
  const auto pos = key.find("bt");
  if (pos == std::string::npos || pos + 2U >= key.size()) {
    return key;
  }
  key.erase(pos, 2U);
  key += "bt";
  return key;
}

bool font_names_match(std::string_view lhs, std::string_view rhs) {
  const auto lhs_compact = compact_font_key(lhs);
  const auto rhs_compact = compact_font_key(rhs);
  return ascii_lower_copy(std::string(lhs)) == ascii_lower_copy(std::string(rhs)) ||
         lhs_compact == rhs_compact ||
         compact_font_key_with_bt_suffix(lhs_compact) == compact_font_key_with_bt_suffix(rhs_compact);
}

bool strip_ascii_ci_suffix(std::string& value, std::string_view suffix) {
  if (value.size() < suffix.size()) {
    return false;
  }
  const auto tail = std::string_view(value).substr(value.size() - suffix.size());
  if (ascii_lower_copy(std::string(tail)) != std::string(suffix)) {
    return false;
  }
  value.resize(value.size() - suffix.size());
  return true;
}

std::string humanized_postscript_family_name(std::string value) {
  if (value.empty() || value.find(' ') != std::string::npos) {
    return value;
  }

  std::string humanized;
  humanized.reserve(value.size() + 4U);
  for (std::size_t index = 0; index < value.size(); ++index) {
    const auto ch = value[index];
    if (index > 0U && std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      const auto previous = value[index - 1U];
      const auto next = index + 1U < value.size() ? value[index + 1U] : '\0';
      const bool lower_to_upper = std::islower(static_cast<unsigned char>(previous)) != 0 &&
                                  std::isupper(static_cast<unsigned char>(ch)) != 0;
      const bool acronym_to_word = std::isupper(static_cast<unsigned char>(previous)) != 0 &&
                                   std::isupper(static_cast<unsigned char>(ch)) != 0 &&
                                   std::islower(static_cast<unsigned char>(next)) != 0;
      const bool alpha_digit_boundary =
          (std::isalpha(static_cast<unsigned char>(previous)) != 0 && std::isdigit(static_cast<unsigned char>(ch)) != 0) ||
          (std::isdigit(static_cast<unsigned char>(previous)) != 0 && std::isalpha(static_cast<unsigned char>(ch)) != 0);
      if (lower_to_upper || acronym_to_word || alpha_digit_boundary) {
        humanized.push_back(' ');
      }
    }
    const auto output = (ch == '_' || ch == '-') ? ' ' : ch;
    if (output == ' ' && (humanized.empty() || humanized.back() == ' ')) {
      continue;
    }
    humanized.push_back(output);
  }
  return humanized;
}

ResolvedPhotoshopFont heuristic_resolved_photoshop_font(std::string_view font_name) {
  ResolvedPhotoshopFont resolved;
  resolved.family = font_name.empty() ? std::string("Arial") : std::string(font_name);
  struct StyleSuffix {
    std::string_view suffix;
    bool bold;
    bool italic;
  };
  static constexpr std::array<StyleSuffix, 25> kStyleSuffixes = {{
      {"-bolditalicmt", true, true},
      {"-boldobliquemt", true, true},
      {"-bolditalic", true, true},
      {"-boldoblique", true, true},
      {"-boldital", true, true},
      {"-boldit", true, true},
      {"-semibolditalic", true, true},
      {"-demibolditalic", true, true},
      {"-blackitalic", true, true},
      {"-heavyitalic", true, true},
      {"-extrabold", true, false},
      {"-ultrabold", true, false},
      {"-semibold", true, false},
      {"-demibold", true, false},
      {"-boldmt", true, false},
      {"-bold", true, false},
      {"-black", true, false},
      {"-heavy", true, false},
      {"-italicmt", false, true},
      {"-obliquemt", false, true},
      {"-italic", false, true},
      {"-oblique", false, true},
      {"-ital", false, true},
      {"-it", false, true},
      {"-regular", false, false},
  }};

  bool stripped = true;
  while (stripped) {
    stripped = false;
    for (const auto suffix : kStyleSuffixes) {
      if (strip_ascii_ci_suffix(resolved.family, suffix.suffix)) {
        resolved.bold = resolved.bold || suffix.bold;
        resolved.italic = resolved.italic || suffix.italic;
        stripped = true;
        break;
      }
    }
  }
  (void)strip_ascii_ci_suffix(resolved.family, "-roman");
  (void)strip_ascii_ci_suffix(resolved.family, "mt");
  (void)strip_ascii_ci_suffix(resolved.family, "ps");
  while (!resolved.family.empty() && (resolved.family.back() == '-' || resolved.family.back() == '_' ||
                                      std::isspace(static_cast<unsigned char>(resolved.family.back())) != 0)) {
    resolved.family.pop_back();
  }
  if (resolved.family.empty()) {
    resolved.family = font_name.empty() ? std::string("Arial") : std::string(font_name);
  } else {
    resolved.family = humanized_postscript_family_name(std::move(resolved.family));
  }
  return resolved;
}

#ifdef _WIN32
std::wstring wide_from_utf8(std::string_view text);
std::string utf8_from_wide(std::wstring_view text);
std::optional<std::wstring> directwrite_localized_string(IDWriteLocalizedStrings* strings);
std::optional<std::string> directwrite_font_info_string(IDWriteFont* font, DWRITE_INFORMATIONAL_STRING_ID id);
std::optional<ResolvedPhotoshopFont> registry_resolved_photoshop_font(std::string_view font_name);

std::optional<ResolvedPhotoshopFont> directwrite_resolved_photoshop_font(std::string_view font_name) {
  const auto wide_name = wide_from_utf8(font_name);
  if (wide_name.empty()) {
    return std::nullopt;
  }

  Microsoft::WRL::ComPtr<IDWriteFactory> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
    return std::nullopt;
  }
  Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(&collection)) || !collection) {
    return std::nullopt;
  }

  const auto family_count = collection->GetFontFamilyCount();
  for (UINT32 family_index = 0; family_index < family_count; ++family_index) {
    Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
    if (FAILED(collection->GetFontFamily(family_index, &font_family)) || !font_family) {
      continue;
    }
    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> family_names;
    if (FAILED(font_family->GetFamilyNames(&family_names)) || !family_names) {
      continue;
    }
    const auto localized_family = directwrite_localized_string(family_names.Get());
    if (!localized_family.has_value()) {
      continue;
    }
    auto family = utf8_from_wide(*localized_family);
    if (family.empty()) {
      continue;
    }

    const auto font_count = font_family->GetFontCount();
    for (UINT32 font_index = 0; font_index < font_count; ++font_index) {
      Microsoft::WRL::ComPtr<IDWriteFont> font;
      if (FAILED(font_family->GetFont(font_index, &font)) || !font) {
        continue;
      }
      std::vector<std::string> candidates;
      if (const auto postscript = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
          postscript.has_value()) {
        candidates.push_back(*postscript);
      }
      if (const auto full_name = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_FULL_NAME);
          full_name.has_value()) {
        candidates.push_back(*full_name);
      }
      Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> face_names;
      if (SUCCEEDED(font->GetFaceNames(&face_names)) && face_names) {
        if (const auto face = directwrite_localized_string(face_names.Get()); face.has_value()) {
          auto face_utf8 = utf8_from_wide(*face);
          if (!face_utf8.empty()) {
            candidates.push_back(family + ' ' + face_utf8);
            candidates.push_back(family + '-' + face_utf8);
          }
        }
      }
      if (std::any_of(candidates.begin(), candidates.end(), [font_name](const std::string& candidate) {
            return font_names_match(candidate, font_name);
          })) {
        // Black/Heavy faces (weight >= 800) keep their full face name: the renderer's
        // family+style matcher then finds the real face ("Arial Black" -> family "Arial",
        // style "Black") instead of flattening it to the Bold face (~15% narrower glyphs on
        // the SNES box blurb). The bold flag stays set so an uninstalled face still falls
        // back to Bold exactly as before.
        if (font->GetWeight() >= DWRITE_FONT_WEIGHT_EXTRA_BOLD) {
          if (const auto full_name =
                  directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_FULL_NAME);
              full_name.has_value() && !full_name->empty() && *full_name != family) {
            return ResolvedPhotoshopFont{*full_name, true, font->GetStyle() != DWRITE_FONT_STYLE_NORMAL};
          }
        }
        return ResolvedPhotoshopFont{std::move(family), font->GetWeight() >= DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     font->GetStyle() != DWRITE_FONT_STYLE_NORMAL};
      }
    }
  }
  return std::nullopt;
}

std::string trim_ascii_whitespace(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  if (first > 0U) {
    value.erase(0, first);
  }
  return value;
}

std::string registry_font_family_from_value_name(std::wstring_view value_name) {
  auto family = trim_ascii_whitespace(utf8_from_wide(value_name));
  const auto suffix = family.find(" (");
  if (suffix != std::string::npos && !family.empty() && family.back() == ')') {
    family.resize(suffix);
    family = trim_ascii_whitespace(std::move(family));
  }
  return family;
}

void append_registry_font_families(HKEY root, const wchar_t* subkey, std::vector<std::string>& families) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS || key == nullptr) {
    return;
  }

  DWORD index = 0;
  std::wstring value_name(512, L'\0');
  while (true) {
    DWORD value_name_length = static_cast<DWORD>(value_name.size());
    const auto result =
        RegEnumValueW(key, index, value_name.data(), &value_name_length, nullptr, nullptr, nullptr, nullptr);
    if (result == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (result == ERROR_MORE_DATA) {
      value_name.resize(value_name.size() * 2U);
      continue;
    }
    if (result == ERROR_SUCCESS) {
      auto family = registry_font_family_from_value_name(std::wstring_view(value_name.data(), value_name_length));
      if (!family.empty()) {
        families.push_back(std::move(family));
      }
    }
    ++index;
  }
  RegCloseKey(key);
}

std::optional<ResolvedPhotoshopFont> registry_resolved_photoshop_font(std::string_view font_name) {
  if (font_name.empty()) {
    return std::nullopt;
  }

  const auto heuristic = heuristic_resolved_photoshop_font(font_name);
  const std::array<std::string_view, 2> targets{font_name, heuristic.family};
  std::vector<std::string> families;
  append_registry_font_families(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                                families);
  append_registry_font_families(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                                families);
  for (auto& family : families) {
    const bool matches = std::any_of(targets.begin(), targets.end(), [&family](std::string_view target) {
      return !target.empty() && font_names_match(family, target);
    });
    if (matches) {
      return ResolvedPhotoshopFont{std::move(family), heuristic.bold, heuristic.italic};
    }
  }
  return std::nullopt;
}
#endif

ResolvedPhotoshopFont resolve_photoshop_font_name(std::string_view font_name) {
#ifdef _WIN32
  if (const auto resolved = directwrite_resolved_photoshop_font(font_name); resolved.has_value()) {
    return *resolved;
  }
  if (const auto resolved = registry_resolved_photoshop_font(font_name); resolved.has_value()) {
    return *resolved;
  }
#endif
  return heuristic_resolved_photoshop_font(font_name);
}

// Auto-leading fraction from the normal paragraph sheet inside a ResourceDict (or the full
// engine text); Photoshop's default is 1.2 (auto leading = 1.2 x font size).
double engine_normal_paragraph_auto_leading_fraction(std::string_view resources) {
  const auto set_range = balanced_range_after(resources, "/ParagraphSheetSet", '[', ']');
  if (!set_range.has_value()) {
    return 1.2;
  }
  const auto sheets =
      engine_dictionary_ranges(resources.substr(set_range->first, set_range->second - set_range->first));
  auto index = 0;
  if (const auto value = engine_number_after_key(resources, "/TheNormalParagraphSheet");
      value.has_value() && std::isfinite(*value) && *value >= 0.0 && *value <= 255.0) {
    index = static_cast<int>(std::lround(*value));
  }
  if (sheets.empty() || static_cast<std::size_t>(index) >= sheets.size()) {
    return 1.2;
  }
  const auto fraction = engine_number_after_key(sheets[static_cast<std::size_t>(index)], "/AutoLeading");
  if (fraction.has_value() && std::isfinite(*fraction) && *fraction > 0.01 && *fraction < 10.0) {
    return *fraction;
  }
  return 1.2;
}

// Parse the ResourceDict's normal style/paragraph sheets (see PsdTextEngineDefaults). The
// fallbacks passed in are used when the engine data has no parsable ResourceDict (hand-built
// engine data in old files and tests).
PsdTextEngineDefaults extract_engine_text_defaults(std::span<const std::uint8_t> payload,
                                                   double fallback_size,
                                                   const CmykColorConverter& cmyk) {
  PsdTextEngineDefaults defaults;
  defaults.font_size = fallback_size;
  const std::string_view engine(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto resource_pos = engine.find("/ResourceDict");
  if (resource_pos == std::string_view::npos) {
    return defaults;
  }
  const auto resources = engine.substr(resource_pos);
  const auto sheet_index = [](std::optional<double> value) {
    if (!value.has_value() || !std::isfinite(*value) || *value < 0.0 || *value > 255.0) {
      return 0;
    }
    return static_cast<int>(std::lround(*value));
  };
  if (const auto set_range = balanced_range_after(resources, "/StyleSheetSet", '[', ']');
      set_range.has_value()) {
    const auto sheets =
        engine_dictionary_ranges(resources.substr(set_range->first, set_range->second - set_range->first));
    const auto index = sheet_index(engine_number_after_key(resources, "/TheNormalStyleSheet"));
    if (!sheets.empty() && static_cast<std::size_t>(index) < sheets.size()) {
      const auto sheet = sheets[static_cast<std::size_t>(index)];
      if (const auto size = engine_number_after_key(sheet, "/FontSize");
          size.has_value() && std::isfinite(*size) && *size > 0.0) {
        defaults.font_size = *size;
      }
      defaults.auto_leading = engine_bool_after_key(sheet, "/AutoLeading", true);
      if (const auto leading = engine_number_after_key(sheet, "/Leading");
          leading.has_value() && std::isfinite(*leading) && *leading > 0.0) {
        defaults.leading = *leading;
      }
      if (const auto tracking = engine_number_after_key(sheet, "/Tracking");
          tracking.has_value() && std::isfinite(*tracking)) {
        defaults.tracking = *tracking;
      }
      if (const auto scale = engine_number_after_key(sheet, "/HorizontalScale");
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        defaults.horizontal_scale = *scale;
      }
      if (const auto scale = engine_number_after_key(sheet, "/VerticalScale");
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        defaults.vertical_scale = *scale;
      }
      if (const auto font_index = engine_number_after_key(sheet, "/Font"); font_index.has_value()) {
        defaults.font_index = static_cast<int>(std::lround(*font_index));
      }
      defaults.faux_bold = engine_bool_after_key(sheet, "/FauxBold");
      defaults.faux_italic = engine_bool_after_key(sheet, "/FauxItalic");
      defaults.fill_color = extract_engine_fill_color_from_text(sheet, cmyk);
    }
  }
  defaults.auto_leading_fraction = engine_normal_paragraph_auto_leading_fraction(resources);
  return defaults;
}

std::optional<std::vector<PsdTextStyleRun>> extract_engine_text_runs(std::span<const std::uint8_t> payload,
                                                                     std::string_view text,
                                                                     int fallback_size,
                                                                     RgbColor fallback_color,
                                                                     const CmykColorConverter& cmyk) {
  const std::string_view engine(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto style_pos = engine.find("/StyleRun");
  if (style_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto style_block = engine.substr(style_pos);
  const auto run_lengths_range = balanced_range_after(style_block, "/RunLengthArray", '[', ']');
  const auto run_array_range = balanced_range_after(style_block, "/RunArray", '[', ']');
  if (!run_lengths_range.has_value() || !run_array_range.has_value()) {
    return std::nullopt;
  }

  const auto length_values = parse_engine_number_array(
      style_block.substr(run_lengths_range->first, run_lengths_range->second - run_lengths_range->first));
  auto dictionaries = engine_dictionary_ranges(style_block.substr(run_array_range->first,
                                                                  run_array_range->second - run_array_range->first));
  if (length_values.empty() || dictionaries.empty()) {
    return std::nullopt;
  }

  const auto font_names = extract_engine_font_names(payload);
  const auto defaults = extract_engine_text_defaults(payload, static_cast<double>(fallback_size), cmyk);
  const auto text_utf16_length = static_cast<int>(utf8_to_utf16(text).size());
  std::vector<PsdTextStyleRun> runs;
  runs.reserve(std::min(length_values.size(), dictionaries.size()));
  int start = 0;
  for (std::size_t index = 0; index < length_values.size() && index < dictionaries.size(); ++index) {
    const auto length = std::max(0, static_cast<int>(std::lround(length_values[index])));
    if (length <= 0) {
      continue;
    }
    if (start >= text_utf16_length) {
      break;
    }
    PsdTextStyleRun run;
    run.start = start;
    run.length = std::min(length, text_utf16_length - start);
    run.size = std::clamp(engine_number_after_key(dictionaries[index], "/FontSize").value_or(defaults.font_size),
                          1.0, static_cast<double>(kMaxTextSizePixels));
    run.color = extract_engine_fill_color_from_text(dictionaries[index], cmyk)
                    .value_or(defaults.fill_color.value_or(fallback_color));
    const auto faux_bold = engine_bool_after_key(dictionaries[index], "/FauxBold", defaults.faux_bold);
    const auto faux_italic = engine_bool_after_key(dictionaries[index], "/FauxItalic", defaults.faux_italic);
    run.bold = faux_bold;
    run.italic = faux_italic;
    run.auto_leading = engine_bool_after_key(dictionaries[index], "/AutoLeading", defaults.auto_leading);
    // Photoshop records a stale /Leading value even for auto-leading runs; only a fixed
    // (non-auto) run's leading participates in layout.
    if (!run.auto_leading) {
      if (const auto leading = engine_number_after_key(dictionaries[index], "/Leading").value_or(defaults.leading);
          std::isfinite(leading) && leading > 0.0) {
        run.leading = leading;
      }
    }
    if (const auto tracking = engine_number_after_key(dictionaries[index], "/Tracking").value_or(defaults.tracking);
        std::isfinite(tracking) && std::abs(tracking) < 10000.0) {
      run.tracking = tracking;
    }
    if (const auto scale =
            engine_number_after_key(dictionaries[index], "/HorizontalScale").value_or(defaults.horizontal_scale);
        std::isfinite(scale) && scale > 0.01 && scale < 100.0) {
      run.horizontal_scale = scale;
    }
    if (const auto scale =
            engine_number_after_key(dictionaries[index], "/VerticalScale").value_or(defaults.vertical_scale);
        std::isfinite(scale) && scale > 0.01 && scale < 100.0) {
      run.vertical_scale = scale;
    }
    const auto font_index = engine_number_after_key(dictionaries[index], "/Font");
    const auto font = font_index.has_value() ? static_cast<int>(std::lround(*font_index))
                                             : defaults.font_index.value_or(-1);
    if (font >= 0 && static_cast<std::size_t>(font) < font_names.size()) {
      const auto resolved = resolve_photoshop_font_name(font_names[static_cast<std::size_t>(font)]);
      run.family = resolved.family;
      run.bold = run.bold || resolved.bold;
      run.italic = run.italic || resolved.italic;
    }
    if (run.family.empty()) {
      run.family = "Arial";
    }
    runs.push_back(std::move(run));
    start += length;
  }

  if (runs.empty()) {
    return std::nullopt;
  }
  return runs;
}

std::optional<std::vector<PsdTextParagraphRun>> extract_engine_paragraph_runs(std::span<const std::uint8_t> payload,
                                                                              std::string_view text) {
  const std::string_view engine(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto paragraph_pos = engine.find("/ParagraphRun");
  if (paragraph_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto paragraph_block = engine.substr(paragraph_pos);
  const auto run_lengths_range = balanced_range_after(paragraph_block, "/RunLengthArray", '[', ']');
  const auto run_array_range = balanced_range_after(paragraph_block, "/RunArray", '[', ']');
  if (!run_lengths_range.has_value() || !run_array_range.has_value()) {
    return std::nullopt;
  }

  const auto length_values = parse_engine_number_array(
      paragraph_block.substr(run_lengths_range->first, run_lengths_range->second - run_lengths_range->first));
  auto dictionaries = engine_dictionary_ranges(paragraph_block.substr(run_array_range->first,
                                                                      run_array_range->second - run_array_range->first));
  if (length_values.empty() || dictionaries.empty()) {
    return std::nullopt;
  }

  const auto text_utf16_length = static_cast<int>(utf8_to_utf16(text).size());
  const auto default_auto_leading_fraction = [&engine] {
    const auto resource_pos = engine.find("/ResourceDict");
    return engine_normal_paragraph_auto_leading_fraction(
        resource_pos == std::string_view::npos ? std::string_view{} : engine.substr(resource_pos));
  }();
  std::vector<PsdTextParagraphRun> runs;
  int start = 0;
  for (std::size_t index = 0; index < length_values.size() && index < dictionaries.size(); ++index) {
    const auto length = std::max(0, static_cast<int>(std::lround(length_values[index])));
    if (length <= 0) {
      continue;
    }
    if (start >= text_utf16_length) {
      break;
    }
    PsdTextParagraphRun run;
    run.start = start;
    run.length = std::min(length, text_utf16_length - start);
    run.justification =
        std::clamp(static_cast<int>(std::lround(engine_number_after_key(dictionaries[index], "/Justification").value_or(0.0))),
                   0, 3);
    run.first_line_indent = engine_number_after_key(dictionaries[index], "/FirstLineIndent").value_or(0.0);
    run.start_indent = engine_number_after_key(dictionaries[index], "/StartIndent").value_or(0.0);
    run.end_indent = engine_number_after_key(dictionaries[index], "/EndIndent").value_or(0.0);
    run.space_before = engine_number_after_key(dictionaries[index], "/SpaceBefore").value_or(0.0);
    run.space_after = engine_number_after_key(dictionaries[index], "/SpaceAfter").value_or(0.0);
    run.auto_leading_fraction = default_auto_leading_fraction;
    if (const auto fraction = engine_number_after_key(dictionaries[index], "/AutoLeading");
        fraction.has_value() && std::isfinite(*fraction) && *fraction > 0.01 && *fraction < 10.0) {
      run.auto_leading_fraction = *fraction;
    }
    runs.push_back(run);
    start += length;
  }

  if (runs.empty()) {
    return std::nullopt;
  }
  return runs;
}

std::string serialize_paragraph_metric(double value);

bool text_run_size_is_integral(const PsdTextStyleRun& run) {
  return std::abs(run.size - std::round(run.size)) < 0.0001;
}

// v1: start len size bold italic color family (int size, no leading)
// v2: v1 + fixed leading (double)
// v3: v2 with double size, leading may be the literal "auto" (auto leading: paragraph
//     auto-leading fraction x size), + tracking (Photoshop 1/1000-em units), + the character
//     panel's horizontal/vertical glyph scales (fractions, 1.0 = none).
std::string serialize_patchy_text_runs(std::span<const PsdTextStyleRun> runs) {
  const bool include_leading = std::any_of(runs.begin(), runs.end(), [](const PsdTextStyleRun& run) {
    return run.leading.has_value() && std::isfinite(*run.leading) && *run.leading > 0.0;
  });
  const bool photoshop_layout = std::any_of(runs.begin(), runs.end(), [](const PsdTextStyleRun& run) {
    return run.auto_leading || std::abs(run.tracking) > 0.0001 || !text_run_size_is_integral(run) ||
           std::abs(run.horizontal_scale - 1.0) > 0.0001 || std::abs(run.vertical_scale - 1.0) > 0.0001;
  });
  std::string serialized = photoshop_layout ? "v3" : (include_leading ? "v2" : "v1");
  for (const auto& run : runs) {
    serialized += '\n';
    serialized += std::to_string(run.start);
    serialized += '\t';
    serialized += std::to_string(run.length);
    serialized += '\t';
    if (photoshop_layout) {
      serialized += serialize_paragraph_metric(run.size);
    } else {
      serialized += std::to_string(static_cast<int>(std::lround(run.size)));
    }
    serialized += '\t';
    serialized += run.bold ? '1' : '0';
    serialized += '\t';
    serialized += run.italic ? '1' : '0';
    serialized += '\t';
    serialized += rgb_hex_color(run.color);
    serialized += '\t';
    serialized += percent_encode(run.family);
    if (photoshop_layout) {
      serialized += '\t';
      // A run with neither auto leading nor a usable fixed value renders auto (Photoshop
      // files always carry one or the other).
      if (run.auto_leading || !run.leading.has_value()) {
        serialized += "auto";
      } else {
        serialized += serialize_paragraph_metric(*run.leading);
      }
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.tracking);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.horizontal_scale);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.vertical_scale);
    } else if (include_leading) {
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.leading.value_or(0.0));
    }
  }
  return serialized;
}

std::string patchy_alignment_name_from_justification(int justification) {
  switch (justification) {
    case 1:
      return "right";
    case 2:
      return "center";
    case 3:
      return "justify";
    default:
      return "left";
  }
}

int photoshop_justification_from_patchy_alignment(std::string_view alignment) {
  const auto lower = ascii_lower_copy(std::string(alignment));
  if (lower == "right") {
    return 1;
  }
  if (lower == "center") {
    return 2;
  }
  if (lower == "justify") {
    return 3;
  }
  return 0;
}

bool paragraph_run_has_layout(const PsdTextParagraphRun& run) noexcept {
  constexpr double kEpsilon = 0.000001;
  return std::abs(run.first_line_indent) > kEpsilon || std::abs(run.start_indent) > kEpsilon ||
         std::abs(run.end_indent) > kEpsilon || std::abs(run.space_before) > kEpsilon ||
         std::abs(run.space_after) > kEpsilon;
}

std::string serialize_paragraph_metric(double value) {
  if (!std::isfinite(value) || std::abs(value) < 0.000001) {
    return "0";
  }
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  return stream.str();
}

// v1: start len alignment; v2: + indent/space metrics; v3: + auto-leading fraction.
std::string serialize_patchy_paragraph_runs(std::span<const PsdTextParagraphRun> runs) {
  const bool include_fraction = std::any_of(runs.begin(), runs.end(), [](const PsdTextParagraphRun& run) {
    return std::abs(run.auto_leading_fraction - 1.2) > 0.0001;
  });
  const bool include_layout =
      include_fraction ||
      std::any_of(runs.begin(), runs.end(), [](const PsdTextParagraphRun& run) { return paragraph_run_has_layout(run); });
  std::string serialized = include_fraction ? "v3" : (include_layout ? "v2" : "v1");
  for (const auto& run : runs) {
    serialized += '\n';
    serialized += std::to_string(run.start);
    serialized += '\t';
    serialized += std::to_string(run.length);
    serialized += '\t';
    serialized += patchy_alignment_name_from_justification(run.justification);
    if (include_layout) {
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.first_line_indent);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.start_indent);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.end_indent);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.space_before);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.space_after);
    }
    if (include_fraction) {
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.auto_leading_fraction);
    }
  }
  return serialized;
}

void append_html_escaped(std::string& output, std::uint32_t codepoint) {
  switch (codepoint) {
    case '&':
      output += "&amp;";
      return;
    case '<':
      output += "&lt;";
      return;
    case '>':
      output += "&gt;";
      return;
    case '"':
      output += "&quot;";
      return;
    case '\n':
      output += "<br />";
      return;
    default:
      append_utf8(output, codepoint);
      return;
  }
}

void append_utf8_range_as_html(std::string& output, std::string_view text, int start_units, int length_units) {
  const auto end_units = start_units + length_units;
  int utf16_position = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = 0x3FU;
    std::size_t consumed = 1;
    if (lead < 0x80U) {
      codepoint = lead;
    } else if ((lead & 0xE0U) == 0xC0U && index + 1 < text.size()) {
      codepoint = ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(text[index + 1]) & 0x3FU);
      consumed = 2;
    } else if ((lead & 0xF0U) == 0xE0U && index + 2 < text.size()) {
      codepoint = ((lead & 0x0FU) << 12U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 2]) & 0x3FU);
      consumed = 3;
    } else if ((lead & 0xF8U) == 0xF0U && index + 3 < text.size()) {
      codepoint = ((lead & 0x07U) << 18U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12U) |
                  ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 3]) & 0x3FU);
      consumed = 4;
    }
    const auto units = codepoint > 0xFFFFU ? 2 : 1;
    if (utf16_position >= start_units && utf16_position < end_units) {
      append_html_escaped(output, codepoint);
    }
    utf16_position += units;
    index += consumed;
    if (utf16_position >= end_units) {
      break;
    }
  }
}

std::string css_escaped_family(std::string_view family) {
  std::string escaped;
  escaped.reserve(family.size());
  for (const auto ch : family) {
    if (ch == '\'' || ch == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::string html_from_text_runs(std::string_view text, std::span<const PsdTextStyleRun> runs,
                                std::span<const PsdTextParagraphRun> paragraph_runs = {}) {
  const auto alignment =
      paragraph_runs.empty() ? std::string("left") : patchy_alignment_name_from_justification(paragraph_runs.front().justification);
  std::string html =
      "<!DOCTYPE HTML><html><head><meta name=\"qrichtext\" content=\"1\" /></head>"
      "<body style=\"margin:0px;\"><p style=\"margin:0px; text-align:";
  html += alignment;
  html += ";\">";
  for (const auto& run : runs) {
    html += "<span style=\" font-family:'";
    html += css_escaped_family(run.family);
    html += "'; font-size:";
    html += std::to_string(std::max(1, static_cast<int>(std::lround(run.size))));
    html += "px;";
    if (run.bold) {
      html += " font-weight:700;";
    }
    if (run.italic) {
      html += " font-style:italic;";
    }
    html += " color:";
    html += rgb_hex_color(run.color);
    html += ";\">";
    append_utf8_range_as_html(html, text, run.start, run.length);
    html += "</span>";
  }
  html += "</p></body></html>";
  return html;
}

const std::array<std::uint8_t, 7>* glyph_for(char ch) {
  static const std::map<char, std::array<std::uint8_t, 7>> glyphs = {
      {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
      {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
      {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
      {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
      {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
      {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
      {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
      {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
      {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
      {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
      {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
      {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
      {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
      {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
      {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
      {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
      {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
      {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
      {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
      {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
      {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
      {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
      {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
      {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
      {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
      {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
      {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
      {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
      {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
      {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
      {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
      {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
      {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
      {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
      {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
      {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
      {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
      {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
      {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
      {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
      {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
      {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
      {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
  };
  const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  const auto found = glyphs.find(upper);
  return found == glyphs.end() ? nullptr : &found->second;
}

PixelBuffer render_placeholder_text(std::string_view text, std::int32_t width, std::int32_t height) {
  width = std::max<std::int32_t>(width, static_cast<std::int32_t>(std::min<std::size_t>(text.size(), 64) * 14U + 8U));
  height = std::max<std::int32_t>(height, 28);
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  pixels.clear(0);

  constexpr int scale = 2;
  constexpr int glyph_width = 5;
  constexpr int glyph_height = 7;
  const int advance = (glyph_width + 1) * scale;
  int cursor_x = 2;
  int cursor_y = 4;
  for (const auto ch : text) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      cursor_x = 2;
      cursor_y += (glyph_height + 2) * scale;
      continue;
    }
    if (ch == ' ') {
      cursor_x += advance;
      continue;
    }
    if (cursor_x + glyph_width * scale >= width) {
      cursor_x = 2;
      cursor_y += (glyph_height + 2) * scale;
    }
    if (cursor_y + glyph_height * scale >= height) {
      break;
    }

    const auto* glyph = glyph_for(ch);
    if (glyph != nullptr) {
      for (int gy = 0; gy < glyph_height; ++gy) {
        for (int gx = 0; gx < glyph_width; ++gx) {
          if (((*glyph)[static_cast<std::size_t>(gy)] & (1U << (glyph_width - 1 - gx))) == 0U) {
            continue;
          }
          for (int sy = 0; sy < scale; ++sy) {
            for (int sx = 0; sx < scale; ++sx) {
              const auto x = cursor_x + gx * scale + sx;
              const auto y = cursor_y + gy * scale + sy;
              if (x >= 0 && y >= 0 && x < width && y < height) {
                auto* px = pixels.pixel(x, y);
                px[0] = 0;
                px[1] = 0;
                px[2] = 0;
                px[3] = 255;
              }
            }
          }
        }
      }
    }
    cursor_x += advance;
  }
  return pixels;
}

bool has_visible_alpha(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (pixels.format().channels < 4) {
    for (const auto byte : pixels.data()) {
      if (byte != 0U) {
        return true;
      }
    }
    return false;
  }
  for (std::size_t offset = 3; offset < pixels.data().size(); offset += pixels.format().channels) {
    if (pixels.data()[offset] != 0U) {
      return true;
    }
  }
  return false;
}

std::optional<Rect> visible_pixel_local_bounds(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return std::nullopt;
  }
  if (pixels.format().channels < 4) {
    return Rect{0, 0, pixels.width(), pixels.height()};
  }

  std::int32_t min_x = pixels.width();
  std::int32_t min_y = pixels.height();
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] == 0U) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

std::optional<PsdTextBoundsD> visible_text_local_bounds_from_layer_pixels(const Layer& layer, const Rect& visible,
                                                                          const std::array<double, 6>& transform) {
  const auto determinant = transform[0] * transform[3] - transform[1] * transform[2];
  if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001) {
    return std::nullopt;
  }
  const auto map_doc_to_local = [&transform, determinant](double x, double y) {
    const auto dx = x - transform[4];
    const auto dy = y - transform[5];
    return std::array<double, 2>{(transform[3] * dx - transform[2] * dy) / determinant,
                                 (-transform[1] * dx + transform[0] * dy) / determinant};
  };

  const auto left = static_cast<double>(layer.bounds().x + visible.x);
  const auto top = static_cast<double>(layer.bounds().y + visible.y);
  const auto right = static_cast<double>(layer.bounds().x + visible.x + visible.width);
  const auto bottom = static_cast<double>(layer.bounds().y + visible.y + visible.height);
  const std::array<std::array<double, 2>, 4> points = {
      map_doc_to_local(left, top),
      map_doc_to_local(right, top),
      map_doc_to_local(right, bottom),
      map_doc_to_local(left, bottom),
  };

  auto min_x = points.front()[0];
  auto max_x = points.front()[0];
  auto min_y = points.front()[1];
  auto max_y = points.front()[1];
  for (const auto& point : points) {
    if (!std::isfinite(point[0]) || !std::isfinite(point[1])) {
      return std::nullopt;
    }
    min_x = std::min(min_x, point[0]);
    max_x = std::max(max_x, point[0]);
    min_y = std::min(min_y, point[1]);
    max_y = std::max(max_y, point[1]);
  }
  if (max_x <= min_x || max_y <= min_y) {
    return std::nullopt;
  }
  return PsdTextBoundsD{min_x, min_y, max_x, max_y};
}

int estimate_text_size_from_alpha(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 4) {
    return 48;
  }

  std::vector<int> visible_runs;
  bool in_run = false;
  int run_start = 0;
  int blank_rows = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    bool row_has_ink = false;
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] >= 8) {
        row_has_ink = true;
        break;
      }
    }

    if (row_has_ink) {
      if (!in_run) {
        in_run = true;
        run_start = y;
      }
      blank_rows = 0;
      continue;
    }

    if (in_run) {
      ++blank_rows;
      if (blank_rows >= 3) {
        visible_runs.push_back(std::max(1, y - blank_rows + 1 - run_start));
        in_run = false;
        blank_rows = 0;
      }
    }
  }
  if (in_run) {
    visible_runs.push_back(std::max(1, pixels.height() - blank_rows - run_start));
  }

  if (visible_runs.empty()) {
    return 48;
  }

  std::sort(visible_runs.begin(), visible_runs.end());
  const auto median_ink_height = visible_runs[visible_runs.size() / 2U];
  return std::clamp(static_cast<int>(std::lround(static_cast<double>(median_ink_height) * 1.35)), 8, 220);
}

bool is_background_layer_name(const std::string& name) {
  return ascii_lower_copy(name) == "background";
}

bool is_full_canvas_background(const Layer& layer, std::int32_t canvas_width, std::int32_t canvas_height) {
  if (!is_background_layer_name(layer.name())) {
    return false;
  }
  const auto bounds = layer.bounds();
  return bounds.x == 0 && bounds.y == 0 && bounds.width >= canvas_width && bounds.height >= canvas_height;
}

bool records_look_like_legacy_top_to_bottom(const std::vector<Layer>& layers, std::int32_t canvas_width,
                                            std::int32_t canvas_height) {
  return !layers.empty() && is_full_canvas_background(layers.back(), canvas_width, canvas_height);
}

std::array<char, 4> blend_mode_key(BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
      return {'p', 'a', 's', 's'};
    case BlendMode::Normal:
      return {'n', 'o', 'r', 'm'};
    case BlendMode::Multiply:
      return {'m', 'u', 'l', ' '};
    case BlendMode::Screen:
      return {'s', 'c', 'r', 'n'};
    case BlendMode::Overlay:
      return {'o', 'v', 'e', 'r'};
    case BlendMode::Darken:
      return {'d', 'a', 'r', 'k'};
    case BlendMode::Lighten:
      return {'l', 'i', 't', 'e'};
    case BlendMode::ColorDodge:
      return {'d', 'i', 'v', ' '};
    case BlendMode::ColorBurn:
      return {'i', 'd', 'i', 'v'};
    case BlendMode::HardLight:
      return {'h', 'L', 'i', 't'};
    case BlendMode::SoftLight:
      return {'s', 'L', 'i', 't'};
    case BlendMode::Difference:
      return {'d', 'i', 'f', 'f'};
    case BlendMode::LinearBurn:
      return {'l', 'b', 'r', 'n'};
    case BlendMode::PinLight:
      return {'p', 'L', 'i', 't'};
    case BlendMode::Saturation:
      return {'s', 'a', 't', ' '};
    case BlendMode::Luminosity:
      return {'l', 'u', 'm', ' '};
    case BlendMode::Exclusion:
      return {'s', 'm', 'u', 'd'};
    case BlendMode::Hue:
      return {'h', 'u', 'e', ' '};
    case BlendMode::Color:
      return {'c', 'o', 'l', 'r'};
    case BlendMode::LinearDodge:
      return {'l', 'd', 'd', 'g'};
    case BlendMode::Subtract:
      return {'f', 's', 'u', 'b'};
    case BlendMode::Divide:
      return {'f', 'd', 'i', 'v'};
  }
  return {'n', 'o', 'r', 'm'};
}

std::optional<std::array<char, 4>> block_key_from_string(std::string_view key);

BlendMode blend_mode_from_key(const std::array<char, 4>& key) {
  if (key == std::array<char, 4>{'N', 'r', 'm', 'l'}) {
    return BlendMode::Normal;
  }
  if (key == std::array<char, 4>{'m', 'u', 'l', ' '}) {
    return BlendMode::Multiply;
  }
  if (key == std::array<char, 4>{'M', 'l', 't', 'p'}) {
    return BlendMode::Multiply;
  }
  if (key == std::array<char, 4>{'s', 'c', 'r', 'n'}) {
    return BlendMode::Screen;
  }
  if (key == std::array<char, 4>{'S', 'c', 'r', 'n'}) {
    return BlendMode::Screen;
  }
  if (key == std::array<char, 4>{'o', 'v', 'e', 'r'}) {
    return BlendMode::Overlay;
  }
  if (key == std::array<char, 4>{'O', 'v', 'r', 'l'}) {
    return BlendMode::Overlay;
  }
  if (key == std::array<char, 4>{'d', 'a', 'r', 'k'}) {
    return BlendMode::Darken;
  }
  if (key == std::array<char, 4>{'l', 'i', 't', 'e'}) {
    return BlendMode::Lighten;
  }
  if (key == std::array<char, 4>{'d', 'i', 'v', ' '}) {
    return BlendMode::ColorDodge;
  }
  if (key == std::array<char, 4>{'C', 'D', 'd', 'g'}) {
    return BlendMode::ColorDodge;
  }
  if (key == std::array<char, 4>{'i', 'd', 'i', 'v'}) {
    return BlendMode::ColorBurn;
  }
  if (key == std::array<char, 4>{'C', 'B', 'r', 'n'}) {
    return BlendMode::ColorBurn;
  }
  if (key == std::array<char, 4>{'h', 'L', 'i', 't'}) {
    return BlendMode::HardLight;
  }
  if (key == std::array<char, 4>{'s', 'L', 'i', 't'} || key == std::array<char, 4>{'S', 'f', 't', 'L'}) {
    return BlendMode::SoftLight;
  }
  if (key == std::array<char, 4>{'d', 'i', 'f', 'f'}) {
    return BlendMode::Difference;
  }
  if (key == std::array<char, 4>{'l', 'b', 'r', 'n'}) {
    return BlendMode::LinearBurn;
  }
  if (key == std::array<char, 4>{'p', 'L', 'i', 't'}) {
    return BlendMode::PinLight;
  }
  if (key == std::array<char, 4>{'s', 'a', 't', ' '}) {
    return BlendMode::Saturation;
  }
  if (key == std::array<char, 4>{'l', 'u', 'm', ' '}) {
    return BlendMode::Luminosity;
  }
  if (key == std::array<char, 4>{'p', 'a', 's', 's'}) {
    return BlendMode::PassThrough;
  }
  if (key == std::array<char, 4>{'s', 'm', 'u', 'd'}) {
    return BlendMode::Exclusion;
  }
  if (key == std::array<char, 4>{'h', 'u', 'e', ' '}) {
    return BlendMode::Hue;
  }
  if (key == std::array<char, 4>{'c', 'o', 'l', 'r'}) {
    return BlendMode::Color;
  }
  if (key == std::array<char, 4>{'l', 'd', 'd', 'g'}) {
    return BlendMode::LinearDodge;
  }
  if (key == std::array<char, 4>{'f', 's', 'u', 'b'}) {
    return BlendMode::Subtract;
  }
  if (key == std::array<char, 4>{'f', 'd', 'i', 'v'}) {
    return BlendMode::Divide;
  }
  return BlendMode::Normal;
}

// Blend mode from an lfx2 descriptor 'BlnM' enum value. Modern Photoshop
// serializes these as full stringIDs ("multiply", "screen", ...); older files
// (including pre-2026 Patchy output) carry 4-char codes, which fall through to
// the legacy key mapping. Unknown values resolve through fallback_key.
BlendMode blend_mode_from_descriptor_enum(std::string_view value, const std::array<char, 4>& fallback_key) {
  if (value == "passThrough") {
    return BlendMode::PassThrough;
  }
  if (value == "normal") {
    return BlendMode::Normal;
  }
  if (value == "multiply") {
    return BlendMode::Multiply;
  }
  if (value == "screen") {
    return BlendMode::Screen;
  }
  if (value == "overlay") {
    return BlendMode::Overlay;
  }
  if (value == "darken") {
    return BlendMode::Darken;
  }
  if (value == "lighten") {
    return BlendMode::Lighten;
  }
  if (value == "colorDodge") {
    return BlendMode::ColorDodge;
  }
  if (value == "colorBurn") {
    return BlendMode::ColorBurn;
  }
  if (value == "hardLight") {
    return BlendMode::HardLight;
  }
  if (value == "softLight") {
    return BlendMode::SoftLight;
  }
  if (value == "difference") {
    return BlendMode::Difference;
  }
  if (value == "linearBurn") {
    return BlendMode::LinearBurn;
  }
  if (value == "pinLight") {
    return BlendMode::PinLight;
  }
  if (value == "saturation") {
    return BlendMode::Saturation;
  }
  if (value == "luminosity") {
    return BlendMode::Luminosity;
  }
  if (value == "exclusion") {
    return BlendMode::Exclusion;
  }
  if (value == "hue") {
    return BlendMode::Hue;
  }
  if (value == "color") {
    return BlendMode::Color;
  }
  if (value == "linearDodge") {
    return BlendMode::LinearDodge;
  }
  if (value == "blendSubtraction") {
    return BlendMode::Subtract;
  }
  if (value == "blendDivide") {
    return BlendMode::Divide;
  }
  return blend_mode_from_key(block_key_from_string(value).value_or(fallback_key));
}

std::optional<Rect> descriptor_bounds_rect(const DescriptorObject& object, std::string_view key) {
  const auto* bounds = descriptor_object(object, key);
  if (bounds == nullptr) {
    return std::nullopt;
  }
  const auto left = descriptor_number(*bounds, "Left", 0.0);
  const auto top = descriptor_number(*bounds, "Top ", 0.0);
  const auto right = descriptor_number(*bounds, "Rght", left);
  const auto bottom = descriptor_number(*bounds, "Btom", top);
  const auto width = static_cast<std::int32_t>(std::max(0.0, std::round(right - left)));
  const auto height = static_cast<std::int32_t>(std::max(0.0, std::round(bottom - top)));
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return Rect{static_cast<std::int32_t>(std::round(left)), static_cast<std::int32_t>(std::round(top)), width, height};
}

std::optional<PsdTextBoundsD> descriptor_bounds(const DescriptorObject& object, std::string_view key) {
  const auto* bounds = descriptor_object(object, key);
  if (bounds == nullptr) {
    return std::nullopt;
  }
  const auto left = descriptor_number(*bounds, "Left", 0.0);
  const auto top = descriptor_number(*bounds, "Top ", 0.0);
  const auto right = descriptor_number(*bounds, "Rght", left);
  const auto bottom = descriptor_number(*bounds, "Btom", top);
  if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)) {
    return std::nullopt;
  }
  return PsdTextBoundsD{left, top, right, bottom};
}

std::optional<PsdTextBoundsD> extract_engine_box_bounds(std::span<const std::uint8_t> payload) {
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto range = balanced_range_after(text, "/BoxBounds", '[', ']');
  if (!range.has_value()) {
    return std::nullopt;
  }
  const auto values = parse_engine_number_array(text.substr(range->first, range->second - range->first));
  if (values.size() < 4U) {
    return std::nullopt;
  }
  return PsdTextBoundsD{values[0], values[1], values[2], values[3]};
}

bool engine_data_describes_box_text(std::span<const std::uint8_t> payload) {
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  return text.find("/BoxBounds") != std::string_view::npos && text.find("/ShapeType 1") != std::string_view::npos;
}

std::optional<PsdTextGeometry> extract_type_tool_geometry(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (reader.remaining() < 2U + 6U * 8U + 2U + 4U) {
      return std::nullopt;
    }
    PsdTextGeometry geometry;
    (void)reader.read_u16();
    for (double& value : geometry.transform) {
      value = read_f64(reader);
    }
    (void)reader.read_u16();
    (void)reader.read_u32();
    const auto descriptor = read_descriptor(reader);
    geometry.bounds = descriptor_bounds(descriptor, "bounds").value_or(geometry.bounds);
    geometry.bounding_box = descriptor_bounds(descriptor, "boundingBox").value_or(geometry.bounds);
    if (const auto* text_index = descriptor_value(descriptor, "TextIndex");
        text_index != nullptr && text_index->type == DescriptorValue::Type::Integer) {
      geometry.text_index = text_index->integer_value;
    }
    // The warp descriptor follows the text descriptor (Warp Text: style + bend +
    // distortions, acting over the 'bounds' box). A malformed warp degrades to "no
    // warp" without losing the text geometry.
    try {
      if (reader.remaining() >= 6U) {
        (void)reader.read_u16();  // warp version (1)
        (void)reader.read_u32();  // descriptor version (16)
        const auto warp_descriptor = read_descriptor(reader);
        TextWarp warp;
        if (const auto* style = descriptor_value(warp_descriptor, "warpStyle");
            style != nullptr && style->type == DescriptorValue::Type::Enum) {
          warp.style = style->enum_value;
        }
        warp.value = descriptor_number(warp_descriptor, "warpValue", 0.0);
        warp.perspective = descriptor_number(warp_descriptor, "warpPerspective", 0.0);
        warp.perspective_other = descriptor_number(warp_descriptor, "warpPerspectiveOther", 0.0);
        if (const auto* rotate = descriptor_value(warp_descriptor, "warpRotate");
            rotate != nullptr && rotate->type == DescriptorValue::Type::Enum) {
          warp.rotate = rotate->enum_value;
        }
        warp.bounds_left = geometry.bounds.left;
        warp.bounds_top = geometry.bounds.top;
        warp.bounds_right = geometry.bounds.right;
        warp.bounds_bottom = geometry.bounds.bottom;
        if (!text_warp_is_identity(warp)) {
          geometry.warp = std::move(warp);
        }
      }
    } catch (const std::exception&) {
    }
    geometry.box_bounds = extract_engine_box_bounds(payload).value_or(PsdTextBoundsD{
        0.0, 0.0, std::max(1.0, geometry.bounds.right - geometry.bounds.left), std::max(1.0, geometry.bounds.bottom)});
    if (payload.size() >= 16U) {
      const auto tail_offset = payload.size() - 16U;
      for (std::size_t index = 0; index < geometry.tail_bounds.size(); ++index) {
        geometry.tail_bounds[index] = static_cast<int>(static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(payload[tail_offset + index * 4U]) << 24U) |
            (static_cast<std::uint32_t>(payload[tail_offset + index * 4U + 1U]) << 16U) |
            (static_cast<std::uint32_t>(payload[tail_offset + index * 4U + 2U]) << 8U) |
            static_cast<std::uint32_t>(payload[tail_offset + index * 4U + 3U])));
      }
    }
    return geometry;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<Rect> extract_type_tool_text_box(std::span<const std::uint8_t> payload) {
  if (!engine_data_describes_box_text(payload)) {
    return std::nullopt;
  }
  if (const auto box_bounds = extract_engine_box_bounds(payload); box_bounds.has_value()) {
    const auto left = static_cast<std::int32_t>(std::round(box_bounds->left));
    const auto top = static_cast<std::int32_t>(std::round(box_bounds->top));
    const auto width = static_cast<std::int32_t>(std::max(0.0, std::round(box_bounds->right - box_bounds->left)));
    const auto height = static_cast<std::int32_t>(std::max(0.0, std::round(box_bounds->bottom - box_bounds->top)));
    if (width > 0 && height > 0) {
      return Rect{left, top, width, height};
    }
  }
  try {
    BigEndianReader reader(payload);
    if (reader.remaining() < 2U + 6U * 8U + 2U + 4U) {
      return std::nullopt;
    }
    (void)reader.read_u16();
    for (int i = 0; i < 6; ++i) {
      (void)read_f64(reader);
    }
    (void)reader.read_u16();
    (void)reader.read_u32();
    const auto descriptor = read_descriptor(reader);
    if (auto bounds = descriptor_bounds_rect(descriptor, "bounds"); bounds.has_value()) {
      return bounds;
    }
    return descriptor_bounds_rect(descriptor, "boundingBox");
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string descriptor_enum(const DescriptorObject& object, std::string_view key, std::string fallback = {}) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::Enum) {
    return fallback;
  }
  return value->enum_value;
}

float percent_to_unit(double value) {
  if (!std::isfinite(value)) {
    return 1.0F;
  }
  return std::clamp(static_cast<float>(value / 100.0), 0.0F, 1.0F);
}

RgbColor descriptor_rgb_color(const DescriptorObject& object, std::string_view key,
                              const CmykColorConverter& cmyk, RgbColor fallback = {}) {
  const auto* color_object = descriptor_object(object, key);
  if (color_object == nullptr) {
    return fallback;
  }
  if (color_object->class_id == "CMYC") {
    // CMYK-mode documents store descriptor colors as ink percentages.
    const auto ink = [&](std::string_view component_key) {
      return descriptor_number(*color_object, component_key) / 100.0;
    };
    return cmyk.rgb_from_ink(ink("Cyn "), ink("Mgnt"), ink("Ylw "), ink("Blck"));
  }
  return RgbColor{static_cast<std::uint8_t>(std::clamp(std::lround(descriptor_number(*color_object, "Rd  ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(descriptor_number(*color_object, "Grn ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(descriptor_number(*color_object, "Bl  ")), 0L, 255L))};
}

LayerStyleGradientType gradient_type_from_descriptor(std::string_view value) {
  if (value == "Rdl ") {
    return LayerStyleGradientType::Radial;
  }
  if (value == "Angl") {
    return LayerStyleGradientType::Angle;
  }
  if (value == "Rflc") {
    return LayerStyleGradientType::Reflected;
  }
  if (value == "Dmnd") {
    return LayerStyleGradientType::Diamond;
  }
  return LayerStyleGradientType::Linear;
}

LayerStyleGradient parse_gradient(const DescriptorObject& effect, const CmykColorConverter& cmyk) {
  LayerStyleGradient gradient;
  if (const auto* gradient_object = descriptor_object(effect, "Grad"); gradient_object != nullptr) {
    if (const auto* colors = descriptor_value(*gradient_object, "Clrs");
        colors != nullptr && colors->type == DescriptorValue::Type::List) {
      for (const auto& item : colors->list_value) {
        if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
          continue;
        }
        const auto& stop = *item.object_value;
        gradient.color_stops.push_back(
            GradientColorStop{std::clamp(static_cast<float>(descriptor_number(stop, "Lctn") / 4096.0), 0.0F, 1.0F),
                              descriptor_rgb_color(stop, "Clr ", cmyk)});
      }
    }
    if (const auto* transparency = descriptor_value(*gradient_object, "Trns");
        transparency != nullptr && transparency->type == DescriptorValue::Type::List) {
      for (const auto& item : transparency->list_value) {
        if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
          continue;
        }
        const auto& stop = *item.object_value;
        gradient.alpha_stops.push_back(
            GradientAlphaStop{std::clamp(static_cast<float>(descriptor_number(stop, "Lctn") / 4096.0), 0.0F, 1.0F),
                              percent_to_unit(descriptor_number(stop, "Opct", 100.0))});
      }
    }
  }
  gradient.angle_degrees = static_cast<float>(descriptor_number(effect, "Angl", 90.0));
  gradient.scale = std::max(0.01F, static_cast<float>(descriptor_number(effect, "Scl ", 100.0) / 100.0));
  gradient.reverse = descriptor_bool(effect, "Rvrs", false);
  gradient.type = gradient_type_from_descriptor(descriptor_enum(effect, "Type", "Lnr "));
  std::sort(gradient.color_stops.begin(), gradient.color_stops.end(),
            [](const GradientColorStop& lhs, const GradientColorStop& rhs) { return lhs.location < rhs.location; });
  std::sort(gradient.alpha_stops.begin(), gradient.alpha_stops.end(),
            [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });
  return gradient;
}

std::optional<LayerDropShadow> parse_drop_shadow(const DescriptorObject& effect,
                                                 const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "mul "),
                                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  shadow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  shadow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  shadow.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  shadow.use_global_light = descriptor_bool(effect, "uglg", false);
  shadow.distance = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Dstn", 5.0)));
  shadow.spread = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  shadow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return shadow;
}

std::optional<LayerInnerShadow> parse_inner_shadow(const DescriptorObject& effect,
                                                   const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "mul "),
                                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  shadow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  shadow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  shadow.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  shadow.use_global_light = descriptor_bool(effect, "uglg", false);
  shadow.distance = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Dstn", 5.0)));
  shadow.choke = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  shadow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return shadow;
}

std::optional<LayerOuterGlow> parse_outer_glow(const DescriptorObject& effect,
                                               const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "scrn"),
                                                      std::array<char, 4>{'s', 'c', 'r', 'n'});
  glow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{255, 255, 190});
  glow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  glow.spread = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  glow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return glow;
}

LayerInnerGlowSource inner_glow_source_from_descriptor(std::string_view value) {
  return value == "SrcC" ? LayerInnerGlowSource::Center : LayerInnerGlowSource::Edge;
}

std::optional<LayerInnerGlow> parse_inner_glow(const DescriptorObject& effect,
                                               const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "scrn"),
                                                      std::array<char, 4>{'s', 'c', 'r', 'n'});
  glow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{255, 255, 190});
  glow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  glow.choke = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  glow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  glow.source = inner_glow_source_from_descriptor(descriptor_enum(effect, "glwS", "SrcE"));
  return glow;
}

std::optional<LayerColorOverlay> parse_color_overlay(const DescriptorObject& effect,
                                                     const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  overlay.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{255, 0, 0});
  overlay.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  return overlay;
}

std::optional<LayerBevelEmboss> parse_bevel_emboss(const DescriptorObject& effect,
                                                   const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode =
      blend_mode_from_descriptor_enum(descriptor_enum(effect, "hglM", "scrn"),
                                      std::array<char, 4>{'s', 'c', 'r', 'n'});
  bevel.highlight_color = descriptor_rgb_color(effect, "hglC", cmyk, RgbColor{255, 255, 255});
  bevel.highlight_opacity = percent_to_unit(descriptor_number(effect, "hglO", 75.0));
  bevel.shadow_blend_mode =
      blend_mode_from_descriptor_enum(descriptor_enum(effect, "sdwM", "mul "),
                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  bevel.shadow_color = descriptor_rgb_color(effect, "sdwC", cmyk, RgbColor{0, 0, 0});
  bevel.shadow_opacity = percent_to_unit(descriptor_number(effect, "sdwO", 75.0));
  bevel.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  bevel.use_global_light = descriptor_bool(effect, "uglg", false);
  bevel.altitude_degrees = static_cast<float>(descriptor_number(effect, "Lald", 30.0));
  bevel.depth = std::max(0.01F, static_cast<float>(descriptor_number(effect, "srgR", 100.0) / 100.0));
  bevel.size = std::max(1.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  bevel.direction_up = descriptor_enum(effect, "bvlD", "In  ") != "Out ";
  return bevel;
}

std::optional<LayerGradientFill> parse_gradient_fill(const DescriptorObject& effect,
                                                     const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  fill.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  fill.gradient = parse_gradient(effect, cmyk);
  return fill;
}

std::optional<LayerSatin> parse_satin(const DescriptorObject& effect,
                                      const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerSatin satin;
  satin.enabled = true;
  satin.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "mul "),
                                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  satin.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  satin.opacity = percent_to_unit(descriptor_number(effect, "Opct", 50.0));
  satin.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 19.0));
  satin.distance = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Dstn", 11.0)));
  satin.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 14.0)));
  satin.invert = descriptor_bool(effect, "Invr", true);
  return satin;
}

std::string descriptor_string(const DescriptorObject& object, std::string_view key, std::string fallback = {}) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::String) {
    return fallback;
  }
  return value->string_value;
}

std::optional<LayerPatternOverlay> parse_pattern_overlay(const DescriptorObject& effect) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerPatternOverlay pattern;
  pattern.enabled = true;
  pattern.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  pattern.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  pattern.scale = std::max(0.01F, static_cast<float>(descriptor_number(effect, "Scl ", 100.0) / 100.0));
  if (const auto* pattern_object = descriptor_object(effect, "Ptrn"); pattern_object != nullptr) {
    pattern.pattern_name = descriptor_string(*pattern_object, "Nm  ");
    pattern.pattern_id = descriptor_string(*pattern_object, "Idnt");
  }
  return pattern;
}

LayerStrokePosition stroke_position_from_descriptor(std::string_view value) {
  if (value == "InsF") {
    return LayerStrokePosition::Inside;
  }
  if (value == "CtrF") {
    return LayerStrokePosition::Center;
  }
  return LayerStrokePosition::Outside;
}

std::optional<LayerStroke> parse_stroke(const DescriptorObject& effect,
                                        const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  stroke.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  stroke.size = std::max(1.0F, static_cast<float>(descriptor_number(effect, "Sz  ", 3.0)));
  stroke.position = stroke_position_from_descriptor(descriptor_enum(effect, "Styl", "OutF"));
  stroke.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  stroke.uses_gradient = descriptor_enum(effect, "PntT", "SClr") == "GrFl";
  if (stroke.uses_gradient) {
    stroke.gradient = parse_gradient(effect, cmyk);
  }
  return stroke;
}

LayerStyle parse_lfx2_layer_style(std::span<const std::uint8_t> payload,
                                  const CmykColorConverter& cmyk) {
  LayerStyle style;
  try {
    BigEndianReader reader(payload);
    (void)reader.read_u32();  // object effects version
    const auto descriptor_version = reader.read_u32();
    if (descriptor_version != 16) {
      return style;
    }
    const auto root = read_descriptor(reader);
    style.effects_visible = descriptor_bool(root, "masterFXSwitch", true);
    if (const auto* effect = descriptor_object(root, "DrSh"); effect != nullptr) {
      if (const auto shadow = parse_drop_shadow(*effect, cmyk); shadow.has_value()) {
        style.drop_shadows.push_back(*shadow);
      }
    }
    if (const auto* effect = descriptor_object(root, "IrSh"); effect != nullptr) {
      if (const auto shadow = parse_inner_shadow(*effect, cmyk); shadow.has_value()) {
        style.inner_shadows.push_back(*shadow);
      }
    }
    if (const auto* effect = descriptor_object(root, "innerShadow"); effect != nullptr) {
      if (const auto shadow = parse_inner_shadow(*effect, cmyk); shadow.has_value()) {
        style.inner_shadows.push_back(*shadow);
      }
    }
    if (const auto* effect = descriptor_object(root, "OrGl"); effect != nullptr) {
      if (const auto glow = parse_outer_glow(*effect, cmyk); glow.has_value()) {
        style.outer_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "outerGlow"); effect != nullptr) {
      if (const auto glow = parse_outer_glow(*effect, cmyk); glow.has_value()) {
        style.outer_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "IrGl"); effect != nullptr) {
      if (const auto glow = parse_inner_glow(*effect, cmyk); glow.has_value()) {
        style.inner_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "innerGlow"); effect != nullptr) {
      if (const auto glow = parse_inner_glow(*effect, cmyk); glow.has_value()) {
        style.inner_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "ChFX"); effect != nullptr) {
      if (const auto satin = parse_satin(*effect, cmyk); satin.has_value()) {
        style.satins.push_back(*satin);
      }
    }
    if (const auto* effect = descriptor_object(root, "chromeFX"); effect != nullptr) {
      if (const auto satin = parse_satin(*effect, cmyk); satin.has_value()) {
        style.satins.push_back(*satin);
      }
    }
    if (const auto* effect = descriptor_object(root, "ebbl"); effect != nullptr) {
      if (const auto bevel = parse_bevel_emboss(*effect, cmyk); bevel.has_value()) {
        style.bevels.push_back(*bevel);
      }
    }
    if (const auto* effect = descriptor_object(root, "bevelEmboss"); effect != nullptr) {
      if (const auto bevel = parse_bevel_emboss(*effect, cmyk); bevel.has_value()) {
        style.bevels.push_back(*bevel);
      }
    }
    if (const auto* effect = descriptor_object(root, "GrFl"); effect != nullptr) {
      if (const auto fill = parse_gradient_fill(*effect, cmyk); fill.has_value()) {
        style.gradient_fills.push_back(*fill);
      }
    }
    if (const auto* effect = descriptor_object(root, "patternFill"); effect != nullptr) {
      if (const auto pattern = parse_pattern_overlay(*effect); pattern.has_value()) {
        style.pattern_overlays.push_back(*pattern);
      }
    }
    if (const auto* effect = descriptor_object(root, "SoFi"); effect != nullptr) {
      if (const auto overlay = parse_color_overlay(*effect, cmyk); overlay.has_value()) {
        style.color_overlays.push_back(*overlay);
      }
    }
    if (const auto* effect = descriptor_object(root, "solidFill"); effect != nullptr) {
      if (const auto overlay = parse_color_overlay(*effect, cmyk); overlay.has_value()) {
        style.color_overlays.push_back(*overlay);
      }
    }
    if (const auto* effect = descriptor_object(root, "FrFX"); effect != nullptr) {
      if (const auto stroke = parse_stroke(*effect, cmyk); stroke.has_value()) {
        style.strokes.push_back(*stroke);
      }
    }
    if (const auto* value = descriptor_value(root, "dropShadowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto shadow = parse_drop_shadow(*item.object_value, cmyk); shadow.has_value()) {
            style.drop_shadows.push_back(*shadow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "innerShadowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto shadow = parse_inner_shadow(*item.object_value, cmyk); shadow.has_value()) {
            style.inner_shadows.push_back(*shadow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "outerGlowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto glow = parse_outer_glow(*item.object_value, cmyk); glow.has_value()) {
            style.outer_glows.push_back(*glow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "innerGlowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto glow = parse_inner_glow(*item.object_value, cmyk); glow.has_value()) {
            style.inner_glows.push_back(*glow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "chromeFXMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto satin = parse_satin(*item.object_value, cmyk); satin.has_value()) {
            style.satins.push_back(*satin);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "bevelEmbossMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto bevel = parse_bevel_emboss(*item.object_value, cmyk); bevel.has_value()) {
            style.bevels.push_back(*bevel);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "gradientFillMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto fill = parse_gradient_fill(*item.object_value, cmyk); fill.has_value()) {
            style.gradient_fills.push_back(*fill);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "patternFillMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto pattern = parse_pattern_overlay(*item.object_value); pattern.has_value()) {
            style.pattern_overlays.push_back(*pattern);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "solidFillMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto overlay = parse_color_overlay(*item.object_value, cmyk); overlay.has_value()) {
            style.color_overlays.push_back(*overlay);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "frameFXMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto stroke = parse_stroke(*item.object_value, cmyk); stroke.has_value()) {
            style.strokes.push_back(*stroke);
          }
        }
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return style;
}

RgbColor read_legacy_effect_color(BigEndianReader& reader) {
  (void)reader.read_u16();
  const auto red = reader.read_u16();
  const auto green = reader.read_u16();
  const auto blue = reader.read_u16();
  (void)reader.read_u16();
  return RgbColor{static_cast<std::uint8_t>(red / 257U), static_cast<std::uint8_t>(green / 257U),
                  static_cast<std::uint8_t>(blue / 257U)};
}

LayerStyle parse_lrfx_layer_style(std::span<const std::uint8_t> payload) {
  LayerStyle style;
  try {
    BigEndianReader reader(payload);
    (void)reader.read_u16();
    const auto effect_count = reader.read_u16();
    for (std::uint16_t index = 0; index < effect_count && reader.remaining() >= 12; ++index) {
      const auto signature = read_signature(reader);
      const auto key = read_signature(reader);
      if (signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
          signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
        return style;
      }
      const auto effect_payload_size = reader.read_u32();
      if (effect_payload_size > reader.remaining()) {
        return style;
      }
      const auto effect_payload = reader.read_bytes(effect_payload_size);
      if (key != std::array<char, 4>{'d', 's', 'd', 'w'}) {
        continue;
      }
      BigEndianReader effect_reader(effect_payload);
      (void)effect_reader.read_u32();
      LayerDropShadow shadow;
      shadow.enabled = true;
      shadow.size = static_cast<float>(effect_reader.read_u32());
      (void)effect_reader.read_u32();
      shadow.angle_degrees = static_cast<float>(effect_reader.read_u32());
      shadow.distance = static_cast<float>(effect_reader.read_u32());
      shadow.color = read_legacy_effect_color(effect_reader);
      (void)read_signature(effect_reader);
      shadow.blend_mode = blend_mode_from_key(read_signature(effect_reader));
      shadow.enabled = effect_reader.read_u8() != 0;
      (void)effect_reader.read_u8();
      shadow.opacity = percent_to_unit(effect_reader.read_u8());
      if (shadow.enabled) {
        style.drop_shadows.push_back(shadow);
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return style;
}

// Photoshop renders shadow and bevel effects flagged "use global light" with the document-wide
// light direction (image resources 1037/1049) instead of the effect's own stored angle. Resolve
// those angles while importing so Patchy renders what Photoshop renders, then clear the flag:
// Patchy edits angles per effect, so resolved styles are saved as local angles that mean the
// same thing in both applications.
void resolve_global_light(LayerStyle& style, float angle_degrees, float altitude_degrees) {
  for (auto& shadow : style.drop_shadows) {
    if (shadow.use_global_light) {
      shadow.angle_degrees = angle_degrees;
      shadow.use_global_light = false;
    }
  }
  for (auto& shadow : style.inner_shadows) {
    if (shadow.use_global_light) {
      shadow.angle_degrees = angle_degrees;
      shadow.use_global_light = false;
    }
  }
  for (auto& bevel : style.bevels) {
    if (bevel.use_global_light) {
      bevel.angle_degrees = angle_degrees;
      bevel.altitude_degrees = altitude_degrees;
      bevel.use_global_light = false;
    }
  }
}

void merge_missing_layer_style_effects(LayerStyle& target, LayerStyle source) {
  if (!source.effects_visible) {
    target.effects_visible = false;
  }
  if (target.drop_shadows.empty()) {
    target.drop_shadows = std::move(source.drop_shadows);
  }
  if (target.inner_shadows.empty()) {
    target.inner_shadows = std::move(source.inner_shadows);
  }
  if (target.outer_glows.empty()) {
    target.outer_glows = std::move(source.outer_glows);
  }
  if (target.inner_glows.empty()) {
    target.inner_glows = std::move(source.inner_glows);
  }
  if (target.color_overlays.empty()) {
    target.color_overlays = std::move(source.color_overlays);
  }
  if (target.gradient_fills.empty()) {
    target.gradient_fills = std::move(source.gradient_fills);
  }
  if (target.pattern_overlays.empty()) {
    target.pattern_overlays = std::move(source.pattern_overlays);
  }
  if (target.strokes.empty()) {
    target.strokes = std::move(source.strokes);
  }
  if (target.bevels.empty()) {
    target.bevels = std::move(source.bevels);
  }
  if (target.satins.empty()) {
    target.satins = std::move(source.satins);
  }
}

void write_bool(BigEndianWriter& writer, bool value) {
  writer.write_u8(value ? 1U : 0U);
}

bool read_bool(BigEndianReader& reader) {
  return reader.read_u8() != 0U;
}

void write_f32(BigEndianWriter& writer, float value) {
  writer.write_u32(std::bit_cast<std::uint32_t>(value));
}

float read_f32(BigEndianReader& reader) {
  return std::bit_cast<float>(reader.read_u32());
}

void write_rgb_color(BigEndianWriter& writer, RgbColor color) {
  writer.write_u8(color.red);
  writer.write_u8(color.green);
  writer.write_u8(color.blue);
}

RgbColor read_rgb_color(BigEndianReader& reader) {
  return RgbColor{reader.read_u8(), reader.read_u8(), reader.read_u8()};
}

std::uint8_t gradient_type_value(LayerStyleGradientType type) {
  switch (type) {
    case LayerStyleGradientType::Radial:
      return 1U;
    case LayerStyleGradientType::Angle:
      return 2U;
    case LayerStyleGradientType::Reflected:
      return 3U;
    case LayerStyleGradientType::Diamond:
      return 4U;
    case LayerStyleGradientType::Linear:
      return 0U;
  }
  return 0U;
}

LayerStyleGradientType gradient_type_from_value(std::uint8_t value) {
  switch (value) {
    case 1U:
      return LayerStyleGradientType::Radial;
    case 2U:
      return LayerStyleGradientType::Angle;
    case 3U:
      return LayerStyleGradientType::Reflected;
    case 4U:
      return LayerStyleGradientType::Diamond;
    default:
      return LayerStyleGradientType::Linear;
  }
}

std::uint8_t stroke_position_value(LayerStrokePosition position) {
  switch (position) {
    case LayerStrokePosition::Inside:
      return 1U;
    case LayerStrokePosition::Center:
      return 2U;
    case LayerStrokePosition::Outside:
      return 0U;
  }
  return 0U;
}

LayerStrokePosition stroke_position_from_value(std::uint8_t value) {
  switch (value) {
    case 1U:
      return LayerStrokePosition::Inside;
    case 2U:
      return LayerStrokePosition::Center;
    default:
      return LayerStrokePosition::Outside;
  }
}

void write_count(BigEndianWriter& writer, std::size_t count, const char* field) {
  writer.write_u16(checked_u16(count, field));
}

std::uint16_t read_count(BigEndianReader& reader, const char* field) {
  const auto count = reader.read_u16();
  if (count > kMaxPatchyLayerStyleEntries) {
    throw std::runtime_error(std::string("Patchy layer style has too many entries: ") + field);
  }
  return count;
}

void write_layer_style_gradient(BigEndianWriter& writer, const LayerStyleGradient& gradient) {
  writer.write_u8(gradient_type_value(gradient.type));
  write_f32(writer, gradient.angle_degrees);
  write_f32(writer, gradient.scale);
  write_bool(writer, gradient.reverse);
  write_count(writer, gradient.color_stops.size(), "layer style gradient color stops");
  for (const auto& stop : gradient.color_stops) {
    write_f32(writer, stop.location);
    write_rgb_color(writer, stop.color);
  }
  write_count(writer, gradient.alpha_stops.size(), "layer style gradient alpha stops");
  for (const auto& stop : gradient.alpha_stops) {
    write_f32(writer, stop.location);
    write_f32(writer, stop.opacity);
  }
}

LayerStyleGradient read_layer_style_gradient(BigEndianReader& reader) {
  LayerStyleGradient gradient;
  gradient.type = gradient_type_from_value(reader.read_u8());
  gradient.angle_degrees = read_f32(reader);
  gradient.scale = read_f32(reader);
  gradient.reverse = read_bool(reader);

  const auto color_stop_count = read_count(reader, "layer style gradient color stops");
  gradient.color_stops.reserve(color_stop_count);
  for (std::uint16_t i = 0; i < color_stop_count; ++i) {
    gradient.color_stops.push_back(GradientColorStop{read_f32(reader), read_rgb_color(reader)});
  }

  const auto alpha_stop_count = read_count(reader, "layer style gradient alpha stops");
  gradient.alpha_stops.reserve(alpha_stop_count);
  for (std::uint16_t i = 0; i < alpha_stop_count; ++i) {
    gradient.alpha_stops.push_back(GradientAlphaStop{read_f32(reader), read_f32(reader)});
  }
  return gradient;
}

std::vector<std::uint8_t> patchy_layer_style_payload(const LayerStyle& style) {
  BigEndianWriter writer;
  write_signature(writer, kPatchyLayerStylePayloadSignature);
  writer.write_u16(kPatchyLayerStyleVersion);
  write_bool(writer, style.effects_visible);

  write_count(writer, style.drop_shadows.size(), "drop shadows");
  for (const auto& shadow : style.drop_shadows) {
    write_bool(writer, shadow.enabled);
    write_signature(writer, blend_mode_key(shadow.blend_mode));
    write_rgb_color(writer, shadow.color);
    write_f32(writer, shadow.opacity);
    write_f32(writer, shadow.angle_degrees);
    write_f32(writer, shadow.distance);
    write_f32(writer, shadow.spread);
    write_f32(writer, shadow.size);
  }

  write_count(writer, style.outer_glows.size(), "outer glows");
  for (const auto& glow : style.outer_glows) {
    write_bool(writer, glow.enabled);
    write_signature(writer, blend_mode_key(glow.blend_mode));
    write_rgb_color(writer, glow.color);
    write_f32(writer, glow.opacity);
    write_f32(writer, glow.spread);
    write_f32(writer, glow.size);
  }

  write_count(writer, style.gradient_fills.size(), "gradient fills");
  for (const auto& fill : style.gradient_fills) {
    write_bool(writer, fill.enabled);
    write_signature(writer, blend_mode_key(fill.blend_mode));
    write_f32(writer, fill.opacity);
    write_layer_style_gradient(writer, fill.gradient);
  }

  write_count(writer, style.strokes.size(), "strokes");
  for (const auto& stroke : style.strokes) {
    write_bool(writer, stroke.enabled);
    write_signature(writer, blend_mode_key(stroke.blend_mode));
    write_rgb_color(writer, stroke.color);
    write_f32(writer, stroke.opacity);
    write_f32(writer, stroke.size);
    writer.write_u8(stroke_position_value(stroke.position));
    write_bool(writer, stroke.uses_gradient);
    write_layer_style_gradient(writer, stroke.gradient);
  }

  write_count(writer, style.bevels.size(), "bevels");
  for (const auto& bevel : style.bevels) {
    write_bool(writer, bevel.enabled);
    write_signature(writer, blend_mode_key(bevel.highlight_blend_mode));
    write_rgb_color(writer, bevel.highlight_color);
    write_f32(writer, bevel.highlight_opacity);
    write_signature(writer, blend_mode_key(bevel.shadow_blend_mode));
    write_rgb_color(writer, bevel.shadow_color);
    write_f32(writer, bevel.shadow_opacity);
    write_f32(writer, bevel.angle_degrees);
    write_f32(writer, bevel.altitude_degrees);
    write_f32(writer, bevel.depth);
    write_f32(writer, bevel.size);
    write_bool(writer, bevel.direction_up);
  }

  write_count(writer, style.color_overlays.size(), "color overlays");
  for (const auto& overlay : style.color_overlays) {
    write_bool(writer, overlay.enabled);
    write_signature(writer, blend_mode_key(overlay.blend_mode));
    write_rgb_color(writer, overlay.color);
    write_f32(writer, overlay.opacity);
  }

  return writer.bytes();
}

std::optional<LayerStyle> parse_patchy_layer_style(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (read_signature(reader) != kPatchyLayerStylePayloadSignature) {
      return std::nullopt;
    }
    if (reader.read_u16() != kPatchyLayerStyleVersion) {
      return std::nullopt;
    }

    LayerStyle style;
    style.effects_visible = read_bool(reader);

    const auto shadow_count = read_count(reader, "drop shadows");
    style.drop_shadows.reserve(shadow_count);
    for (std::uint16_t i = 0; i < shadow_count; ++i) {
      LayerDropShadow shadow;
      shadow.enabled = read_bool(reader);
      shadow.blend_mode = blend_mode_from_key(read_signature(reader));
      shadow.color = read_rgb_color(reader);
      shadow.opacity = read_f32(reader);
      shadow.angle_degrees = read_f32(reader);
      shadow.distance = read_f32(reader);
      shadow.spread = read_f32(reader);
      shadow.size = read_f32(reader);
      style.drop_shadows.push_back(shadow);
    }

    const auto glow_count = read_count(reader, "outer glows");
    style.outer_glows.reserve(glow_count);
    for (std::uint16_t i = 0; i < glow_count; ++i) {
      LayerOuterGlow glow;
      glow.enabled = read_bool(reader);
      glow.blend_mode = blend_mode_from_key(read_signature(reader));
      glow.color = read_rgb_color(reader);
      glow.opacity = read_f32(reader);
      glow.spread = read_f32(reader);
      glow.size = read_f32(reader);
      style.outer_glows.push_back(glow);
    }

    const auto gradient_fill_count = read_count(reader, "gradient fills");
    style.gradient_fills.reserve(gradient_fill_count);
    for (std::uint16_t i = 0; i < gradient_fill_count; ++i) {
      LayerGradientFill fill;
      fill.enabled = read_bool(reader);
      fill.blend_mode = blend_mode_from_key(read_signature(reader));
      fill.opacity = read_f32(reader);
      fill.gradient = read_layer_style_gradient(reader);
      style.gradient_fills.push_back(std::move(fill));
    }

    const auto stroke_count = read_count(reader, "strokes");
    style.strokes.reserve(stroke_count);
    for (std::uint16_t i = 0; i < stroke_count; ++i) {
      LayerStroke stroke;
      stroke.enabled = read_bool(reader);
      stroke.blend_mode = blend_mode_from_key(read_signature(reader));
      stroke.color = read_rgb_color(reader);
      stroke.opacity = read_f32(reader);
      stroke.size = read_f32(reader);
      stroke.position = stroke_position_from_value(reader.read_u8());
      stroke.uses_gradient = read_bool(reader);
      stroke.gradient = read_layer_style_gradient(reader);
      style.strokes.push_back(std::move(stroke));
    }

    const auto bevel_count = read_count(reader, "bevels");
    style.bevels.reserve(bevel_count);
    for (std::uint16_t i = 0; i < bevel_count; ++i) {
      LayerBevelEmboss bevel;
      bevel.enabled = read_bool(reader);
      bevel.highlight_blend_mode = blend_mode_from_key(read_signature(reader));
      bevel.highlight_color = read_rgb_color(reader);
      bevel.highlight_opacity = read_f32(reader);
      bevel.shadow_blend_mode = blend_mode_from_key(read_signature(reader));
      bevel.shadow_color = read_rgb_color(reader);
      bevel.shadow_opacity = read_f32(reader);
      bevel.angle_degrees = read_f32(reader);
      bevel.altitude_degrees = read_f32(reader);
      bevel.depth = read_f32(reader);
      bevel.size = read_f32(reader);
      bevel.direction_up = read_bool(reader);
      style.bevels.push_back(bevel);
    }

    if (reader.remaining() > 0) {
      const auto overlay_count = read_count(reader, "color overlays");
      style.color_overlays.reserve(overlay_count);
      for (std::uint16_t i = 0; i < overlay_count; ++i) {
        LayerColorOverlay overlay;
        overlay.enabled = read_bool(reader);
        overlay.blend_mode = blend_mode_from_key(read_signature(reader));
        overlay.color = read_rgb_color(reader);
        overlay.opacity = read_f32(reader);
        style.color_overlays.push_back(overlay);
      }
    }

    return style;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::uint8_t adjustment_kind_value(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
      return 0U;
    case AdjustmentKind::Curves:
      return 1U;
    case AdjustmentKind::HueSaturation:
      return 2U;
    case AdjustmentKind::ColorBalance:
      return 3U;
  }
  return 0U;
}

AdjustmentKind adjustment_kind_from_value(std::uint8_t value) {
  switch (value) {
    case 1U:
      return AdjustmentKind::Curves;
    case 2U:
      return AdjustmentKind::HueSaturation;
    case 3U:
      return AdjustmentKind::ColorBalance;
    default:
      return AdjustmentKind::Levels;
  }
}

std::uint8_t curves_channel_value(CurvesChannel channel) {
  switch (channel) {
    case CurvesChannel::Rgb:
      return 0U;
    case CurvesChannel::Red:
      return 1U;
    case CurvesChannel::Green:
      return 2U;
    case CurvesChannel::Blue:
      return 3U;
  }
  return 0U;
}

std::optional<CurvesChannel> curves_channel_from_value(std::uint8_t value) {
  switch (value) {
    case 0U:
      return CurvesChannel::Rgb;
    case 1U:
      return CurvesChannel::Red;
    case 2U:
      return CurvesChannel::Green;
    case 3U:
      return CurvesChannel::Blue;
    default:
      return std::nullopt;
  }
}

std::uint8_t levels_channel_value(LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return 1U;
    case LevelsChannel::Green:
      return 2U;
    case LevelsChannel::Blue:
      return 3U;
    case LevelsChannel::Rgb:
      return 0U;
  }
  return 0U;
}

LevelsChannel levels_channel_from_value(int value) {
  switch (value) {
    case 1:
      return LevelsChannel::Red;
    case 2:
      return LevelsChannel::Green;
    case 3:
      return LevelsChannel::Blue;
    default:
      return LevelsChannel::Rgb;
  }
}

LevelsRecord clamp_levels_record(LevelsRecord record) {
  record.black_input = std::clamp(record.black_input, 0, 254);
  record.white_input = std::clamp(record.white_input, record.black_input + 1, 255);
  record.gamma_percent = std::clamp(record.gamma_percent, 10, 999);
  record.black_output = std::clamp(record.black_output, 0, 255);
  record.white_output = std::clamp(record.white_output, record.black_output, 255);
  return record;
}

LevelsRecord levels_master_record(LevelsAdjustment settings) {
  return clamp_levels_record(LevelsRecord{settings.black_input, settings.white_input, settings.gamma_percent,
                                          settings.black_output, settings.white_output});
}

void set_levels_master_record(LevelsAdjustment& settings, LevelsRecord record) {
  record = clamp_levels_record(record);
  settings.black_input = record.black_input;
  settings.white_input = record.white_input;
  settings.gamma_percent = record.gamma_percent;
  settings.black_output = record.black_output;
  settings.white_output = record.white_output;
}

LevelsRecord levels_record_for_photoshop_index(LevelsAdjustment settings, int index) {
  switch (index) {
    case 0:
      return levels_master_record(settings);
    case 1:
      return clamp_levels_record(settings.red);
    case 2:
      return clamp_levels_record(settings.green);
    case 3:
      return clamp_levels_record(settings.blue);
    default:
      return {};
  }
}

void set_levels_record_for_photoshop_index(LevelsAdjustment& settings, int index, LevelsRecord record) {
  record = clamp_levels_record(record);
  switch (index) {
    case 0:
      set_levels_master_record(settings, record);
      return;
    case 1:
      settings.red = record;
      return;
    case 2:
      settings.green = record;
      return;
    case 3:
      settings.blue = record;
      return;
    default:
      return;
  }
}

void write_i32(BigEndianWriter& writer, int value) {
  writer.write_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
}

int read_i32(BigEndianReader& reader) {
  return static_cast<int>(static_cast<std::int32_t>(reader.read_u32()));
}

void write_i16(BigEndianWriter& writer, int value) {
  writer.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
}

int read_i16(BigEndianReader& reader) {
  return static_cast<int>(static_cast<std::int16_t>(reader.read_u16()));
}

void write_levels_record_i32(BigEndianWriter& writer, LevelsRecord record) {
  record = clamp_levels_record(record);
  write_i32(writer, record.black_input);
  write_i32(writer, record.white_input);
  write_i32(writer, record.gamma_percent);
  write_i32(writer, record.black_output);
  write_i32(writer, record.white_output);
}

LevelsRecord read_levels_record_i32(BigEndianReader& reader) {
  return clamp_levels_record(
      LevelsRecord{read_i32(reader), read_i32(reader), read_i32(reader), read_i32(reader), read_i32(reader)});
}

void write_photoshop_levels_record(BigEndianWriter& writer, LevelsRecord record) {
  record = clamp_levels_record(record);
  writer.write_u16(static_cast<std::uint16_t>(record.black_input));
  writer.write_u16(static_cast<std::uint16_t>(record.white_input));
  writer.write_u16(static_cast<std::uint16_t>(record.black_output));
  writer.write_u16(static_cast<std::uint16_t>(record.white_output));
  writer.write_u16(static_cast<std::uint16_t>(record.gamma_percent));
}

LevelsRecord read_photoshop_levels_record(BigEndianReader& reader) {
  const auto black_input = static_cast<int>(reader.read_u16());
  const auto white_input = static_cast<int>(reader.read_u16());
  const auto black_output = static_cast<int>(reader.read_u16());
  const auto white_output = static_cast<int>(reader.read_u16());
  const auto gamma_percent = static_cast<int>(reader.read_u16());
  return clamp_levels_record(LevelsRecord{black_input, white_input, gamma_percent, black_output, white_output});
}

std::vector<std::uint8_t> photoshop_levels_payload(LevelsAdjustment settings) {
  BigEndianWriter writer;
  writer.write_u16(kPhotoshopLevelsAdjustmentVersion);
  for (int index = 0; index < kPhotoshopLevelsRecordCount; ++index) {
    write_photoshop_levels_record(writer, levels_record_for_photoshop_index(settings, index));
  }
  return writer.bytes();
}

std::optional<AdjustmentSettings> parse_photoshop_levels_adjustment(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (reader.read_u16() != kPhotoshopLevelsAdjustmentVersion ||
        reader.remaining() < static_cast<std::size_t>(kPhotoshopLevelsRecordCount) * 10U) {
      return std::nullopt;
    }
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Levels;
    for (int index = 0; index < kPhotoshopLevelsRecordCount; ++index) {
      const auto record = read_photoshop_levels_record(reader);
      if (index < 4) {
        set_levels_record_for_photoshop_index(settings.levels, index, record);
      }
    }
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Photoshop's hue2 hue fields store -180..180; the model keeps 0..360 (UI convention).
int hue2_file_hue_to_model(int hue) {
  return ((hue % 360) + 360) % 360;
}

int hue2_model_hue_to_file(int hue) {
  const auto normalized = ((hue % 360) + 360) % 360;
  return normalized > 180 ? normalized - 360 : normalized;
}

std::optional<AdjustmentSettings> parse_photoshop_hue2_adjustment(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (reader.read_u16() != kPhotoshopHueSaturationVersion ||
        reader.remaining() < kPhotoshopHueSaturationHeaderSize - 2U) {
      return std::nullopt;
    }
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation.colorize = reader.read_u8() != 0;
    reader.skip(1);  // padding
    settings.hue_saturation.colorize_hue = hue2_file_hue_to_model(read_i16(reader));
    settings.hue_saturation.colorize_saturation = std::clamp(read_i16(reader), 0, 100);
    settings.hue_saturation.colorize_lightness = std::clamp(read_i16(reader), -100, 100);
    settings.hue_saturation.hue_shift = std::clamp(read_i16(reader), -180, 180);
    settings.hue_saturation.saturation_delta = std::clamp(read_i16(reader), -100, 100);
    settings.hue_saturation.lightness_delta = std::clamp(read_i16(reader), -100, 100);
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// The six per-hextant band records plus the undocumented 36-byte trailer exactly as
// Photoshop 2026 writes them for a fresh Hue/Saturation layer (COM byte capture, July
// 2026; identical for colorize on/off). Bands are preserved but not rendered.
constexpr std::array<std::uint8_t, 120> kPhotoshopHueSaturationDefaultTail = {
    0x01, 0x3B, 0x01, 0x59, 0x00, 0x0F, 0x00, 0x2D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0F, 0x00, 0x2D, 0x00, 0x4B, 0x00, 0x69, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x4B, 0x00, 0x69, 0x00, 0x87, 0x00, 0xA5,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87, 0x00, 0xA5, 0x00, 0xC3,
    0x00, 0xE1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x00, 0xE1,
    0x00, 0xFF, 0x01, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x1D, 0x01, 0x3B, 0x01, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x64, 0x00, 0x32, 0x00, 0x3C, 0x00, 0x64, 0x00, 0x32,
    0x00, 0x78, 0x00, 0x64, 0x00, 0x32, 0x00, 0xB4, 0x00, 0x64, 0x00, 0x32,
    0x00, 0xF0, 0x00, 0x64, 0x00, 0x32, 0x01, 0x2C, 0x00, 0x64, 0x00, 0x32,
};

std::vector<std::uint8_t> photoshop_hue2_payload(const HueSaturationAdjustment& settings,
                                                 const UnknownPsdBlock* original) {
  BigEndianWriter header;
  header.write_u16(kPhotoshopHueSaturationVersion);
  header.write_u8(settings.colorize ? 1 : 0);
  header.write_u8(0);  // padding
  write_i16(header, hue2_model_hue_to_file(settings.colorize_hue));
  write_i16(header, std::clamp(settings.colorize_saturation, 0, 100));
  write_i16(header, std::clamp(settings.colorize_lightness, -100, 100));
  write_i16(header, std::clamp(settings.hue_shift, -180, 180));
  write_i16(header, std::clamp(settings.saturation_delta, -100, 100));
  write_i16(header, std::clamp(settings.lightness_delta, -100, 100));

  auto bytes = header.bytes();
  if (original != nullptr && original->payload.size() >= kPhotoshopHueSaturationHeaderSize &&
      original->payload[0] == 0x00 && original->payload[1] == kPhotoshopHueSaturationVersion) {
    // Patch-in-place: everything past the header (band records, trailer) stays
    // byte-identical to the imported payload, so unedited layers round-trip exactly.
    std::vector<std::uint8_t> patched(original->payload.begin(), original->payload.end());
    std::copy(bytes.begin(), bytes.end(), patched.begin());
    return patched;
  }
  bytes.insert(bytes.end(), kPhotoshopHueSaturationDefaultTail.begin(), kPhotoshopHueSaturationDefaultTail.end());
  return bytes;
}

void write_patchy_curves_extension(BigEndianWriter& writer, const CurvesAdjustment& curves) {
  BigEndianWriter extension;
  extension.write_u16(kPatchyCurvesExtensionVersion);
  extension.write_u16(kPatchyCurvesExtensionChannelCount);
  constexpr std::array channels{CurvesChannel::Rgb, CurvesChannel::Red, CurvesChannel::Green, CurvesChannel::Blue};
  for (const auto channel : channels) {
    const auto points = normalized_curve_control_points(curve_points_for_channel(curves, channel));
    extension.write_u8(curves_channel_value(channel));
    extension.write_u8(0U);  // reserved
    extension.write_u16(static_cast<std::uint16_t>(points.size()));
    for (const auto point : points) {
      extension.write_u16(static_cast<std::uint16_t>(point.input));
      extension.write_u16(static_cast<std::uint16_t>(point.output));
    }
  }

  write_signature(writer, kPatchyCurvesExtensionSignature);
  writer.write_u32(static_cast<std::uint32_t>(extension.bytes().size()));
  writer.write_bytes(extension.bytes());
}

std::optional<CurvesAdjustment> parse_patchy_curves_extension(std::span<const std::uint8_t> payload) {
  if (payload.size() > kPatchyCurvesExtensionMaxPayloadSize) {
    return std::nullopt;
  }
  try {
    BigEndianReader reader(payload);
    if (reader.read_u16() != kPatchyCurvesExtensionVersion ||
        reader.read_u16() != kPatchyCurvesExtensionChannelCount) {
      return std::nullopt;
    }

    CurvesAdjustment curves;
    std::array<bool, kPatchyCurvesExtensionChannelCount> seen{};
    for (std::uint16_t index = 0; index < kPatchyCurvesExtensionChannelCount; ++index) {
      const auto channel_value = reader.read_u8();
      const auto channel = curves_channel_from_value(channel_value);
      if (!channel.has_value() || reader.read_u8() != 0U || seen[channel_value]) {
        return std::nullopt;
      }
      seen[channel_value] = true;
      const auto count = reader.read_u16();
      if (count < 2U || count > 19U || reader.remaining() < static_cast<std::size_t>(count) * 4U) {
        return std::nullopt;
      }
      CurveControlPoints points;
      points.reserve(count);
      for (std::uint16_t point_index = 0; point_index < count; ++point_index) {
        points.push_back(CurveControlPoint{static_cast<int>(reader.read_u16()),
                                           static_cast<int>(reader.read_u16())});
      }
      if (normalized_curve_control_points(points) != points) {
        return std::nullopt;
      }
      set_curve_points_for_channel(curves, *channel, std::move(points));
    }
    if (reader.remaining() != 0U || std::any_of(seen.begin(), seen.end(), [](bool value) { return !value; })) {
      return std::nullopt;
    }
    return curves;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool curve_points_are_exact_identity(const CurveControlPoints& points) {
  return points.size() == 2U && points[0] == CurveControlPoint{0, 0} &&
         points[1] == CurveControlPoint{255, 255};
}

std::optional<AdjustmentSettings> parse_photoshop_curves_adjustment(
    std::span<const std::uint8_t> payload) {
  // Photoshop's curv adjustment block begins with one zero byte, followed by
  // the documented Curves-file body. Photoshop 2026 writes a version-1 bitmap
  // body plus its indexed `Crv ` version-4 extension and pads the payload to a
  // four-byte boundary. The shared ACV reader handles both sections and gives
  // the richer indexed extension authority when it is present.
  if (payload.empty() || payload.front() != 0U) {
    return std::nullopt;
  }
  try {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Curves;
    settings.curves = acv::read(payload.subspan(1U));
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> photoshop_curves_payload(const CurvesAdjustment& curves,
                                                   const UnknownPsdBlock* original) {
  if (original != nullptr) {
    if (const auto parsed = parse_photoshop_curves_adjustment(original->payload);
        parsed.has_value() && parsed->curves == curves) {
      // The imported payload may contain compatibility details Patchy does not
      // model. Keep every byte until the modeled control points actually change.
      return original->payload;
    }
  }

  constexpr std::array channels{CurvesChannel::Rgb, CurvesChannel::Red,
                                CurvesChannel::Green, CurvesChannel::Blue};
  struct ActiveCurve {
    std::uint16_t channel{0};
    CurveControlPoints points;
  };
  std::vector<ActiveCurve> active;
  std::uint32_t bitmap = 0U;
  for (std::size_t index = 0; index < channels.size(); ++index) {
    auto points = normalized_curve_control_points(curve_points_for_channel(curves, channels[index]));
    if (curve_points_are_exact_identity(points)) {
      continue;
    }
    bitmap |= 1U << static_cast<unsigned>(index);
    active.push_back(ActiveCurve{static_cast<std::uint16_t>(index), std::move(points)});
  }

  const auto write_curve = [](BigEndianWriter& writer, const CurveControlPoints& points) {
    writer.write_u16(static_cast<std::uint16_t>(points.size()));
    for (const auto point : points) {
      // Photoshop stores each control point as output first, then input.
      writer.write_u16(static_cast<std::uint16_t>(point.output));
      writer.write_u16(static_cast<std::uint16_t>(point.input));
    }
  };

  BigEndianWriter writer;
  writer.write_u8(0U);  // curv adjustment-block prefix
  writer.write_u16(1U);
  // Photoshop 2026 writes this bitmap as four bytes even though Adobe's table
  // labels the field as two. Real captures use 0x0000000f for RGB+R+G+B.
  writer.write_u32(bitmap);
  for (const auto& curve : active) {
    write_curve(writer, curve.points);
  }

  write_signature(writer, kPhotoshopCurvesExtraMarker);
  writer.write_u16(4U);
  writer.write_u32(static_cast<std::uint32_t>(active.size()));
  for (const auto& curve : active) {
    writer.write_u16(curve.channel);
    write_curve(writer, curve.points);
  }
  while ((writer.bytes().size() % 4U) != 0U) {
    writer.write_u8(0U);
  }
  return writer.bytes();
}

bool curves_require_patchy_extension(const CurvesAdjustment& curves) {
  if (!curve_points_are_exact_identity(curves.red) || !curve_points_are_exact_identity(curves.green) ||
      !curve_points_are_exact_identity(curves.blue)) {
    return true;
  }
  if (curve_points_are_exact_identity(curves.rgb)) {
    return false;
  }
  return curves.rgb.size() != 3U || curves.rgb[0].input != 0 || curves.rgb[1].input != 128 ||
         curves.rgb[2].input != 255;
}

std::optional<AdjustmentSettings> parse_patchy_adjustment(std::span<const std::uint8_t> payload);

std::vector<std::uint8_t> patchy_adjustment_payload(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value()) {
    return {};
  }

  if (settings->kind == AdjustmentKind::Curves) {
    for (const auto& block : layer.unknown_psd_blocks()) {
      if (block.key != "plAD") {
        continue;
      }
      const auto original = parse_patchy_adjustment(block.payload);
      if (original.has_value() && original->kind == AdjustmentKind::Curves &&
          original->curves == settings->curves) {
        // Untouched imported Curves payloads remain byte-identical, including an
        // unknown, future, or malformed CRV2 tail. A real Patchy edit changes the
        // modeled points and falls through to regenerate the known v4 shape.
        return block.payload;
      }
      break;
    }
  }

  BigEndianWriter writer;
  write_signature(writer, kPatchyAdjustmentPayloadSignature);
  writer.write_u16(kPatchyAdjustmentVersion);
  writer.write_u8(adjustment_kind_value(settings->kind));
  for (int index = 0; index < 4; ++index) {
    write_levels_record_i32(writer, levels_record_for_photoshop_index(settings->levels, index));
  }
  write_i32(writer, levels_channel_value(settings->levels.channel));
  const auto composite_curve_lut = build_curve_lut(settings->curves.rgb);
  write_i32(writer, composite_curve_lut[0]);
  write_i32(writer, composite_curve_lut[128]);
  write_i32(writer, composite_curve_lut[255]);
  write_i32(writer, settings->hue_saturation.hue_shift);
  write_i32(writer, settings->hue_saturation.saturation_delta);
  write_i32(writer, settings->hue_saturation.lightness_delta);
  write_i32(writer, settings->color_balance.cyan_red);
  write_i32(writer, settings->color_balance.magenta_green);
  write_i32(writer, settings->color_balance.yellow_blue);
  // Trailing version-4 extension (July 2026): Hue/Saturation colorize. Kept under
  // version 4 because shipped parsers tolerate longer payloads but reject unknown
  // versions - a bump would make every new adjustment file unreadable in old builds.
  write_i32(writer, settings->hue_saturation.colorize ? 1 : 0);
  write_i32(writer, settings->hue_saturation.colorize_hue);
  write_i32(writer, settings->hue_saturation.colorize_saturation);
  write_i32(writer, settings->hue_saturation.colorize_lightness);
  if (settings->kind == AdjustmentKind::Curves && curves_require_patchy_extension(settings->curves)) {
    // Length-delimited extension under the intentionally unchanged plAD v4.
    // Older builds ignore trailing bytes and continue to use the three legacy
    // composite outputs written above.
    write_patchy_curves_extension(writer, settings->curves);
  }
  return writer.bytes();
}

std::optional<AdjustmentSettings> parse_patchy_adjustment(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (read_signature(reader) != kPatchyAdjustmentPayloadSignature) {
      return std::nullopt;
    }
    if (reader.read_u16() != kPatchyAdjustmentVersion) {
      return std::nullopt;
    }
    constexpr auto expected_i32_count = 30U;
    if (reader.remaining() < 1U + expected_i32_count * 4U) {
      return std::nullopt;
    }

    AdjustmentSettings settings;
    settings.kind = adjustment_kind_from_value(reader.read_u8());
    for (int index = 0; index < 4; ++index) {
      set_levels_record_for_photoshop_index(settings.levels, index, read_levels_record_i32(reader));
    }
    settings.levels.channel = levels_channel_from_value(read_i32(reader));
    const auto legacy_curve_shadow = read_i32(reader);
    const auto legacy_curve_midtone = read_i32(reader);
    const auto legacy_curve_highlight = read_i32(reader);
    settings.curves =
        curves_adjustment_from_legacy_outputs(legacy_curve_shadow, legacy_curve_midtone, legacy_curve_highlight);
    settings.hue_saturation.hue_shift = read_i32(reader);
    settings.hue_saturation.saturation_delta = read_i32(reader);
    settings.hue_saturation.lightness_delta = read_i32(reader);
    settings.color_balance.cyan_red = read_i32(reader);
    settings.color_balance.magenta_green = read_i32(reader);
    settings.color_balance.yellow_blue = read_i32(reader);
    if (reader.remaining() >= 16U) {
      // Version-4 trailing colorize extension; absent in pre-July-2026 files.
      settings.hue_saturation.colorize = read_i32(reader) != 0;
      settings.hue_saturation.colorize_hue = std::clamp(read_i32(reader), 0, 360) % 360;
      settings.hue_saturation.colorize_saturation = std::clamp(read_i32(reader), 0, 100);
      settings.hue_saturation.colorize_lightness = std::clamp(read_i32(reader), -100, 100);
    }
    if (settings.kind == AdjustmentKind::Curves && reader.remaining() >= 8U &&
        read_signature(reader) == kPatchyCurvesExtensionSignature) {
      const auto extension_length = static_cast<std::size_t>(reader.read_u32());
      if (extension_length <= kPatchyCurvesExtensionMaxPayloadSize && extension_length <= reader.remaining()) {
        const auto extension = reader.read_bytes(extension_length);
        if (const auto rich_curves = parse_patchy_curves_extension(extension); rich_curves.has_value()) {
          settings.curves = *rich_curves;
        }
      }
      // A malformed or unknown rich tail never invalidates the legacy plAD
      // fields above. This is the compatibility escape hatch for old files and
      // future extensions that retain version 4.
    }
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string read_pascal_string(BigEndianReader& reader, std::size_t padded_multiple) {
  const auto start = reader.position();
  const auto length = reader.read_u8();
  auto bytes = reader.read_bytes(length);
  const auto consumed = reader.position() - start;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  if (padded > consumed) {
    reader.skip(padded - consumed);
  }
  return std::string(bytes.begin(), bytes.end());
}

void write_pascal_string(BigEndianWriter& writer, const std::string& value, std::size_t padded_multiple) {
  const auto length = std::min<std::size_t>(value.size(), 255);
  writer.write_u8(static_cast<std::uint8_t>(length));
  writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), length));
  const auto consumed = 1 + length;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  for (std::size_t i = consumed; i < padded; ++i) {
    writer.write_u8(0);
  }
}

std::optional<std::vector<ImageResource>> read_image_resources(std::span<const std::uint8_t> bytes) {
  BigEndianReader reader(bytes);
  std::vector<ImageResource> resources;
  while (reader.remaining() > 0) {
    if (reader.remaining() < 12) {
      return std::nullopt;
    }
    ImageResource resource;
    resource.signature = read_signature(reader);
    if (resource.signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
        resource.signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
      return std::nullopt;
    }
    resource.id = reader.read_u16();
    resource.name = read_pascal_string(reader, 2);
    const auto payload_length = reader.read_u32();
    if (payload_length > reader.remaining()) {
      return std::nullopt;
    }
    resource.payload = reader.read_bytes(payload_length);
    if ((payload_length % 2U) != 0) {
      if (reader.remaining() == 0) {
        return std::nullopt;
      }
      reader.skip(1);
    }
    resources.push_back(std::move(resource));
  }
  return resources;
}

void write_image_resource(BigEndianWriter& writer, const ImageResource& resource) {
  write_signature(writer, resource.signature);
  writer.write_u16(resource.id);
  write_pascal_string(writer, resource.name, 2);
  writer.write_u32(checked_u32(resource.payload.size(), "image resource payload"));
  writer.write_bytes(resource.payload);
  if ((resource.payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

std::vector<std::uint8_t> write_image_resources(std::span<const ImageResource> resources) {
  BigEndianWriter writer;
  for (const auto& resource : resources) {
    write_image_resource(writer, resource);
  }
  return writer.bytes();
}

std::optional<std::vector<std::uint8_t>> find_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id) {
  auto parsed = read_image_resources(resources);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  for (const auto& resource : *parsed) {
    if (resource.id == id) {
      return resource.payload;
    }
  }
  return std::nullopt;
}

std::vector<std::string> parse_legacy_alpha_channel_names(std::span<const std::uint8_t> payload) {
  std::vector<std::string> names;
  std::size_t offset = 0;
  while (offset < payload.size()) {
    const auto length = static_cast<std::size_t>(payload[offset++]);
    if (length > payload.size() - offset) {
      break;
    }
    names.emplace_back(reinterpret_cast<const char*>(payload.data() + offset), length);
    offset += length;
  }
  return names;
}

std::vector<std::string> parse_unicode_alpha_channel_names(std::span<const std::uint8_t> payload) {
  BigEndianReader reader(payload);
  std::vector<std::string> names;
  while (reader.remaining() >= 4U) {
    const auto unit_count = reader.read_u32();
    if (unit_count > reader.remaining() / 2U) {
      break;
    }
    std::string decoded;
    for (std::uint32_t index = 0; index < unit_count; ++index) {
      auto codepoint = static_cast<std::uint32_t>(reader.read_u16());
      if (codepoint == 0) {
        continue;
      }
      if (codepoint >= 0xD800U && codepoint <= 0xDBFFU && index + 1U < unit_count) {
        const auto low = static_cast<std::uint32_t>(reader.read_u16());
        ++index;
        if (low >= 0xDC00U && low <= 0xDFFFU) {
          codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
        } else {
          codepoint = '?';
        }
      }
      append_utf8(decoded, codepoint);
    }
    names.push_back(std::move(decoded));
  }
  return names;
}

std::vector<std::uint32_t> parse_alpha_identifiers(std::span<const std::uint8_t> payload) {
  if (payload.size() < 4U) {
    return {};
  }
  BigEndianReader reader(payload);
  const auto count = reader.read_u32();
  if (count > reader.remaining() / 4U) {
    return {};
  }
  std::vector<std::uint32_t> identifiers;
  identifiers.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    identifiers.push_back(reader.read_u32());
  }
  return identifiers;
}

std::vector<std::vector<std::uint8_t>> parse_display_info_records(
    std::span<const std::uint8_t> payload, bool floating_point_resource) {
  std::size_t offset = 0;
  const auto record_size = floating_point_resource ? 13U : 14U;
  if (floating_point_resource) {
    if (payload.size() < 4U || BigEndianReader(payload.first(4)).read_u32() != 1U) {
      return {};
    }
    offset = 4U;
  }
  if ((payload.size() - offset) % record_size != 0U) {
    return {};
  }
  std::vector<std::vector<std::uint8_t>> records;
  records.reserve((payload.size() - offset) / record_size);
  while (offset < payload.size()) {
    records.emplace_back(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                         payload.begin() + static_cast<std::ptrdiff_t>(offset + record_size));
    offset += record_size;
  }
  return records;
}

ParsedCompositeChannelResources parse_composite_channel_resources(
    std::span<const std::uint8_t> image_resources) {
  ParsedCompositeChannelResources result;
  const auto parsed = read_image_resources(image_resources);
  if (!parsed.has_value()) {
    return result;
  }
  std::optional<std::span<const std::uint8_t>> legacy_display;
  std::optional<std::span<const std::uint8_t>> modern_display;
  for (const auto& resource : *parsed) {
    switch (resource.id) {
      case kImageResourceAlphaChannelNames:
        if (result.legacy_names.empty()) {
          result.legacy_names = parse_legacy_alpha_channel_names(resource.payload);
        }
        break;
      case kImageResourceUnicodeAlphaChannelNames:
        if (result.unicode_names.empty()) {
          result.unicode_names = parse_unicode_alpha_channel_names(resource.payload);
        }
        break;
      case kImageResourceAlphaIdentifiers:
        if (result.identifiers.empty()) {
          result.identifiers = parse_alpha_identifiers(resource.payload);
        }
        break;
      case kImageResourceDisplayInfo:
        if (!legacy_display.has_value()) {
          legacy_display = resource.payload;
        }
        break;
      case kImageResourceDisplayInfoFloat:
        if (!modern_display.has_value()) {
          modern_display = resource.payload;
        }
        break;
      default:
        break;
    }
  }
  if (modern_display.has_value()) {
    result.display_records = parse_display_info_records(*modern_display, true);
  }
  if (result.display_records.empty() && legacy_display.has_value()) {
    result.display_records = parse_display_info_records(*legacy_display, false);
  }
  return result;
}

DocumentChannelDisplayInfo display_info_from_photoshop_record(std::span<const std::uint8_t> record) {
  DocumentChannelDisplayInfo info;
  if (record.size() < 13U) {
    return info;
  }
  BigEndianReader reader(record.first(13U));
  const auto color_space = reader.read_u16();
  std::array<std::uint16_t, 4> components{};
  for (auto& component : components) {
    component = reader.read_u16();
  }
  const auto opacity_percent = reader.read_u16();
  const auto mode = reader.read_u8();
  if (color_space == 0U) {  // RGB, 16-bit unsigned components.
    const auto component8 = [](std::uint16_t value) {
      return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) + 128U) / 257U);
    };
    info.color = RgbColor{component8(components[0]), component8(components[1]), component8(components[2])};
  }
  info.opacity = std::clamp(static_cast<float>(opacity_percent) / 100.0F, 0.0F, 1.0F);
  info.color_indicates = mode == 2U   ? DocumentChannelColorIndicates::SpotColor
                         : mode == 0U ? DocumentChannelColorIndicates::SelectedAreas
                                      : DocumentChannelColorIndicates::MaskedAreas;
  return info;
}

std::uint16_t composite_color_channel_count(std::uint16_t color_mode) noexcept {
  return is_cmyk_color_mode(color_mode) ? 4U : 3U;
}

void add_saved_composite_channels(Document& document,
                                  std::vector<std::vector<std::uint8_t>> channel_planes,
                                  std::uint16_t first_saved_channel, const Header& header,
                                  const ParsedCompositeChannelResources& resources) {
  const auto color_channels = composite_color_channel_count(header.color_mode);
  if (first_saved_channel < color_channels || first_saved_channel > header.channels) {
    throw std::runtime_error("Invalid PSD saved channel layout");
  }
  const auto expected_count = static_cast<std::size_t>(header.channels - first_saved_channel);
  if (channel_planes.size() != expected_count) {
    throw std::runtime_error("PSD saved channel count does not match the composite data");
  }
  const auto first_resource_index = static_cast<std::size_t>(first_saved_channel - color_channels);
  const auto pixel_count = static_cast<std::size_t>(document.width()) * static_cast<std::size_t>(document.height());
  std::size_t alpha_identifier_index = 0;
  for (std::size_t index = 0; index < channel_planes.size(); ++index) {
    if (channel_planes[index].size() != pixel_count) {
      throw std::runtime_error("PSD saved channel dimensions do not match the document");
    }
    const auto aligned_index = [first_resource_index, index, saved_count = channel_planes.size()](
                                   std::size_t resource_count) {
      // Modern Photoshop describes merged transparency in the name/display arrays;
      // some older writers omit that derived entry. Align either shape without using
      // the literal channel name to decide whether a plane is merged transparency.
      return first_resource_index != 0U && resource_count == saved_count ? index
                                                                         : first_resource_index + index;
    };
    const auto unicode_index = aligned_index(resources.unicode_names.size());
    const auto legacy_index = aligned_index(resources.legacy_names.size());
    const auto display_index = aligned_index(resources.display_records.size());
    std::string name;
    if (unicode_index < resources.unicode_names.size() && !resources.unicode_names[unicode_index].empty()) {
      name = resources.unicode_names[unicode_index];
    } else if (legacy_index < resources.legacy_names.size() &&
               !resources.legacy_names[legacy_index].empty()) {
      name = resources.legacy_names[legacy_index];
    } else {
      name = "Alpha " + std::to_string(index + 1U);
    }

    DocumentChannelDisplayInfo display_info;
    std::vector<std::uint8_t> raw_display_info;
    if (display_index < resources.display_records.size()) {
      raw_display_info = resources.display_records[display_index];
      display_info = display_info_from_photoshop_record(raw_display_info);
    }
    const auto kind = display_info.color_indicates == DocumentChannelColorIndicates::SpotColor
                          ? DocumentChannelKind::Spot
                          : DocumentChannelKind::Alpha;
    PixelBuffer pixels(document.width(), document.height(), PixelFormat::gray8());
    std::copy(channel_planes[index].begin(), channel_planes[index].end(), pixels.data().begin());
    DocumentChannel channel(document.allocate_channel_id(), std::move(name), kind, std::move(pixels));
    // Resource 1053 contains identifiers for saved alpha channels only.
    // Photoshop omits spot channels, so consume this array independently of
    // the name/display arrays that describe every extra plane.
    if (kind == DocumentChannelKind::Alpha &&
        alpha_identifier_index < resources.identifiers.size()) {
      channel.set_photoshop_identifier(resources.identifiers[alpha_identifier_index]);
      ++alpha_identifier_index;
    }
    channel.set_display_info(display_info);
    if (!raw_display_info.empty()) {
      channel.set_raw_photoshop_display_info(std::move(raw_display_info));
    }
    document.add_channel(std::move(channel));
  }
}

double sanitized_print_ppi(double value) noexcept {
  return std::isfinite(value) && value > 0.0 ? value : 300.0;
}

double fixed_16_16_to_double(std::uint32_t value) noexcept {
  return static_cast<double>(value) / 65536.0;
}

std::uint32_t double_to_fixed_16_16(double value) noexcept {
  value = std::clamp(sanitized_print_ppi(value), 1.0, 9999.0);
  return static_cast<std::uint32_t>(std::lround(value * 65536.0));
}

double resolution_unit_to_ppi(double value, std::uint16_t unit) noexcept {
  // Photoshop resolution resource unit 1 is pixels/inch, 2 is pixels/cm.
  return unit == 2 ? value * 2.54 : value;
}

std::optional<DocumentPrintSettings> print_settings_from_resolution_resource(std::span<const std::uint8_t> payload) {
  if (payload.size() < 16U) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  const auto horizontal = fixed_16_16_to_double(reader.read_u32());
  const auto horizontal_unit = reader.read_u16();
  (void)reader.read_u16();  // width display unit
  const auto vertical = fixed_16_16_to_double(reader.read_u32());
  const auto vertical_unit = reader.read_u16();
  (void)reader.read_u16();  // height display unit

  DocumentPrintSettings settings;
  settings.horizontal_ppi = sanitized_print_ppi(resolution_unit_to_ppi(horizontal, horizontal_unit));
  settings.vertical_ppi = sanitized_print_ppi(resolution_unit_to_ppi(vertical, vertical_unit));
  return settings;
}

std::vector<std::uint8_t> resolution_resource_for_document(const Document& document) {
  BigEndianWriter writer;
  writer.write_u32(double_to_fixed_16_16(document.print_settings().horizontal_ppi));
  writer.write_u16(1);  // pixels/inch
  writer.write_u16(1);  // inches
  writer.write_u32(double_to_fixed_16_16(document.print_settings().vertical_ppi));
  writer.write_u16(1);  // pixels/inch
  writer.write_u16(1);  // inches
  return writer.bytes();
}

std::int32_t sanitized_grid_cycle_32(std::int32_t value) noexcept {
  return value > 0 ? value : kDefaultGridCycle32;
}

std::int32_t sanitized_guide_position_32(std::int32_t value) noexcept {
  return std::max<std::int32_t>(0, value);
}

std::optional<std::pair<DocumentGridSettings, std::vector<DocumentGuide>>>
grid_guides_from_resource(std::span<const std::uint8_t> payload) {
  if (payload.size() < 16U) {
    return std::nullopt;
  }

  try {
    BigEndianReader reader(payload);
    const auto version = reader.read_u32();
    if (version != 1U) {
      return std::nullopt;
    }

    DocumentGridSettings settings;
    settings.horizontal_cycle_32 = sanitized_grid_cycle_32(read_i32(reader));
    settings.vertical_cycle_32 = sanitized_grid_cycle_32(read_i32(reader));
    const auto guide_count = reader.read_u32();
    if (guide_count > (reader.remaining() / 5U)) {
      return std::nullopt;
    }

    std::vector<DocumentGuide> guides;
    guides.reserve(static_cast<std::size_t>(guide_count));
    for (std::uint32_t index = 0; index < guide_count; ++index) {
      DocumentGuide guide;
      guide.position_32 = sanitized_guide_position_32(read_i32(reader));
      const auto direction = reader.read_u8();
      guide.orientation = direction == 1U ? GuideOrientation::Horizontal : GuideOrientation::Vertical;
      guides.push_back(guide);
    }
    return std::pair<DocumentGridSettings, std::vector<DocumentGuide>>{settings, std::move(guides)};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> grid_guides_resource_for_document(const Document& document) {
  BigEndianWriter writer;
  writer.write_u32(1);
  writer.write_u32(static_cast<std::uint32_t>(sanitized_grid_cycle_32(document.grid_settings().horizontal_cycle_32)));
  writer.write_u32(static_cast<std::uint32_t>(sanitized_grid_cycle_32(document.grid_settings().vertical_cycle_32)));
  writer.write_u32(checked_u32(document.guides().size(), "guide count"));
  for (const auto& guide : document.guides()) {
    writer.write_u32(static_cast<std::uint32_t>(sanitized_guide_position_32(guide.position_32)));
    writer.write_u8(guide.orientation == GuideOrientation::Horizontal ? 1U : 0U);
  }
  return writer.bytes();
}

void upsert_image_resource(std::vector<ImageResource>& resources, std::uint16_t id,
                           std::vector<std::uint8_t> payload) {
  bool replaced = false;
  for (auto it = resources.begin(); it != resources.end();) {
    if (it->id != id) {
      ++it;
      continue;
    }
    if (!replaced) {
      it->signature = {'8', 'B', 'I', 'M'};
      it->name.clear();
      it->payload = std::move(payload);
      replaced = true;
      ++it;
    } else {
      it = resources.erase(it);
    }
  }
  if (!replaced) {
    resources.push_back(ImageResource{std::array<char, 4>{'8', 'B', 'I', 'M'}, id, {}, std::move(payload)});
  }
}

void remove_image_resource(std::vector<ImageResource>& resources, std::uint16_t id) {
  resources.erase(std::remove_if(resources.begin(), resources.end(),
                                 [id](const ImageResource& resource) { return resource.id == id; }),
                  resources.end());
}

[[nodiscard]] std::vector<std::uint8_t> patchy_palette_resource(std::span<const RgbColor> colors, bool mode_active,
                                                                std::uint8_t alpha_threshold) {
  std::vector<std::uint8_t> payload;
  payload.reserve(12U + colors.size() * 3U);
  const auto push_u16 = [&payload](std::uint16_t value) {
    payload.push_back(static_cast<std::uint8_t>(value >> 8U));
    payload.push_back(static_cast<std::uint8_t>(value & 0xffU));
  };
  payload.push_back(static_cast<std::uint8_t>((kPatchyPaletteMagic >> 24U) & 0xffU));
  payload.push_back(static_cast<std::uint8_t>((kPatchyPaletteMagic >> 16U) & 0xffU));
  payload.push_back(static_cast<std::uint8_t>((kPatchyPaletteMagic >> 8U) & 0xffU));
  payload.push_back(static_cast<std::uint8_t>(kPatchyPaletteMagic & 0xffU));
  push_u16(1);  // version
  push_u16(mode_active ? 1U : 0U);
  payload.push_back(alpha_threshold);
  payload.push_back(0);  // reserved
  push_u16(static_cast<std::uint16_t>(colors.size()));
  for (const auto& color : colors) {
    payload.push_back(color.red);
    payload.push_back(color.green);
    payload.push_back(color.blue);
  }
  return payload;
}

// Malformed payloads are ignored: the file still opens as a plain RGB document.
void apply_patchy_palette_resource(Document& document, std::span<const std::uint8_t> payload) {
  if (payload.size() < 12U) {
    return;
  }
  const auto magic = (static_cast<std::uint32_t>(payload[0]) << 24U) |
                     (static_cast<std::uint32_t>(payload[1]) << 16U) |
                     (static_cast<std::uint32_t>(payload[2]) << 8U) | static_cast<std::uint32_t>(payload[3]);
  const auto version = static_cast<std::uint16_t>((payload[4] << 8U) | payload[5]);
  if (magic != kPatchyPaletteMagic || version != 1) {
    return;
  }
  const auto flags = static_cast<std::uint16_t>((payload[6] << 8U) | payload[7]);
  const auto alpha_threshold = payload[8];
  const auto count = static_cast<std::uint16_t>((payload[10] << 8U) | payload[11]);
  if (count == 0 || count > 256 || payload.size() < 12U + static_cast<std::size_t>(count) * 3U) {
    return;
  }
  std::vector<RgbColor> colors;
  colors.reserve(count);
  for (std::uint16_t index = 0; index < count; ++index) {
    const auto offset = 12U + static_cast<std::size_t>(index) * 3U;
    colors.push_back(RgbColor{payload[offset], payload[offset + 1U], payload[offset + 2U]});
  }
  const std::uint16_t depth = count <= 4 ? 2 : (count <= 16 ? 4 : 8);
  document.indexed_palette() = DocumentIndexedPalette{colors, depth};
  if ((flags & 1U) != 0U) {
    DocumentPaletteEditing editing;
    editing.palette.colors = std::move(colors);
    editing.alpha_threshold = alpha_threshold == 0 ? std::uint8_t{128} : alpha_threshold;
    document.palette_editing() = std::move(editing);
  }
}

std::vector<std::uint8_t> alpha_channel_names_resource(
    std::span<const CompositeChannelInfo> channels) {
  std::vector<std::uint8_t> payload;
  for (const auto& channel : channels) {
    // Resource 1006 is a legacy one-byte Pascal string array. Photoshop uses
    // one '?' per Unicode scalar that is not representable there; the exact
    // UTF-8 name belongs in resource 1045.
    std::vector<std::uint8_t> legacy_name;
    legacy_name.reserve(std::min<std::size_t>(channel.name.size(), 255U));
    const auto units = utf8_to_utf16(channel.name);
    for (std::size_t index = 0; index < units.size() && legacy_name.size() < 255U; ++index) {
      const auto unit = units[index];
      if (unit >= 0xD800U && unit <= 0xDBFFU && index + 1U < units.size() &&
          units[index + 1U] >= 0xDC00U && units[index + 1U] <= 0xDFFFU) {
        ++index;
      }
      legacy_name.push_back(unit <= 0x7FU ? static_cast<std::uint8_t>(unit)
                                         : static_cast<std::uint8_t>('?'));
    }
    payload.push_back(static_cast<std::uint8_t>(legacy_name.size()));
    payload.insert(payload.end(), legacy_name.begin(), legacy_name.end());
  }
  return payload;
}

std::vector<std::uint8_t> unicode_alpha_channel_names_resource(
    std::span<const CompositeChannelInfo> channels) {
  BigEndianWriter writer;
  for (const auto& channel : channels) {
    const auto units = utf8_to_utf16(channel.name);
    writer.write_u32(checked_u32(units.size() + 1U, "Unicode alpha channel name length"));
    for (const auto unit : units) {
      writer.write_u16(unit);
    }
    writer.write_u16(0);  // Photoshop includes the terminator in the unit count.
  }
  return writer.bytes();
}

std::vector<std::uint8_t> alpha_identifiers_resource(
    std::span<const CompositeChannelInfo> channels) {
  const auto has_alpha_identifier = [](const CompositeChannelInfo& channel) {
    return !channel.merged_transparency && channel.alpha_identifier_eligible;
  };
  std::vector<std::uint32_t> used;
  for (const auto& channel : channels) {
    if (has_alpha_identifier(channel) && channel.photoshop_identifier.has_value()) {
      used.push_back(*channel.photoshop_identifier);
    }
  }
  std::uint32_t next_identifier = 1U;
  const auto allocate_identifier = [&used, &next_identifier]() {
    while (std::find(used.begin(), used.end(), next_identifier) != used.end()) {
      ++next_identifier;
      if (next_identifier == 0U) {
        throw std::runtime_error("PSD alpha channel identifiers are exhausted");
      }
    }
    const auto result = next_identifier++;
    used.push_back(result);
    return result;
  };

  BigEndianWriter writer;
  const auto saved_count = static_cast<std::size_t>(std::count_if(
      channels.begin(), channels.end(), has_alpha_identifier));
  writer.write_u32(checked_u32(saved_count, "alpha identifier count"));
  for (const auto& channel : channels) {
    if (!has_alpha_identifier(channel)) {
      continue;
    }
    writer.write_u32(channel.photoshop_identifier.has_value() ? *channel.photoshop_identifier
                                                               : allocate_identifier());
  }
  return writer.bytes();
}

std::vector<std::uint8_t> generated_display_info_record(const DocumentChannelDisplayInfo& info) {
  BigEndianWriter writer;
  writer.write_u16(0);  // RGB color space.
  writer.write_u16(static_cast<std::uint16_t>(info.color.red) * 257U);
  writer.write_u16(static_cast<std::uint16_t>(info.color.green) * 257U);
  writer.write_u16(static_cast<std::uint16_t>(info.color.blue) * 257U);
  writer.write_u16(0);
  writer.write_u16(static_cast<std::uint16_t>(std::lround(std::clamp(info.opacity, 0.0F, 1.0F) * 100.0F)));
  writer.write_u8(info.color_indicates == DocumentChannelColorIndicates::SpotColor       ? 2U
                  : info.color_indicates == DocumentChannelColorIndicates::SelectedAreas ? 0U
                                                                                           : 1U);
  return writer.bytes();
}

std::vector<std::uint8_t> display_info_resource(std::span<const CompositeChannelInfo> channels,
                                                bool floating_point_resource) {
  BigEndianWriter writer;
  if (floating_point_resource) {
    writer.write_u32(1);
  }
  for (const auto& channel : channels) {
    if ((!floating_point_resource && channel.raw_display_info.size() == 14U)) {
      writer.write_bytes(channel.raw_display_info);
      continue;
    }
    if (channel.raw_display_info.size() >= 13U) {
      writer.write_bytes(channel.raw_display_info.first(13U));
    } else {
      writer.write_bytes(generated_display_info_record(channel.display_info));
    }
    if (!floating_point_resource) {
      writer.write_u8(0);
    }
  }
  return writer.bytes();
}

std::vector<std::uint8_t> image_resources_for_document(const Document& document,
                                                       std::span<const CompositeChannelInfo> channels) {
  auto resources = document.metadata().raw_psd_image_resources;
  auto parsed = read_image_resources(resources);
  if (!parsed.has_value()) {
    parsed = std::vector<ImageResource>{};
  }
  if (const auto color_mode = document.metadata().values.find("psd.color_mode");
      color_mode != document.metadata().values.end() && color_mode->second != "RGB") {
    remove_image_resource(*parsed, kImageResourceIccProfile);
  }

  const auto had_grid_guides_resource = std::any_of(parsed->begin(), parsed->end(), [](const ImageResource& resource) {
    return resource.id == kImageResourceGridAndGuidesInfo;
  });
  const auto has_non_default_grid_guides =
      !document.guides().empty() ||
      sanitized_grid_cycle_32(document.grid_settings().horizontal_cycle_32) != kDefaultGridCycle32 ||
      sanitized_grid_cycle_32(document.grid_settings().vertical_cycle_32) != kDefaultGridCycle32;

  upsert_image_resource(*parsed, kImageResourceResolutionInfo, resolution_resource_for_document(document));
  if (had_grid_guides_resource || has_non_default_grid_guides) {
    upsert_image_resource(*parsed, kImageResourceGridAndGuidesInfo, grid_guides_resource_for_document(document));
  }
  if (!document.color_state().embedded_icc_profile.empty()) {
    upsert_image_resource(*parsed, kImageResourceIccProfile, document.color_state().embedded_icc_profile);
  }
  if (!channels.empty()) {
    upsert_image_resource(*parsed, kImageResourceAlphaChannelNames, alpha_channel_names_resource(channels));
    upsert_image_resource(*parsed, kImageResourceUnicodeAlphaChannelNames,
                          unicode_alpha_channel_names_resource(channels));
    upsert_image_resource(*parsed, kImageResourceAlphaIdentifiers, alpha_identifiers_resource(channels));
    upsert_image_resource(*parsed, kImageResourceDisplayInfo, display_info_resource(channels, false));
    upsert_image_resource(*parsed, kImageResourceDisplayInfoFloat, display_info_resource(channels, true));
  } else {
    remove_image_resource(*parsed, kImageResourceAlphaChannelNames);
    remove_image_resource(*parsed, kImageResourceUnicodeAlphaChannelNames);
    remove_image_resource(*parsed, kImageResourceAlphaIdentifiers);
    remove_image_resource(*parsed, kImageResourceDisplayInfo);
    remove_image_resource(*parsed, kImageResourceDisplayInfoFloat);
  }
  const auto& palette_editing = document.palette_editing();
  const std::vector<RgbColor>* palette_colors = nullptr;
  if (palette_editing.has_value() && !palette_editing->palette.colors.empty() &&
      palette_editing->palette.colors.size() <= 256) {
    palette_colors = &palette_editing->palette.colors;
  } else if (document.indexed_palette().has_value() && !document.indexed_palette()->colors.empty() &&
             document.indexed_palette()->colors.size() <= 256) {
    // A palette attached without the editing mode (imports, RGB round trips)
    // still travels with the file.
    palette_colors = &document.indexed_palette()->colors;
  }
  if (palette_colors != nullptr) {
    upsert_image_resource(*parsed, kImageResourcePatchyPalette,
                          patchy_palette_resource(*palette_colors, palette_editing.has_value(),
                                                  palette_editing.has_value() ? palette_editing->alpha_threshold
                                                                              : std::uint8_t{128}));
  } else {
    remove_image_resource(*parsed, kImageResourcePatchyPalette);
  }
  return write_image_resources(*parsed);
}

std::optional<std::array<char, 4>> block_key_from_string(std::string_view key) {
  if (key.size() != 4U) {
    return std::nullopt;
  }
  return std::array<char, 4>{key[0], key[1], key[2], key[3]};
}

// force_wide carries a preserved block's original 8B64 form; PSD saves always downgrade
// to the narrow form (PSB-only widths cannot appear in a version-1 file).
void write_additional_layer_block(BigEndianWriter& writer, const std::array<char, 4>& key,
                                  std::span<const std::uint8_t> payload, bool large_document,
                                  bool force_wide = false) {
  const bool wide_length =
      large_document && (force_wide || tagged_block_length_is_u64(std::string_view(key.data(), key.size())));
  write_signature(writer, wide_length ? std::array<char, 4>{'8', 'B', '6', '4'}
                                      : std::array<char, 4>{'8', 'B', 'I', 'M'});
  write_signature(writer, key);
  if (wide_length) {
    writer.write_u64(payload.size());
  } else {
    writer.write_u32(checked_u32(payload.size(), "additional layer block length"));
  }
  writer.write_bytes(payload);
}

// write_f64 moved to psd_descriptor.{hpp,cpp} alongside read_f64.

std::optional<std::string_view> layer_metadata_value(const Layer& layer, const char* key) {
  const auto found = layer.metadata().find(key);
  if (found == layer.metadata().end()) {
    return std::nullopt;
  }
  return std::string_view(found->second);
}

int parse_int_or(std::string_view text, int fallback) {
  const std::string copy(text);
  char* end = nullptr;
  const auto parsed = std::strtol(copy.c_str(), &end, 10);
  return end == copy.c_str() ? fallback : static_cast<int>(parsed);
}

std::optional<double> parse_double(std::string_view text) {
  const std::string copy(text);
  char* end = nullptr;
  const auto parsed = std::strtod(copy.c_str(), &end);
  if (end == copy.c_str() || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::vector<std::string_view> split_tab_fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1U;
  }
  return fields;
}

// Whether serialized runs carry layout data only a Photoshop import can produce (fixed leading
// or tracking; Patchy's own text UI cannot author either). Reopening a converted layer must
// restore the Photoshop layout marker even though its regenerated TySh is Patchy-signed, while
// reopened native text (auto-leading only) keeps native layout.
bool serialized_runs_have_photoshop_leading_signals(std::string_view runs_text) {
  std::size_t line_start = 0;
  while (line_start < runs_text.size()) {
    const auto line_end = runs_text.find('\n', line_start);
    const auto line = line_end == std::string_view::npos
                          ? runs_text.substr(line_start)
                          : runs_text.substr(line_start, line_end - line_start);
    line_start = line_end == std::string_view::npos ? runs_text.size() : line_end + 1U;
    const auto fields = split_tab_fields(line);
    if (fields.size() >= 8U && fields[7] != "auto") {
      if (const auto leading = parse_double(fields[7]);
          leading.has_value() && std::isfinite(*leading) && *leading > 0.0) {
        return true;
      }
    }
    if (fields.size() >= 9U) {
      if (const auto tracking = parse_double(fields[8]);
          tracking.has_value() && std::isfinite(*tracking) && std::abs(*tracking) > 0.0001) {
        return true;
      }
    }
    for (std::size_t scale_field = 9; scale_field <= 10 && scale_field < fields.size(); ++scale_field) {
      if (const auto scale = parse_double(fields[scale_field]);
          scale.has_value() && std::isfinite(*scale) && std::abs(*scale - 1.0) > 0.0001) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string_view> split_space_fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start < line.size()) {
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])) != 0) {
      ++start;
    }
    const auto end = line.find_first_of(" \t\r\n", start);
    if (end == std::string_view::npos) {
      if (start < line.size()) {
        fields.push_back(line.substr(start));
      }
      break;
    }
    if (end > start) {
      fields.push_back(line.substr(start, end - start));
    }
    start = end + 1U;
  }
  return fields;
}

template <std::size_t Size>
std::string serialize_double_array(const std::array<double, Size>& values) {
  std::ostringstream stream;
  stream << std::setprecision(17);
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      stream << ' ';
    }
    stream << values[index];
  }
  return stream.str();
}

std::string serialize_text_bounds(const PsdTextBoundsD& bounds) {
  return serialize_double_array(std::array<double, 4>{bounds.left, bounds.top, bounds.right, bounds.bottom});
}

std::string serialize_int_array(const std::array<int, 4>& values) {
  std::string result;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      result.push_back(' ');
    }
    result += std::to_string(values[index]);
  }
  return result;
}

std::optional<std::array<double, 6>> parse_double_array6(std::string_view text) {
  const auto fields = split_space_fields(text);
  if (fields.size() < 6U) {
    return std::nullopt;
  }
  std::array<double, 6> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto parsed = parse_double(fields[index]);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    values[index] = *parsed;
  }
  return values;
}

std::optional<PsdTextBoundsD> parse_text_bounds_metadata(std::string_view text) {
  const auto fields = split_space_fields(text);
  if (fields.size() < 4U) {
    return std::nullopt;
  }
  std::array<double, 4> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto parsed = parse_double(fields[index]);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    values[index] = *parsed;
  }
  return PsdTextBoundsD{values[0], values[1], values[2], values[3]};
}

std::optional<std::array<int, 4>> parse_int_array4(std::string_view text) {
  const auto fields = split_space_fields(text);
  if (fields.size() < 4U) {
    return std::nullopt;
  }
  std::array<int, 4> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    values[index] = parse_int_or(fields[index], 0);
  }
  return values;
}

PsdTextStyleRun fallback_text_run_from_metadata(const Layer& layer) {
  PsdTextStyleRun fallback;
  if (const auto family = layer_metadata_value(layer, kLayerMetadataTextFont);
      family.has_value() && !family->empty() && ascii_lower_copy(std::string(*family)) != "psd text") {
    fallback.family = std::string(*family);
  }
  if (const auto size = layer_metadata_value(layer, kLayerMetadataTextSize); size.has_value()) {
    fallback.size = std::clamp(static_cast<double>(parse_int_or(*size, static_cast<int>(std::lround(fallback.size)))),
                               1.0, static_cast<double>(kMaxTextSizePixels));
  }
  if (const auto color = layer_metadata_value(layer, kLayerMetadataTextColor); color.has_value()) {
    fallback.color = rgb_color_from_hex(*color).value_or(fallback.color);
  }
  fallback.bold = layer_metadata_value(layer, kLayerMetadataTextBold).value_or(std::string_view{}) == "true";
  fallback.italic = layer_metadata_value(layer, kLayerMetadataTextItalic).value_or(std::string_view{}) == "true";
  return fallback;
}

std::vector<PsdTextStyleRun> parse_patchy_text_runs_metadata(std::string_view runs_text,
                                                             std::string_view plain_text,
                                                             const PsdTextStyleRun& fallback) {
  std::vector<PsdTextStyleRun> runs;
  const auto text_length = static_cast<int>(utf8_to_utf16(plain_text).size());
  std::size_t line_start = 0;
  while (line_start < runs_text.size()) {
    const auto line_end = runs_text.find('\n', line_start);
    auto line = line_end == std::string_view::npos ? runs_text.substr(line_start)
                                                   : runs_text.substr(line_start, line_end - line_start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    line_start = line_end == std::string_view::npos ? runs_text.size() : line_end + 1U;
    if (line.empty() || line == "v1" || line == "v2" || line == "v3") {
      continue;
    }

    const auto fields = split_tab_fields(line);
    if (fields.size() < 7U) {
      continue;
    }
    PsdTextStyleRun run = fallback;
    run.start = std::clamp(parse_int_or(fields[0], 0), 0, std::max(0, text_length));
    run.length = std::max(0, parse_int_or(fields[1], 0));
    run.size = std::clamp(parse_double(fields[2]).value_or(fallback.size), 1.0,
                          static_cast<double>(kMaxTextSizePixels));
    run.bold = parse_int_or(fields[3], fallback.bold ? 1 : 0) != 0;
    run.italic = parse_int_or(fields[4], fallback.italic ? 1 : 0) != 0;
    if (auto color = rgb_color_from_hex(fields[5]); color.has_value()) {
      run.color = *color;
    }
    run.family = percent_decode(fields[6]);
    if (run.family.empty() || ascii_lower_copy(run.family) == "psd text") {
      run.family = fallback.family;
    }
    if (fields.size() >= 8U) {
      if (fields[7] == "auto") {
        run.auto_leading = true;
        run.leading.reset();
      } else if (const auto leading = parse_double(fields[7]); leading.has_value() && std::isfinite(*leading) &&
                                                               *leading > 0.0) {
        run.leading = *leading;
      }
    }
    if (fields.size() >= 9U) {
      if (const auto tracking = parse_double(fields[8]);
          tracking.has_value() && std::isfinite(*tracking) && std::abs(*tracking) < 10000.0) {
        run.tracking = *tracking;
      }
    }
    if (fields.size() >= 10U) {
      if (const auto scale = parse_double(fields[9]);
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        run.horizontal_scale = *scale;
      }
    }
    if (fields.size() >= 11U) {
      if (const auto scale = parse_double(fields[10]);
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        run.vertical_scale = *scale;
      }
    }
    if (run.length <= 0 || run.start >= text_length) {
      continue;
    }
    run.length = std::min(run.length, text_length - run.start);
    runs.push_back(std::move(run));
  }

  std::sort(runs.begin(), runs.end(), [](const PsdTextStyleRun& lhs, const PsdTextStyleRun& rhs) {
    return lhs.start < rhs.start;
  });
  if (runs.empty() && text_length > 0) {
    auto run = fallback;
    run.start = 0;
    run.length = text_length;
    runs.push_back(std::move(run));
  } else if (!runs.empty()) {
    const auto covered = runs.back().start + runs.back().length;
    if (covered < text_length) {
      auto run = fallback;
      run.start = covered;
      run.length = text_length - covered;
      runs.push_back(std::move(run));
    }
  }
  return runs;
}

std::vector<PsdTextParagraphRun> paragraph_runs_from_text_line_breaks(std::string_view text, int justification) {
  std::vector<PsdTextParagraphRun> runs;
  const auto text_length = static_cast<int>(utf8_to_utf16(text).size());
  if (text_length <= 0) {
    runs.push_back(PsdTextParagraphRun{0, 1, justification});
    return runs;
  }

  int start_units = 0;
  std::size_t segment_start = 0;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '\n') {
      continue;
    }
    const auto segment = text.substr(segment_start, index + 1U - segment_start);
    const auto length = static_cast<int>(utf8_to_utf16(segment).size());
    if (length > 0) {
      runs.push_back(PsdTextParagraphRun{start_units, length, justification});
      start_units += length;
    }
    segment_start = index + 1U;
  }

  if (segment_start < text.size()) {
    const auto length = static_cast<int>(utf8_to_utf16(text.substr(segment_start)).size());
    if (length > 0) {
      runs.push_back(PsdTextParagraphRun{start_units, length, justification});
    }
  }
  if (runs.empty()) {
    runs.push_back(PsdTextParagraphRun{0, text_length, justification});
  }
  return runs;
}

std::vector<PsdTextStyleRun> split_single_style_run_on_line_breaks(std::vector<PsdTextStyleRun> runs,
                                                                   std::string_view text) {
  if (text.find('\n') == std::string_view::npos || runs.size() != 1U) {
    return runs;
  }
  const auto text_length = static_cast<int>(utf8_to_utf16(text).size());
  if (text_length <= 0 || runs.front().start != 0 || runs.front().length < text_length) {
    return runs;
  }

  const auto line_runs = paragraph_runs_from_text_line_breaks(text, 0);
  if (line_runs.size() <= 1U) {
    return runs;
  }

  std::vector<PsdTextStyleRun> split_runs;
  split_runs.reserve(line_runs.size());
  for (const auto& line : line_runs) {
    auto run = runs.front();
    run.start = line.start;
    run.length = line.length;
    split_runs.push_back(std::move(run));
  }
  return split_runs;
}

std::vector<PsdTextStyleRun> text_runs_for_layer(const Layer& layer, std::string_view text) {
  const auto fallback = fallback_text_run_from_metadata(layer);
  if (const auto runs = layer_metadata_value(layer, kLayerMetadataTextRuns); runs.has_value()) {
    return split_single_style_run_on_line_breaks(parse_patchy_text_runs_metadata(*runs, text, fallback), text);
  }
  return split_single_style_run_on_line_breaks(parse_patchy_text_runs_metadata({}, text, fallback), text);
}

std::vector<PsdTextParagraphRun> parse_patchy_paragraph_runs_metadata(std::string_view runs_text,
                                                                      std::string_view plain_text) {
  std::vector<PsdTextParagraphRun> runs;
  const auto text_length = static_cast<int>(utf8_to_utf16(plain_text).size());
  std::size_t line_start = 0;
  while (line_start < runs_text.size()) {
    const auto line_end = runs_text.find('\n', line_start);
    auto line = line_end == std::string_view::npos ? runs_text.substr(line_start)
                                                   : runs_text.substr(line_start, line_end - line_start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    line_start = line_end == std::string_view::npos ? runs_text.size() : line_end + 1U;
    if (line.empty() || line == "v1" || line == "v2" || line == "v3") {
      continue;
    }
    const auto fields = split_tab_fields(line);
    if (fields.size() < 3U) {
      continue;
    }
    PsdTextParagraphRun run;
    run.start = std::clamp(parse_int_or(fields[0], 0), 0, std::max(0, text_length));
    run.length = std::max(0, parse_int_or(fields[1], 0));
    run.justification = photoshop_justification_from_patchy_alignment(fields[2]);
    if (fields.size() >= 8U) {
      run.first_line_indent = parse_double(fields[3]).value_or(0.0);
      run.start_indent = parse_double(fields[4]).value_or(0.0);
      run.end_indent = parse_double(fields[5]).value_or(0.0);
      run.space_before = parse_double(fields[6]).value_or(0.0);
      run.space_after = parse_double(fields[7]).value_or(0.0);
    }
    if (fields.size() >= 9U) {
      if (const auto fraction = parse_double(fields[8]);
          fraction.has_value() && std::isfinite(*fraction) && *fraction > 0.01 && *fraction < 10.0) {
        run.auto_leading_fraction = *fraction;
      }
    }
    if (run.length <= 0 || run.start >= text_length) {
      continue;
    }
    run.length = std::min(run.length, text_length - run.start);
    runs.push_back(run);
  }
  std::sort(runs.begin(), runs.end(), [](const PsdTextParagraphRun& lhs, const PsdTextParagraphRun& rhs) {
    return lhs.start < rhs.start;
  });
  if (runs.empty()) {
    runs.push_back(PsdTextParagraphRun{0, std::max(1, text_length), 0});
  }
  return runs;
}

std::vector<PsdTextParagraphRun> paragraph_runs_for_layer(const Layer& layer, std::string_view text) {
  std::vector<PsdTextParagraphRun> parsed;
  if (const auto runs = layer_metadata_value(layer, kLayerMetadataTextParagraphRuns); runs.has_value()) {
    parsed = parse_patchy_paragraph_runs_metadata(*runs, text);
  } else {
    parsed = parse_patchy_paragraph_runs_metadata({}, text);
  }
  if (text.find('\n') != std::string_view::npos && parsed.size() == 1U) {
    const auto text_length = static_cast<int>(utf8_to_utf16(text).size());
    const auto& run = parsed.front();
    if (text_length > 0 && run.start == 0 && run.length >= text_length) {
      return paragraph_runs_from_text_line_breaks(text, run.justification);
    }
  }
  return parsed;
}

PsdTextStyleRun imported_text_fallback_run(const LayerRecord& record) {
  PsdTextStyleRun fallback;
  if (record.text_font.has_value() && !record.text_font->empty() &&
      ascii_lower_copy(*record.text_font) != "psd text") {
    fallback.family = *record.text_font;
  }
  fallback.size = std::clamp(static_cast<double>(record.text_size.value_or(static_cast<int>(std::lround(fallback.size)))),
                             1.0, static_cast<double>(kMaxTextSizePixels));
  fallback.color = record.text_color.value_or(fallback.color);
  fallback.bold = record.text_bold.value_or(false);
  fallback.italic = record.text_italic.value_or(false);
  return fallback;
}

PsdTextStyleRun imported_text_primary_run(const LayerRecord& record) {
  auto fallback = imported_text_fallback_run(record);
  if (!record.text.has_value()) {
    return fallback;
  }
  if (record.text_runs.has_value()) {
    auto runs = parse_patchy_text_runs_metadata(*record.text_runs, *record.text, fallback);
    if (!runs.empty()) {
      return runs.front();
    }
  }
  return fallback;
}

int imported_text_primary_justification(const LayerRecord& record) {
  if (!record.text.has_value() || !record.text_paragraph_runs.has_value()) {
    return 0;
  }
  const auto runs = parse_patchy_paragraph_runs_metadata(*record.text_paragraph_runs, *record.text);
  return runs.empty() ? 0 : runs.front().justification;
}

bool layer_style_has_regeneratable_outer_text_effect(const LayerStyle& style) noexcept {
  if (!style.effects_visible || style.empty()) {
    return false;
  }
  for (const auto& shadow : style.drop_shadows) {
    if (shadow.enabled && shadow.opacity > 0.0F &&
        (shadow.size >= 64.0F || (shadow.size >= 32.0F && shadow.distance >= 16.0F))) {
      return true;
    }
  }
  for (const auto& glow : style.outer_glows) {
    if (glow.enabled && glow.opacity > 0.0F && glow.size >= 64.0F) {
      return true;
    }
  }
  return false;
}

bool psd_text_preview_lacks_declared_fill_color(const PixelBuffer& pixels, RgbColor fill) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 4) {
    return false;
  }

  std::uint64_t visible = 0;
  std::uint64_t fill_like = 0;
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  for (std::size_t offset = 0; offset + 3U < pixels.data().size(); offset += channels) {
    if (pixels.data()[offset + 3U] <= 16U) {
      continue;
    }
    ++visible;
    const auto red_delta = std::abs(static_cast<int>(pixels.data()[offset]) - static_cast<int>(fill.red));
    const auto green_delta = std::abs(static_cast<int>(pixels.data()[offset + 1U]) - static_cast<int>(fill.green));
    const auto blue_delta = std::abs(static_cast<int>(pixels.data()[offset + 2U]) - static_cast<int>(fill.blue));
    if (std::max({red_delta, green_delta, blue_delta}) <= 18 && red_delta + green_delta + blue_delta <= 44) {
      ++fill_like;
    }
  }

  return visible >= 256U && fill_like * 8U < visible;
}

bool should_regenerate_imported_text_preview(const LayerRecord& record, const PixelBuffer& pixels) {
  if (!record.text.has_value() || !record.text_source_block.has_value() || !has_visible_alpha(pixels)) {
    return false;
  }
  // Warped imports keep Photoshop's raster: the GDI regeneration path renders
  // unwarped glyphs and would flatten the warp out of the preview.
  if (record.text_geometry.has_value() && record.text_geometry->warp.has_value()) {
    return false;
  }
  if (!layer_style_has_regeneratable_outer_text_effect(record.layer_style)) {
    return false;
  }
  if (record.text_geometry.has_value()) {
    return true;
  }
  const auto fill = imported_text_primary_run(record).color;
  return psd_text_preview_lacks_declared_fill_color(pixels, fill);
}

struct RectD {
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
};

std::optional<RectD> transformed_text_bounds(const PsdTextGeometry& geometry, const PsdTextBoundsD& bounds) {
  const auto map_point = [&geometry](double x, double y) {
    return std::array<double, 2>{geometry.transform[0] * x + geometry.transform[2] * y + geometry.transform[4],
                                 geometry.transform[1] * x + geometry.transform[3] * y + geometry.transform[5]};
  };
  const std::array<std::array<double, 2>, 4> points = {
      map_point(bounds.left, bounds.top),
      map_point(bounds.right, bounds.top),
      map_point(bounds.right, bounds.bottom),
      map_point(bounds.left, bounds.bottom),
  };
  auto min_x = points.front()[0];
  auto max_x = points.front()[0];
  auto min_y = points.front()[1];
  auto max_y = points.front()[1];
  for (const auto& point : points) {
    min_x = std::min(min_x, point[0]);
    max_x = std::max(max_x, point[0]);
    min_y = std::min(min_y, point[1]);
    max_y = std::max(max_y, point[1]);
  }
  if (!std::isfinite(min_x) || !std::isfinite(max_x) || !std::isfinite(min_y) || !std::isfinite(max_y) ||
      max_x <= min_x || max_y <= min_y) {
    return std::nullopt;
  }
  return RectD{min_x, min_y, max_x, max_y};
}

// Vertical scale of the TySh transform: Photoshop's engine font size maps to rendered pixels
// through the transform's y column, so this (not an x/y average) scales font sizes and leading.
double imported_text_transform_scale(const LayerRecord& record) noexcept {
  if (!record.text_geometry.has_value()) {
    return 1.0;
  }
  const auto& transform = record.text_geometry->transform;
  const auto scale = std::hypot(transform[2], transform[3]);
  return std::isfinite(scale) && scale > 0.01 ? scale : 1.0;
}

Rect imported_text_draw_rect(const LayerRecord& record, int width, int height) {
  if (record.text_geometry.has_value()) {
    const auto& geometry = *record.text_geometry;
    const auto source_bounds = record.text_box.has_value() ? geometry.box_bounds : geometry.bounding_box;
    if (const auto transformed = transformed_text_bounds(geometry, source_bounds); transformed.has_value()) {
      Rect rect{static_cast<std::int32_t>(std::floor(transformed->left - record.bounds.x)),
                static_cast<std::int32_t>(std::floor(transformed->top - record.bounds.y)),
                static_cast<std::int32_t>(std::ceil(transformed->right - transformed->left)),
                static_cast<std::int32_t>(std::ceil(transformed->bottom - transformed->top))};
      if (rect.width > 0 && rect.height > 0 && rect.x < width && rect.y < height &&
          rect.x + rect.width > 0 && rect.y + rect.height > 0) {
        return rect;
      }
    }
  }

  if (record.text_box.has_value()) {
    const auto box_width = std::clamp(record.text_box->width, 1, std::max(1, width));
    const auto box_height = std::clamp(record.text_box->height, 1, std::max(1, height));
    return Rect{(width - box_width) / 2, (height - box_height) / 2, box_width, box_height};
  }
  return Rect{0, 0, std::max(1, width), std::max(1, height)};
}

std::wstring windows_text_line_breaks(std::wstring text) {
  std::wstring normalized;
  normalized.reserve(text.size() + 8U);
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == L'\n' && (index == 0U || text[index - 1U] != L'\r')) {
      normalized.push_back(L'\r');
    }
    normalized.push_back(text[index]);
  }
  return normalized;
}

std::optional<PixelBuffer> render_regenerated_imported_text_pixels(const LayerRecord& record,
                                                                   std::int32_t width,
                                                                   std::int32_t height) {
  if (!record.text.has_value() || record.text->empty() || width <= 0 || height <= 0) {
    return std::nullopt;
  }

  const auto run = imported_text_primary_run(record);
  const auto font_size =
      std::clamp(static_cast<int>(std::lround(static_cast<double>(run.size) * imported_text_transform_scale(record))),
                 1, kMaxTextSizePixels);
  const auto draw_rect = imported_text_draw_rect(record, width, height);
  const auto boxed = record.text_box.has_value();
  const auto justification = imported_text_primary_justification(record);

#ifdef _WIN32
  const auto wide_text = windows_text_line_breaks(wide_from_utf8(*record.text));
  if (wide_text.empty()) {
    return std::nullopt;
  }

  HDC memory_dc = CreateCompatibleDC(nullptr);
  if (memory_dc == nullptr) {
    return std::nullopt;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = width;
  bitmap_info.bmiHeader.biHeight = -height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* bitmap_bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(memory_dc, &bitmap_info, DIB_RGB_COLORS, &bitmap_bits, nullptr, 0);
  if (bitmap == nullptr || bitmap_bits == nullptr) {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    DeleteDC(memory_dc);
    return std::nullopt;
  }

  const auto wide_family =
      wide_from_utf8(run.family.empty() ? std::string_view("Arial") : std::string_view(run.family));
  LOGFONTW log_font{};
  log_font.lfHeight = -std::max(1, font_size);
  log_font.lfWeight = run.bold ? FW_BOLD : FW_NORMAL;
  log_font.lfItalic = run.italic ? TRUE : FALSE;
  log_font.lfCharSet = DEFAULT_CHARSET;
  log_font.lfQuality = record.text_anti_alias.value_or(4) <= 0 ? NONANTIALIASED_QUALITY : ANTIALIASED_QUALITY;
  const auto family_length = std::min<std::size_t>(wide_family.size(), LF_FACESIZE - 1U);
  for (std::size_t index = 0; index < family_length; ++index) {
    log_font.lfFaceName[index] = wide_family[index];
  }
  if (family_length == 0U) {
    const std::wstring fallback_family = L"Arial";
    for (std::size_t index = 0; index < fallback_family.size() && index < LF_FACESIZE - 1U; ++index) {
      log_font.lfFaceName[index] = fallback_family[index];
    }
  }

  HFONT font = CreateFontIndirectW(&log_font);
  if (font == nullptr) {
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    return std::nullopt;
  }

  const auto old_bitmap = SelectObject(memory_dc, bitmap);
  const auto old_font = SelectObject(memory_dc, font);
  const RECT full_rect{0, 0, width, height};
  HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(memory_dc, &full_rect, black);
  DeleteObject(black);
  SetBkMode(memory_dc, TRANSPARENT);
  SetTextColor(memory_dc, RGB(255, 255, 255));

  RECT text_rect{draw_rect.x, draw_rect.y, draw_rect.x + draw_rect.width, draw_rect.y + draw_rect.height};
  UINT flags = DT_NOPREFIX;
  if (boxed) {
    flags |= DT_WORDBREAK | DT_EDITCONTROL;
  } else {
    flags |= DT_NOCLIP;
  }
  if (justification == 1) {
    flags |= DT_RIGHT;
  } else if (justification == 2) {
    flags |= DT_CENTER;
  } else {
    flags |= DT_LEFT;
  }
  DrawTextW(memory_dc, wide_text.data(), static_cast<int>(std::min<std::size_t>(wide_text.size(), INT_MAX)),
            &text_rect, flags);

  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  pixels.clear(0);
  const auto* source = static_cast<const std::uint8_t*>(bitmap_bits);
  bool has_alpha = false;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto source_offset =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
      auto alpha = std::max({source[source_offset], source[source_offset + 1U], source[source_offset + 2U]});
      if (record.text_anti_alias.value_or(4) <= 0) {
        alpha = alpha >= 128U ? 255U : 0U;
      }
      if (alpha == 0U) {
        continue;
      }
      auto* target = pixels.pixel(x, y);
      target[0] = run.color.red;
      target[1] = run.color.green;
      target[2] = run.color.blue;
      target[3] = alpha;
      has_alpha = true;
    }
  }

  SelectObject(memory_dc, old_font);
  SelectObject(memory_dc, old_bitmap);
  DeleteObject(font);
  DeleteObject(bitmap);
  DeleteDC(memory_dc);
  return has_alpha ? std::optional<PixelBuffer>(std::move(pixels)) : std::nullopt;
#else
  auto pixels = render_placeholder_text(*record.text, width, height);
  const auto fill = run.color;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      if (pixel[3] == 0U) {
        continue;
      }
      pixel[0] = fill.red;
      pixel[1] = fill.green;
      pixel[2] = fill.blue;
    }
  }
  return pixels;
#endif
}

std::string photoshop_engine_text(std::string_view text) {
  std::string converted(text);
  for (auto& ch : converted) {
    if (ch == '\n') {
      ch = '\r';
    }
  }
  if (converted.empty() || (converted.back() != '\r' && converted.back() != '\n')) {
    converted.push_back('\r');
  }
  return converted;
}

std::string engine_escaped_utf16_string(std::string_view text) {
  std::string escaped = "(";
  const auto append_byte = [&escaped](std::uint8_t byte) {
    if (byte == '(' || byte == ')' || byte == '\\') {
      escaped.push_back('\\');
      escaped.push_back(static_cast<char>(byte));
    } else {
      escaped.push_back(static_cast<char>(byte));
    }
  };

  append_byte(0xFEU);
  append_byte(0xFFU);
  for (const auto unit : utf8_to_utf16(text)) {
    append_byte(static_cast<std::uint8_t>((unit >> 8U) & 0xFFU));
    append_byte(static_cast<std::uint8_t>(unit & 0xFFU));
  }
  escaped.push_back(')');
  return escaped;
}

std::vector<std::uint8_t> utf16be_text_bytes(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  for (const auto unit : utf8_to_utf16(text)) {
    bytes.push_back(static_cast<std::uint8_t>((unit >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>(unit & 0xFFU));
  }
  return bytes;
}

bool replace_all_bytes(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> old_value,
                       std::span<const std::uint8_t> new_value) {
  if (old_value.empty() || old_value.size() != new_value.size()) {
    return false;
  }
  bool replaced = false;
  auto search_start = bytes.begin();
  while (search_start != bytes.end()) {
    const auto found = std::search(search_start, bytes.end(), old_value.begin(), old_value.end());
    if (found == bytes.end()) {
      break;
    }
    std::copy(new_value.begin(), new_value.end(), found);
    search_start = found + static_cast<std::ptrdiff_t>(new_value.size());
    replaced = true;
  }
  return replaced;
}

std::optional<std::vector<std::uint8_t>> photoshop_type_tool_payload_from_template(const Layer& layer,
                                                                                   std::string_view new_text) {
  const auto source_block = layer_metadata_value(layer, kLayerMetadataTextSourceBlock);
  if (!source_block.has_value() || (*source_block != "TySh" && *source_block != "tySh")) {
    return std::nullopt;
  }
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key != "TySh" && block.key != "tySh") {
      continue;
    }
    const auto old_text = extract_engine_data_text(block.payload);
    if (!old_text.has_value()) {
      continue;
    }
    const auto old_engine_text = photoshop_engine_text(*old_text);
    const auto new_engine_text = photoshop_engine_text(new_text);
    const auto old_units = utf8_to_utf16(old_engine_text);
    const auto new_units = utf8_to_utf16(new_engine_text);
    if (old_units.empty() || old_units.size() != new_units.size()) {
      continue;
    }
    auto payload = block.payload;
    const auto old_bytes = utf16be_text_bytes(old_engine_text);
    const auto new_bytes = utf16be_text_bytes(new_engine_text);
    if (replace_all_bytes(payload, old_bytes, new_bytes)) {
      return payload;
    }
  }
  return std::nullopt;
}

bool text_transform_overrides_psd_template(const Layer& layer) {
  const auto patchy_transform = layer_metadata_value(layer, kLayerMetadataTextTransform);
  if (!patchy_transform.has_value()) {
    return false;
  }
  const auto parsed_patchy = parse_double_array6(*patchy_transform);
  if (!parsed_patchy.has_value()) {
    return false;
  }
  const auto psd_transform = layer_metadata_value(layer, kLayerMetadataPsdTextTransform);
  if (!psd_transform.has_value()) {
    return true;
  }
  const auto parsed_psd = parse_double_array6(*psd_transform);
  if (!parsed_psd.has_value()) {
    return true;
  }
  for (std::size_t i = 0; i < parsed_patchy->size(); ++i) {
    if (std::abs((*parsed_patchy)[i] - (*parsed_psd)[i]) > 0.000001) {
      return true;
    }
  }
  return false;
}

bool layer_has_photoshop_text_source(const Layer& layer) {
  const auto source_block = layer_metadata_value(layer, kLayerMetadataTextSourceBlock);
  return source_block.has_value() && (*source_block == "TySh" || *source_block == "tySh");
}

bool should_preserve_imported_text_geometry(const Layer& layer) {
  const auto raster_status = layer_metadata_value(layer, kLayerMetadataTextRasterStatus).value_or(std::string_view{});
  if (raster_status == "patchy_raster" && layer_has_photoshop_text_source(layer)) {
    return false;
  }
  if (text_transform_overrides_psd_template(layer)) {
    return false;
  }
  if (layer_has_photoshop_text_source(layer)) {
    return true;
  }
  return raster_status != "patchy_raster" || !layer_metadata_value(layer, kLayerMetadataTextTransform).has_value();
}

double text_bounds_height(const PsdTextBoundsD& bounds) {
  return bounds.bottom - bounds.top;
}

bool finite_text_bounds(const PsdTextBoundsD& bounds) {
  return std::isfinite(bounds.left) && std::isfinite(bounds.top) && std::isfinite(bounds.right) &&
         std::isfinite(bounds.bottom);
}

void translate_text_bounds_local(PsdTextBoundsD& bounds, double dx, double dy) {
  bounds.left -= dx;
  bounds.right -= dx;
  bounds.top -= dy;
  bounds.bottom -= dy;
}

void translate_text_geometry_local(PsdTextGeometry& geometry, double dx, double dy) {
  geometry.transform[4] += geometry.transform[0] * dx + geometry.transform[2] * dy;
  geometry.transform[5] += geometry.transform[1] * dx + geometry.transform[3] * dy;
  translate_text_bounds_local(geometry.bounds, dx, dy);
  translate_text_bounds_local(geometry.bounding_box, dx, dy);
  translate_text_bounds_local(geometry.box_bounds, dx, dy);
}

double point_text_baseline_offset(const Layer& layer, const PsdTextGeometry& geometry) {
  if (finite_text_bounds(geometry.bounding_box) && geometry.bounding_box.bottom > 1.0 &&
      text_bounds_height(geometry.bounding_box) > 1.0) {
    return geometry.bounding_box.bottom;
  }
  if (finite_text_bounds(geometry.bounds) && geometry.bounds.bottom > 1.0 &&
      text_bounds_height(geometry.bounds) > 1.0) {
    return geometry.bounds.bottom;
  }
  if (const auto size = layer_metadata_value(layer, kLayerMetadataTextSize); size.has_value()) {
    return std::max(1.0, static_cast<double>(parse_int_or(*size, 1)) * 0.75);
  }
  return 1.0;
}

bool point_text_geometry_needs_baseline_anchor(const Layer& layer, const PsdTextGeometry& geometry) {
  if (layer_has_photoshop_text_source(layer) && should_preserve_imported_text_geometry(layer)) {
    return false;
  }
  if (!finite_text_bounds(geometry.bounding_box) || text_bounds_height(geometry.bounding_box) <= 1.0) {
    return false;
  }
  return geometry.bounding_box.top >= -0.5 && point_text_baseline_offset(layer, geometry) > 1.0;
}

#ifdef _WIN32
std::wstring wide_from_utf8(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const auto input_size = static_cast<int>(std::min<std::size_t>(text.size(), static_cast<std::size_t>(INT_MAX)));
  const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
  if (wide_size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, wide.data(), wide_size) !=
      wide_size) {
    return {};
  }
  return wide;
}

std::string utf8_from_wide(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  const auto input_size = static_cast<int>(std::min<std::size_t>(text.size(), static_cast<std::size_t>(INT_MAX)));
  const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, text.data(), input_size, nullptr, 0, nullptr, nullptr);
  if (utf8_size <= 0) {
    return {};
  }
  std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, text.data(), input_size, utf8.data(), utf8_size, nullptr, nullptr) !=
      utf8_size) {
    return {};
  }
  return utf8;
}

std::optional<std::wstring> directwrite_localized_string(IDWriteLocalizedStrings* strings) {
  if (strings == nullptr || strings->GetCount() == 0) {
    return std::nullopt;
  }
  UINT32 index = 0;
  BOOL exists = FALSE;
  if (FAILED(strings->FindLocaleName(L"en-us", &index, &exists)) || !exists) {
    index = 0;
  }
  UINT32 length = 0;
  if (FAILED(strings->GetStringLength(index, &length))) {
    return std::nullopt;
  }
  std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
  if (FAILED(strings->GetString(index, value.data(), length + 1U))) {
    return std::nullopt;
  }
  value.resize(length);
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> directwrite_font_info_string(IDWriteFont* font, DWRITE_INFORMATIONAL_STRING_ID id) {
  if (font == nullptr) {
    return std::nullopt;
  }
  Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> strings;
  BOOL exists = FALSE;
  if (FAILED(font->GetInformationalStrings(id, &strings, &exists)) || !exists || !strings) {
    return std::nullopt;
  }
  const auto value = directwrite_localized_string(strings.Get());
  if (!value.has_value()) {
    return std::nullopt;
  }
  auto utf8 = utf8_from_wide(*value);
  if (utf8.empty()) {
    return std::nullopt;
  }
  return utf8;
}

std::string photoshop_font_name_for_run(std::string_view family, bool bold, bool italic) {
  const auto fallback = family.empty() ? std::string("Arial") : std::string(family);
  const auto wide_family = wide_from_utf8(fallback);
  if (wide_family.empty()) {
    return fallback;
  }

  Microsoft::WRL::ComPtr<IDWriteFactory> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
    return fallback;
  }
  Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(&collection)) || !collection) {
    return fallback;
  }

  UINT32 family_index = 0;
  BOOL exists = FALSE;
  if (FAILED(collection->FindFamilyName(wide_family.c_str(), &family_index, &exists)) || !exists) {
    return fallback;
  }

  Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
  if (FAILED(collection->GetFontFamily(family_index, &font_family)) || !font_family) {
    return fallback;
  }
  Microsoft::WRL::ComPtr<IDWriteFont> font;
  if (FAILED(font_family->GetFirstMatchingFont(bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL,
                                               italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                               &font)) ||
      !font) {
    return fallback;
  }

  if (const auto postscript = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
      postscript.has_value()) {
    return *postscript;
  }
  if (const auto full_name = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_FULL_NAME);
      full_name.has_value()) {
    return *full_name;
  }
  return fallback;
}
#else
std::string photoshop_font_name_for_run(std::string_view family, bool /*bold*/, bool /*italic*/) {
  return family.empty() ? std::string("Arial") : std::string(family);
}
#endif

int font_index_for_run(std::vector<std::string>& fonts, std::string_view family, bool bold, bool italic) {
  const auto photoshop_name = photoshop_font_name_for_run(family, bold, italic);
  for (std::size_t index = 0; index < fonts.size(); ++index) {
    if (fonts[index] == photoshop_name) {
      return static_cast<int>(index);
    }
  }
  fonts.emplace_back(photoshop_name);
  return static_cast<int>(fonts.size() - 1U);
}

std::string engine_color_values(RgbColor color) {
  const auto component = [](std::uint8_t value) {
    return std::to_string(static_cast<double>(value) / 255.0);
  };
  return "1.0 " + component(color.red) + ' ' + component(color.green) + ' ' + component(color.blue);
}

std::string engine_color_object(RgbColor color) {
  return "<< /Type 1 /Values [ " + engine_color_values(color) + " ] >>";
}

std::string engine_adjustments_object() {
  return "<< /Axis [ 1.0 0.0 1.0 ] /XY [ 0.0 0.0 ] >>";
}

std::string engine_paragraph_properties(const PsdTextParagraphRun& run) {
  std::string properties = "<< /Justification ";
  properties += std::to_string(std::clamp(run.justification, 0, 3));
  properties += " /FirstLineIndent ";
  properties += serialize_paragraph_metric(run.first_line_indent);
  properties += " /StartIndent ";
  properties += serialize_paragraph_metric(run.start_indent);
  properties += " /EndIndent ";
  properties += serialize_paragraph_metric(run.end_indent);
  properties += " /SpaceBefore ";
  properties += serialize_paragraph_metric(run.space_before);
  properties += " /SpaceAfter ";
  properties += serialize_paragraph_metric(run.space_after);
  properties +=
      " /AutoHyphenate true /HyphenatedWordSize 6 /PreHyphen 2 /PostHyphen 2 /ConsecutiveHyphens 8"
      " /Zone 36.0 /WordSpacing [ 0.8 1.0 1.33 ] /LetterSpacing [ 0.0 0.0 0.0 ]"
      " /GlyphSpacing [ 1.0 1.0 1.0 ] /AutoLeading ";
  properties += serialize_paragraph_metric(std::isfinite(run.auto_leading_fraction) &&
                                                   run.auto_leading_fraction > 0.01 &&
                                                   run.auto_leading_fraction < 10.0
                                               ? run.auto_leading_fraction
                                               : 1.2);
  properties += " /LeadingType 0 /Hanging ";
  properties += run.first_line_indent < 0.0 && run.start_indent > 0.0 ? "true" : "false";
  properties += " /Burasagari false /KinsokuOrder 0 /EveryLineComposer false >>";
  return properties;
}

std::string engine_style_sheet_data(const PsdTextStyleRun& run, int font_index) {
  std::string style = "<< /Font ";
  style += std::to_string(std::max(0, font_index));
  const auto font_size = std::max(1.0, run.size);
  style += " /FontSize ";
  style += std::to_string(font_size);
  style += " /FauxBold ";
  style += run.bold ? "true" : "false";
  style += " /FauxItalic ";
  style += run.italic ? "true" : "false";
  // A fixed-leading run must export /AutoLeading false or Photoshop ignores the value and
  // re-derives auto leading. Auto (and unspecified) runs keep the historical auto shape.
  const bool fixed_leading = !run.auto_leading && run.leading.has_value() && std::isfinite(*run.leading) &&
                             *run.leading > 0.0;
  style += fixed_leading ? " /AutoLeading false /Leading " : " /AutoLeading true /Leading ";
  style += std::to_string(fixed_leading ? *run.leading : font_size * 1.2);
  if (std::isfinite(run.tracking) && std::abs(run.tracking) > 0.0001) {
    style += " /Tracking ";
    style += std::to_string(run.tracking);
  }
  if (std::isfinite(run.horizontal_scale) && std::abs(run.horizontal_scale - 1.0) > 0.0001) {
    style += " /HorizontalScale ";
    style += std::to_string(run.horizontal_scale);
  }
  if (std::isfinite(run.vertical_scale) && std::abs(run.vertical_scale - 1.0) > 0.0001) {
    style += " /VerticalScale ";
    style += std::to_string(run.vertical_scale);
  }
  style += " /AutoKerning true /Kerning 0 /FillColor ";
  style += engine_color_object(run.color);
  style += " >>";
  return style;
}

std::string engine_font_set(std::span<const std::string> fonts) {
  std::string font_set = "[ ";
  for (std::size_t index = 0; index < fonts.size(); ++index) {
    const auto& font = fonts[index];
    font_set += "<< /Name ";
    font_set += engine_escaped_utf16_string(font.empty() ? "Arial" : font);
    font_set += " /Script 0 /FontType ";
    font_set += index == 0U ? "0" : "1";
    font_set += " /Synthetic 0 >> ";
  }
  font_set += "]";
  return font_set;
}

std::string engine_text_resources(std::span<const std::string> fonts, const PsdTextStyleRun& normal_style,
                                  int normal_font_index, int normal_justification) {
  std::string resources = "<< /KinsokuSet [ ] /MojiKumiSet [ ] /TheNormalStyleSheet 0 /TheNormalParagraphSheet 0 ";
  resources += "/ParagraphSheetSet [ << /Name ";
  resources += engine_escaped_utf16_string("Normal RGB");
  resources += " /DefaultStyleSheet 0 /Properties ";
  PsdTextParagraphRun normal_paragraph;
  normal_paragraph.justification = normal_justification;
  resources += engine_paragraph_properties(normal_paragraph);
  resources += " >> ] /StyleSheetSet [ << /Name ";
  resources += engine_escaped_utf16_string("Normal RGB");
  resources += " /StyleSheetData ";
  resources += engine_style_sheet_data(normal_style, normal_font_index);
  resources += " >> ] /FontSet ";
  resources += engine_font_set(fonts);
  resources +=
      " /SuperscriptSize 0.583 /SuperscriptPosition 0.333 /SubscriptSize 0.583 /SubscriptPosition 0.333"
      " /SmallCapSize 0.7 >>";
  return resources;
}

std::string engine_grid_info() {
  return "/GridInfo << /GridIsOn false /ShowGrid false /GridSize 18.0 /GridLeading 22.0"
         " /GridColor << /Type 1 /Values [ 1.0 0.0 0.0 1.0 ] >>"
         " /GridLeadingFillColor << /Type 1 /Values [ 1.0 0.0 0.0 1.0 ] >>"
         " /AlignLineHeightToGridFlags false >>\n";
}

std::string engine_rendered_shape(bool boxed_text, const PsdTextBoundsD& box_bounds) {
  const auto shape_type = boxed_text ? 1 : 0;
  std::string rendered = "/Rendered << /Version 1 /Shapes << /WritingDirection 0 /Children [ << /ShapeType ";
  rendered += std::to_string(shape_type);
  rendered +=
      " /Procession 0 /Lines << /WritingDirection 0 /Children [ ] >> /Cookie << /Photoshop << /ShapeType ";
  rendered += std::to_string(shape_type);
  if (boxed_text) {
    rendered += " /BoxBounds [ ";
    rendered += std::to_string(box_bounds.left);
    rendered += ' ';
    rendered += std::to_string(box_bounds.top);
    rendered += ' ';
    rendered += std::to_string(box_bounds.right);
    rendered += ' ';
    rendered += std::to_string(box_bounds.bottom);
    rendered += " ]";
  } else {
    rendered += " /PointBase [ 0.0 0.0 ]";
  }
  rendered += " /Base << /ShapeType ";
  rendered += std::to_string(shape_type);
  rendered +=
      " /TransformPoint0 [ 1.0 0.0 ] /TransformPoint1 [ 0.0 1.0 ] /TransformPoint2 [ 0.0 0.0 ]"
      " >> >> >> >> ] >> >>\n";
  return rendered;
}

std::vector<std::uint8_t> engine_data_for_text(std::string_view text, std::span<const PsdTextStyleRun> runs,
                                               std::span<const PsdTextParagraphRun> paragraph_runs, bool boxed_text,
                                               const PsdTextBoundsD& box_bounds, int anti_alias) {
  const auto engine_text = photoshop_engine_text(text);
  const auto engine_units = static_cast<int>(utf8_to_utf16(engine_text).size());
  std::vector<std::string> fonts{"AdobeInvisFont"};
  std::vector<int> font_indices;
  font_indices.reserve(runs.size());
  for (const auto& run : runs) {
    font_indices.push_back(font_index_for_run(fonts, run.family, run.bold, run.italic));
  }
  if (fonts.size() == 1U) {
    fonts.push_back("Arial");
  }

  std::vector<int> run_lengths;
  int covered = 0;
  for (const auto& run : runs) {
    if (run.length <= 0) {
      continue;
    }
    run_lengths.push_back(run.length);
    covered += run.length;
  }
  if (run_lengths.empty()) {
    run_lengths.push_back(engine_units);
  } else if (covered < engine_units) {
    run_lengths.back() += engine_units - covered;
  }

  std::vector<PsdTextParagraphRun> engine_paragraph_runs;
  int paragraph_covered = 0;
  for (const auto& run : paragraph_runs) {
    if (run.length <= 0) {
      continue;
    }
    engine_paragraph_runs.push_back(run);
    paragraph_covered += run.length;
  }
  if (engine_paragraph_runs.empty()) {
    PsdTextParagraphRun run;
    run.length = engine_units;
    engine_paragraph_runs.push_back(run);
  } else if (paragraph_covered < engine_units) {
    engine_paragraph_runs.back().length += engine_units - paragraph_covered;
  }

  std::string engine = "<<\n/EngineDict <<\n/Editor << /Text ";
  engine += engine_escaped_utf16_string(engine_text);
  engine += " >>\n/ParagraphRun << /DefaultRunData << /ParagraphSheet << /DefaultStyleSheet 0 /Properties << >> >> ";
  engine += "/Adjustments ";
  engine += engine_adjustments_object();
  engine += " >> /RunArray [ ";
  for (const auto& run : engine_paragraph_runs) {
    engine += "<< /ParagraphSheet << /DefaultStyleSheet 0 /Properties ";
    engine += engine_paragraph_properties(run);
    engine += " >> /Adjustments ";
    engine += engine_adjustments_object();
    engine += " >> ";
  }
  engine += "] /RunLengthArray [ ";
  for (const auto& run : engine_paragraph_runs) {
    engine += std::to_string(run.length);
    engine += ' ';
  }
  engine += "] /IsJoinable 1 >>\n";
  engine += "/StyleRun << /DefaultRunData << /StyleSheet << /StyleSheetData << >> >> >> /RunArray [ ";
  for (std::size_t index = 0; index < std::max<std::size_t>(runs.size(), 1U); ++index) {
    PsdTextStyleRun run;
    if (!runs.empty()) {
      run = runs[index];
    }
    const auto font_index = font_indices.empty() ? 1 : font_indices[std::min(index, font_indices.size() - 1U)];
    engine += "<< /StyleSheet << /StyleSheetData ";
    engine += engine_style_sheet_data(run, font_index);
    engine += " >> >> ";
  }
  engine += "] /RunLengthArray [ ";
  for (const auto length : run_lengths) {
    engine += std::to_string(length);
    engine += ' ';
  }
  engine += "] /IsJoinable 2 >>\n";
  engine += engine_grid_info();
  engine += "/AntiAlias ";
  engine += std::to_string(std::clamp(anti_alias, 0, 16));
  engine += " /UseFractionalGlyphWidths true\n";
  engine += engine_rendered_shape(boxed_text, box_bounds);
  engine += ">>\n";
  const PsdTextStyleRun normal_style = runs.empty() ? PsdTextStyleRun{} : runs.front();
  const auto normal_font_index = font_indices.empty() ? 1 : font_indices.front();
  const auto normal_justification = engine_paragraph_runs.empty() ? 0 : engine_paragraph_runs.front().justification;
  const auto resources = engine_text_resources(fonts, normal_style, normal_font_index, normal_justification);
  engine += "/ResourceDict ";
  engine += resources;
  engine += "\n/DocumentResources ";
  engine += resources;
  engine += "\n>>";
  return std::vector<std::uint8_t>(engine.begin(), engine.end());
}

// write_descriptor_unicode_string / write_descriptor_id moved to psd_descriptor.{hpp,cpp}
// (identical byte behavior), where the generic write_descriptor also lives.

void write_descriptor_item_header(BigEndianWriter& writer, std::string_view key, const std::array<char, 4>& type) {
  write_descriptor_id(writer, key);
  write_signature(writer, type);
}

void write_descriptor_enum_item(BigEndianWriter& writer, std::string_view key, std::string_view enum_type,
                                std::string_view enum_value) {
  write_descriptor_item_header(writer, key, {'e', 'n', 'u', 'm'});
  write_descriptor_id(writer, enum_type);
  write_descriptor_id(writer, enum_value);
}

void write_descriptor_bool_item(BigEndianWriter& writer, std::string_view key, bool value) {
  write_descriptor_item_header(writer, key, {'b', 'o', 'o', 'l'});
  writer.write_u8(value ? 1U : 0U);
}

void write_descriptor_long_item(BigEndianWriter& writer, std::string_view key, std::int32_t value) {
  write_descriptor_item_header(writer, key, {'l', 'o', 'n', 'g'});
  writer.write_u32(static_cast<std::uint32_t>(value));
}

void write_descriptor_double_item(BigEndianWriter& writer, std::string_view key, double value) {
  write_descriptor_item_header(writer, key, {'d', 'o', 'u', 'b'});
  write_f64(writer, value);
}

void write_descriptor_unit_float_item(BigEndianWriter& writer, std::string_view key, const std::array<char, 4>& unit,
                                      double value) {
  write_descriptor_item_header(writer, key, {'U', 'n', 't', 'F'});
  write_signature(writer, unit);
  write_f64(writer, value);
}

void write_descriptor_unit_float_item(BigEndianWriter& writer, std::string_view key, double value) {
  write_descriptor_unit_float_item(writer, key, {'#', 'P', 'n', 't'}, value);
}

void write_descriptor_object_header(BigEndianWriter& writer, std::string_view name, std::string_view class_id,
                                    std::uint32_t item_count) {
  write_descriptor_unicode_string(writer, name);
  write_descriptor_id(writer, class_id);
  writer.write_u32(item_count);
}

void write_descriptor_raw_item(BigEndianWriter& writer, std::string_view key, std::span<const std::uint8_t> payload) {
  write_descriptor_item_header(writer, key, {'t', 'd', 't', 'a'});
  writer.write_u32(checked_u32(payload.size(), "descriptor raw data"));
  writer.write_bytes(payload);
}

void write_bounds_descriptor(BigEndianWriter& writer, double left, double top, double right, double bottom) {
  write_descriptor_unicode_string(writer, "");
  write_descriptor_id(writer, "bounds");
  writer.write_u32(4);
  write_descriptor_unit_float_item(writer, "Left", left);
  write_descriptor_unit_float_item(writer, "Top ", top);
  write_descriptor_unit_float_item(writer, "Rght", right);
  write_descriptor_unit_float_item(writer, "Btom", bottom);
}

void write_bounds_descriptor(BigEndianWriter& writer, const PsdTextBoundsD& bounds) {
  write_bounds_descriptor(writer, bounds.left, bounds.top, bounds.right, bounds.bottom);
}

void write_descriptor_object_item(BigEndianWriter& writer, std::string_view key, double left, double top,
                                  double right, double bottom) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_bounds_descriptor(writer, left, top, right, bottom);
}

void write_descriptor_object_item(BigEndianWriter& writer, std::string_view key, const PsdTextBoundsD& bounds) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_bounds_descriptor(writer, bounds);
}

void write_text_descriptor(BigEndianWriter& writer, std::string_view text, std::span<const std::uint8_t> engine_data,
                           const PsdTextGeometry& geometry) {
  write_descriptor_unicode_string(writer, "");
  write_descriptor_id(writer, "TxLr");
  writer.write_u32(8);
  write_descriptor_item_header(writer, "Txt ", {'T', 'E', 'X', 'T'});
  write_descriptor_unicode_string(writer, text);
  write_descriptor_enum_item(writer, "textGridding", "textGridding", "None");
  write_descriptor_enum_item(writer, "Ornt", "Ornt", "Hrzn");
  write_descriptor_enum_item(writer, "AntA", "Annt", "AnCr");
  write_descriptor_object_item(writer, "bounds", geometry.bounds);
  write_descriptor_object_item(writer, "boundingBox", geometry.bounding_box);
  write_descriptor_raw_item(writer, "EngineData", engine_data);
  write_descriptor_item_header(writer, "TextIndex", {'l', 'o', 'n', 'g'});
  writer.write_u32(static_cast<std::uint32_t>(std::max(0, geometry.text_index)));
}

void write_warp_descriptor(BigEndianWriter& writer, const TextWarp* warp) {
  const bool active = warp != nullptr && !warp->style.empty();
  write_descriptor_unicode_string(writer, "");
  write_descriptor_id(writer, "warp");
  writer.write_u32(5);
  write_descriptor_enum_item(writer, "warpStyle", "warpStyle", active ? warp->style : "warpNone");
  write_descriptor_item_header(writer, "warpValue", {'d', 'o', 'u', 'b'});
  write_f64(writer, active ? warp->value : 0.0);
  write_descriptor_item_header(writer, "warpPerspective", {'d', 'o', 'u', 'b'});
  write_f64(writer, active ? warp->perspective : 0.0);
  write_descriptor_item_header(writer, "warpPerspectiveOther", {'d', 'o', 'u', 'b'});
  write_f64(writer, active ? warp->perspective_other : 0.0);
  write_descriptor_enum_item(writer, "warpRotate", "Ornt",
                             active && warp->rotate == "Vrtc" ? "Vrtc" : "Hrzn");
}

void write_descriptor_text_item(BigEndianWriter& writer, std::string_view key, std::string_view text) {
  write_descriptor_item_header(writer, key, {'T', 'E', 'X', 'T'});
  write_descriptor_unicode_string(writer, text);
}

// Photoshop 2026's lfx2 parser resolves 'BlnM' enum values only through their
// full stringID names ("multiply", "screen", ...); 4-char codes like 'Mltp'
// are silently read as Normal (verified July 2026 by byte-patching a probe
// PSD). Photoshop itself serializes these enums as length-prefixed strings,
// so write exactly that form.
std::string_view blend_mode_descriptor_value(BlendMode mode) {
  switch (mode) {
    case BlendMode::Multiply:
      return "multiply";
    case BlendMode::Screen:
      return "screen";
    case BlendMode::Overlay:
      return "overlay";
    case BlendMode::Darken:
      return "darken";
    case BlendMode::Lighten:
      return "lighten";
    case BlendMode::ColorDodge:
      return "colorDodge";
    case BlendMode::ColorBurn:
      return "colorBurn";
    case BlendMode::HardLight:
      return "hardLight";
    case BlendMode::SoftLight:
      return "softLight";
    case BlendMode::Difference:
      return "difference";
    case BlendMode::LinearBurn:
      return "linearBurn";
    case BlendMode::PinLight:
      return "pinLight";
    case BlendMode::Saturation:
      return "saturation";
    case BlendMode::Luminosity:
      return "luminosity";
    case BlendMode::Exclusion:
      return "exclusion";
    case BlendMode::Hue:
      return "hue";
    case BlendMode::Color:
      return "color";
    case BlendMode::LinearDodge:
      return "linearDodge";
    // Photoshop's stringIDs for these two really are "blendSubtraction"/"blendDivide".
    case BlendMode::Subtract:
      return "blendSubtraction";
    case BlendMode::Divide:
      return "blendDivide";
    case BlendMode::PassThrough:
    case BlendMode::Normal:
      return "normal";
  }
  return "normal";
}

void write_blend_mode_descriptor_item(BigEndianWriter& writer, std::string_view key, BlendMode mode) {
  write_descriptor_enum_item(writer, key, "BlnM", blend_mode_descriptor_value(mode));
}

void write_rgb_color_descriptor(BigEndianWriter& writer, RgbColor color) {
  write_descriptor_object_header(writer, "", "RGBC", 3);
  write_descriptor_unit_float_item(writer, "Rd  ", {'#', 'P', 'r', 'c'}, color.red);
  write_descriptor_unit_float_item(writer, "Grn ", {'#', 'P', 'r', 'c'}, color.green);
  write_descriptor_unit_float_item(writer, "Bl  ", {'#', 'P', 'r', 'c'}, color.blue);
}

void write_rgb_color_descriptor_item(BigEndianWriter& writer, std::string_view key, RgbColor color) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_rgb_color_descriptor(writer, color);
}

void write_gradient_color_stop(BigEndianWriter& writer, const GradientColorStop& stop) {
  write_descriptor_object_header(writer, "", "Clrt", 4);
  write_descriptor_enum_item(writer, "Type", "Clry", "UsrS");
  write_descriptor_long_item(writer, "Lctn", static_cast<std::int32_t>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F)));
  write_descriptor_long_item(writer, "Mdpn", 50);
  write_rgb_color_descriptor_item(writer, "Clr ", stop.color);
}

void write_gradient_alpha_stop(BigEndianWriter& writer, const GradientAlphaStop& stop) {
  write_descriptor_object_header(writer, "", "TrnS", 3);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, std::clamp(stop.opacity, 0.0F, 1.0F) * 100.0);
  write_descriptor_long_item(writer, "Lctn", static_cast<std::int32_t>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F)));
  write_descriptor_long_item(writer, "Mdpn", 50);
}

std::string_view gradient_type_descriptor_value(LayerStyleGradientType type) {
  switch (type) {
    case LayerStyleGradientType::Radial:
      return "Rdl ";
    case LayerStyleGradientType::Angle:
      return "Angl";
    case LayerStyleGradientType::Reflected:
      return "Rflc";
    case LayerStyleGradientType::Diamond:
      return "Dmnd";
    case LayerStyleGradientType::Linear:
      return "Lnr ";
  }
  return "Lnr ";
}

void write_layer_style_gradient_descriptor(BigEndianWriter& writer, const LayerStyleGradient& gradient) {
  auto color_stops = gradient.color_stops;
  auto alpha_stops = gradient.alpha_stops;
  if (color_stops.empty()) {
    color_stops.push_back(GradientColorStop{0.0F, RgbColor{0, 0, 0}});
    color_stops.push_back(GradientColorStop{1.0F, RgbColor{255, 255, 255}});
  }
  if (alpha_stops.empty()) {
    alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
    alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  }
  std::sort(color_stops.begin(), color_stops.end(),
            [](const GradientColorStop& lhs, const GradientColorStop& rhs) { return lhs.location < rhs.location; });
  std::sort(alpha_stops.begin(), alpha_stops.end(),
            [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });

  write_descriptor_object_header(writer, "", "Grdn", 5);
  write_descriptor_text_item(writer, "Nm  ", "Custom");
  write_descriptor_enum_item(writer, "GrdF", "GrdF", "CstS");
  write_descriptor_double_item(writer, "Intr", 4096.0);

  write_descriptor_item_header(writer, "Clrs", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(color_stops.size(), "gradient color stops"));
  for (const auto& stop : color_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_gradient_color_stop(writer, stop);
  }

  write_descriptor_item_header(writer, "Trns", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(alpha_stops.size(), "gradient alpha stops"));
  for (const auto& stop : alpha_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_gradient_alpha_stop(writer, stop);
  }
}

void write_layer_style_gradient_descriptor_item(BigEndianWriter& writer, std::string_view key,
                                                const LayerStyleGradient& gradient) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_layer_style_gradient_descriptor(writer, gradient);
}

// Photoshop's FrFX gradient shape differs from the otherwise similar GrFl
// descriptor. In particular, RGB components are plain doubles, each color stop
// puts Clr before Type/Lctn/Mdpn, and the Grad object's unicode header name is
// "Gradient". Photoshop accepts lean layer-effects roots, but expects this
// native shape inside the FrFX object.
void write_stroke_rgb_color_descriptor(BigEndianWriter& writer, RgbColor color) {
  write_descriptor_object_header(writer, "", "RGBC", 3);
  write_descriptor_double_item(writer, "Rd  ", color.red);
  write_descriptor_double_item(writer, "Grn ", color.green);
  write_descriptor_double_item(writer, "Bl  ", color.blue);
}

void write_stroke_rgb_color_descriptor_item(BigEndianWriter& writer, std::string_view key, RgbColor color) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_stroke_rgb_color_descriptor(writer, color);
}

void write_stroke_gradient_color_stop(BigEndianWriter& writer, const GradientColorStop& stop) {
  write_descriptor_object_header(writer, "", "Clrt", 4);
  write_stroke_rgb_color_descriptor_item(writer, "Clr ", stop.color);
  write_descriptor_enum_item(writer, "Type", "Clry", "UsrS");
  write_descriptor_long_item(
      writer, "Lctn",
      static_cast<std::int32_t>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F)));
  write_descriptor_long_item(writer, "Mdpn", 50);
}

void write_stroke_gradient_descriptor(BigEndianWriter& writer, const LayerStyleGradient& gradient) {
  auto color_stops = gradient.color_stops;
  auto alpha_stops = gradient.alpha_stops;
  if (color_stops.empty()) {
    color_stops.push_back(GradientColorStop{0.0F, RgbColor{0, 0, 0}});
    color_stops.push_back(GradientColorStop{1.0F, RgbColor{255, 255, 255}});
  }
  if (alpha_stops.empty()) {
    alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
    alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  }
  std::sort(color_stops.begin(), color_stops.end(),
            [](const GradientColorStop& lhs, const GradientColorStop& rhs) { return lhs.location < rhs.location; });
  std::sort(alpha_stops.begin(), alpha_stops.end(),
            [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });

  write_descriptor_object_header(writer, "Gradient", "Grdn", 5);
  write_descriptor_text_item(writer, "Nm  ", "Custom");
  write_descriptor_enum_item(writer, "GrdF", "GrdF", "CstS");
  write_descriptor_double_item(writer, "Intr", 4096.0);

  write_descriptor_item_header(writer, "Clrs", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(color_stops.size(), "gradient color stops"));
  for (const auto& stop : color_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_stroke_gradient_color_stop(writer, stop);
  }

  write_descriptor_item_header(writer, "Trns", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(alpha_stops.size(), "gradient alpha stops"));
  for (const auto& stop : alpha_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_gradient_alpha_stop(writer, stop);
  }
}

void write_stroke_gradient_descriptor_item(BigEndianWriter& writer, std::string_view key,
                                           const LayerStyleGradient& gradient) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_stroke_gradient_descriptor(writer, gradient);
}

std::string_view stroke_position_descriptor_value(LayerStrokePosition position) {
  switch (position) {
    case LayerStrokePosition::Inside:
      return "InsF";
    case LayerStrokePosition::Center:
      return "CtrF";
    case LayerStrokePosition::Outside:
      return "OutF";
  }
  return "OutF";
}

std::string_view inner_glow_source_descriptor_value(LayerInnerGlowSource source) {
  return source == LayerInnerGlowSource::Center ? "SrcC" : "SrcE";
}

void write_drop_shadow_descriptor(BigEndianWriter& writer, const LayerDropShadow& shadow) {
  write_descriptor_object_header(writer, "", "DrSh", 12);
  write_descriptor_bool_item(writer, "enab", shadow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", shadow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", shadow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, shadow.opacity * 100.0);
  // Claiming "use global light" would make Photoshop ignore lagl and swing the shadow to the
  // document's global angle; Patchy's angles are per-effect, so always declare them local.
  write_descriptor_bool_item(writer, "uglg", shadow.use_global_light);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, shadow.angle_degrees);
  write_descriptor_unit_float_item(writer, "Dstn", {'#', 'P', 'x', 'l'}, shadow.distance);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, shadow.spread);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, shadow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_bool_item(writer, "layerConceals", true);
}

void write_inner_shadow_descriptor(BigEndianWriter& writer, const LayerInnerShadow& shadow) {
  write_descriptor_object_header(writer, "", "IrSh", 11);
  write_descriptor_bool_item(writer, "enab", shadow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", shadow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", shadow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, shadow.opacity * 100.0);
  write_descriptor_bool_item(writer, "uglg", shadow.use_global_light);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, shadow.angle_degrees);
  write_descriptor_unit_float_item(writer, "Dstn", {'#', 'P', 'x', 'l'}, shadow.distance);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, shadow.choke);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, shadow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
}

void write_outer_glow_descriptor(BigEndianWriter& writer, const LayerOuterGlow& glow) {
  write_descriptor_object_header(writer, "", "OrGl", 10);
  write_descriptor_bool_item(writer, "enab", glow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", glow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", glow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, glow.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, glow.spread);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, glow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_unit_float_item(writer, "ShdN", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_unit_float_item(writer, "Inpr", {'#', 'P', 'r', 'c'}, 50.0);
}

void write_inner_glow_descriptor(BigEndianWriter& writer, const LayerInnerGlow& glow) {
  write_descriptor_object_header(writer, "", "IrGl", 11);
  write_descriptor_bool_item(writer, "enab", glow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", glow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", glow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, glow.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, glow.choke);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, glow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_enum_item(writer, "glwS", "IGSr", inner_glow_source_descriptor_value(glow.source));
  write_descriptor_unit_float_item(writer, "ShdN", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_unit_float_item(writer, "Inpr", {'#', 'P', 'r', 'c'}, 50.0);
}

void write_color_overlay_descriptor(BigEndianWriter& writer, const LayerColorOverlay& overlay) {
  write_descriptor_object_header(writer, "", "SoFi", 4);
  write_descriptor_bool_item(writer, "enab", overlay.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", overlay.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", overlay.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, overlay.opacity * 100.0);
}

void write_gradient_fill_descriptor(BigEndianWriter& writer, const LayerGradientFill& fill) {
  // Field set and order mirror what Photoshop 2026 writes for a gradient
  // overlay. PS silently resets the blend mode of GrFl descriptors that lack
  // this shape (byte-diffed July 2026), so keep the layout exact.
  write_descriptor_object_header(writer, "", "GrFl", 14);
  write_descriptor_bool_item(writer, "enab", fill.enabled);
  write_descriptor_bool_item(writer, "present", true);
  write_descriptor_bool_item(writer, "showInDialog", true);
  write_blend_mode_descriptor_item(writer, "Md  ", fill.blend_mode);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, fill.opacity * 100.0);
  write_layer_style_gradient_descriptor_item(writer, "Grad", fill.gradient);
  write_descriptor_unit_float_item(writer, "Angl", {'#', 'A', 'n', 'g'}, fill.gradient.angle_degrees);
  write_descriptor_enum_item(writer, "Type", "GrdT", gradient_type_descriptor_value(fill.gradient.type));
  write_descriptor_bool_item(writer, "Rvrs", fill.gradient.reverse);
  write_descriptor_bool_item(writer, "Dthr", false);
  // "Classic" gradient interpolation, matching Patchy's linear stop ramps.
  write_descriptor_enum_item(writer, "gs99", "gradientInterpolationMethodType", "Gcls");
  write_descriptor_bool_item(writer, "Algn", true);
  write_descriptor_unit_float_item(writer, "Scl ", {'#', 'P', 'r', 'c'}, fill.gradient.scale * 100.0);
  // Photoshop's draggable gradient offset; Patchy has no offset model, so the
  // gradient stays centered.
  write_descriptor_item_header(writer, "Ofst", {'O', 'b', 'j', 'c'});
  write_descriptor_object_header(writer, "", "Pnt ", 2);
  write_descriptor_unit_float_item(writer, "Hrzn", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_unit_float_item(writer, "Vrtc", {'#', 'P', 'r', 'c'}, 0.0);
}

void write_stroke_descriptor(BigEndianWriter& writer, const LayerStroke& stroke) {
  write_descriptor_object_header(writer, "", "FrFX", stroke.uses_gradient ? 19U : 10U);
  write_descriptor_bool_item(writer, "enab", stroke.enabled);
  write_descriptor_bool_item(writer, "present", true);
  write_descriptor_bool_item(writer, "showInDialog", true);
  write_descriptor_enum_item(writer, "Styl", "FStl", stroke_position_descriptor_value(stroke.position));
  write_descriptor_enum_item(writer, "PntT", "FrFl", stroke.uses_gradient ? "GrFl" : "SClr");
  write_blend_mode_descriptor_item(writer, "Md  ", stroke.blend_mode);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, stroke.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "Sz  ", {'#', 'P', 'x', 'l'}, stroke.size);
  if (stroke.uses_gradient) {
    // Photoshop writes a black Clr placeholder even though PntT selects the
    // following gradient. Omitting it makes the descriptor non-native.
    write_stroke_rgb_color_descriptor_item(writer, "Clr ", RgbColor{0, 0, 0});
    write_stroke_gradient_descriptor_item(writer, "Grad", stroke.gradient);
    write_descriptor_enum_item(writer, "gradientsInterpolationMethod", "gradientInterpolationMethodType", "Gcls");
    write_descriptor_unit_float_item(writer, "Angl", {'#', 'A', 'n', 'g'}, stroke.gradient.angle_degrees);
    write_descriptor_enum_item(writer, "Type", "GrdT", gradient_type_descriptor_value(stroke.gradient.type));
    write_descriptor_bool_item(writer, "Rvrs", stroke.gradient.reverse);
    write_descriptor_bool_item(writer, "Dthr", false);
    write_descriptor_unit_float_item(writer, "Scl ", {'#', 'P', 'r', 'c'}, stroke.gradient.scale * 100.0);
    write_descriptor_bool_item(writer, "Algn", true);
    write_descriptor_item_header(writer, "Ofst", {'O', 'b', 'j', 'c'});
    write_descriptor_object_header(writer, "", "Pnt ", 2);
    write_descriptor_unit_float_item(writer, "Hrzn", {'#', 'P', 'r', 'c'}, 0.0);
    write_descriptor_unit_float_item(writer, "Vrtc", {'#', 'P', 'r', 'c'}, 0.0);
  } else {
    write_stroke_rgb_color_descriptor_item(writer, "Clr ", stroke.color);
  }
  write_descriptor_bool_item(writer, "overprint", false);
}

void write_bevel_emboss_descriptor(BigEndianWriter& writer, const LayerBevelEmboss& bevel) {
  write_descriptor_object_header(writer, "", "ebbl", 13);
  write_descriptor_bool_item(writer, "enab", bevel.enabled);
  write_blend_mode_descriptor_item(writer, "hglM", bevel.highlight_blend_mode);
  write_rgb_color_descriptor_item(writer, "hglC", bevel.highlight_color);
  write_descriptor_unit_float_item(writer, "hglO", {'#', 'P', 'r', 'c'}, bevel.highlight_opacity * 100.0);
  write_blend_mode_descriptor_item(writer, "sdwM", bevel.shadow_blend_mode);
  write_rgb_color_descriptor_item(writer, "sdwC", bevel.shadow_color);
  write_descriptor_unit_float_item(writer, "sdwO", {'#', 'P', 'r', 'c'}, bevel.shadow_opacity * 100.0);
  write_descriptor_bool_item(writer, "uglg", bevel.use_global_light);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, bevel.angle_degrees);
  write_descriptor_unit_float_item(writer, "Lald", {'#', 'A', 'n', 'g'}, bevel.altitude_degrees);
  write_descriptor_unit_float_item(writer, "srgR", {'#', 'P', 'r', 'c'}, bevel.depth * 100.0);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, bevel.size);
  write_descriptor_enum_item(writer, "bvlD", "BESl", bevel.direction_up ? "In  " : "Out ");
}

void write_satin_descriptor(BigEndianWriter& writer, const LayerSatin& satin) {
  write_descriptor_object_header(writer, "", "ChFX", 10);
  write_descriptor_bool_item(writer, "enab", satin.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", satin.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", satin.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, satin.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, satin.angle_degrees);
  write_descriptor_unit_float_item(writer, "Dstn", {'#', 'P', 'x', 'l'}, satin.distance);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, satin.size);
  write_descriptor_bool_item(writer, "Invr", satin.invert);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
}

void write_pattern_descriptor(BigEndianWriter& writer, const LayerPatternOverlay& pattern) {
  write_descriptor_object_header(writer, "", "patternFill", 6);
  write_descriptor_bool_item(writer, "enab", pattern.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", pattern.blend_mode);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, pattern.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "Scl ", {'#', 'P', 'r', 'c'}, pattern.scale * 100.0);
  write_descriptor_bool_item(writer, "Algn", true);
  write_descriptor_item_header(writer, "Ptrn", {'O', 'b', 'j', 'c'});
  write_descriptor_object_header(writer, "", "Ptrn", 2);
  write_descriptor_text_item(writer, "Nm  ", pattern.pattern_name);
  write_descriptor_text_item(writer, "Idnt", pattern.pattern_id);
}

template <typename Effect, typename Writer>
void write_layer_effect_item(BigEndianWriter& writer, std::string_view single_key, std::string_view multi_key,
                             const std::vector<Effect>& effects, Writer write_effect) {
  if (effects.empty()) {
    return;
  }
  if (effects.size() == 1U) {
    write_descriptor_item_header(writer, single_key, {'O', 'b', 'j', 'c'});
    write_effect(writer, effects.front());
    return;
  }
  write_descriptor_item_header(writer, multi_key, {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(effects.size(), "layer effect list"));
  for (const auto& effect : effects) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_effect(writer, effect);
  }
}

std::vector<std::uint8_t> photoshop_lfx2_layer_style_payload(const LayerStyle& style) {
  BigEndianWriter payload;
  payload.write_u32(0);   // object effects version
  payload.write_u32(16);  // descriptor version

  std::uint32_t item_count = 2;
  const auto add_item_if_present = [&item_count](const auto& effects) {
    if (!effects.empty()) {
      ++item_count;
    }
  };
  add_item_if_present(style.drop_shadows);
  add_item_if_present(style.inner_shadows);
  add_item_if_present(style.outer_glows);
  add_item_if_present(style.inner_glows);
  add_item_if_present(style.satins);
  add_item_if_present(style.bevels);
  add_item_if_present(style.color_overlays);
  add_item_if_present(style.gradient_fills);
  add_item_if_present(style.pattern_overlays);
  add_item_if_present(style.strokes);

  write_descriptor_object_header(payload, "", "null", item_count);
  write_descriptor_unit_float_item(payload, "Scl ", {'#', 'P', 'r', 'c'}, 100.0);
  write_descriptor_bool_item(payload, "masterFXSwitch", style.effects_visible);
  write_layer_effect_item(payload, "DrSh", "dropShadowMulti", style.drop_shadows, write_drop_shadow_descriptor);
  write_layer_effect_item(payload, "IrSh", "innerShadowMulti", style.inner_shadows, write_inner_shadow_descriptor);
  write_layer_effect_item(payload, "OrGl", "outerGlowMulti", style.outer_glows, write_outer_glow_descriptor);
  write_layer_effect_item(payload, "IrGl", "innerGlowMulti", style.inner_glows, write_inner_glow_descriptor);
  write_layer_effect_item(payload, "ChFX", "chromeFXMulti", style.satins, write_satin_descriptor);
  write_layer_effect_item(payload, "ebbl", "bevelEmbossMulti", style.bevels, write_bevel_emboss_descriptor);
  write_layer_effect_item(payload, "SoFi", "solidFillMulti", style.color_overlays, write_color_overlay_descriptor);
  write_layer_effect_item(payload, "GrFl", "gradientFillMulti", style.gradient_fills, write_gradient_fill_descriptor);
  write_layer_effect_item(payload, "patternFill", "patternFillMulti", style.pattern_overlays, write_pattern_descriptor);
  write_layer_effect_item(payload, "FrFX", "frameFXMulti", style.strokes, write_stroke_descriptor);
  return payload.bytes();
}

PsdTextGeometry text_geometry_for_layer(const Layer& layer, const Rect& text_bounds, bool boxed_text,
                                        const TextWarp* warp) {
  PsdTextGeometry geometry;
  geometry.transform = {1.0, 0.0, 0.0, 1.0, static_cast<double>(text_bounds.x), static_cast<double>(text_bounds.y)};
  geometry.bounds =
      PsdTextBoundsD{0.0, 0.0, static_cast<double>(std::max(1, text_bounds.width)),
                     static_cast<double>(std::max(1, text_bounds.height))};
  geometry.bounding_box = geometry.bounds;
  geometry.box_bounds = geometry.bounds;
  geometry.tail_bounds = {text_bounds.x, text_bounds.y, text_bounds.x + text_bounds.width,
                          text_bounds.y + text_bounds.height};
  bool bounding_box_from_pixels = false;

  if (const auto patchy_transform = layer_metadata_value(layer, kLayerMetadataTextTransform); patchy_transform.has_value()) {
    if (const auto parsed = parse_double_array6(*patchy_transform); parsed.has_value()) {
      geometry.transform = *parsed;
    }
  } else if (const auto psd_transform = layer_metadata_value(layer, kLayerMetadataPsdTextTransform); psd_transform.has_value()) {
    if (const auto parsed = parse_double_array6(*psd_transform); parsed.has_value()) {
      geometry.transform = *parsed;
    }
  }
  if (warp != nullptr) {
    // A warped layer's pixels are the WARPED render, so neither the layer rect nor
    // the visible-pixel scan describes the text box; the warp metadata carries the
    // unwarped layout bounds in the same text-local space as the transform above.
    geometry.bounds = PsdTextBoundsD{warp->bounds_left, warp->bounds_top, warp->bounds_right,
                                     warp->bounds_bottom};
    geometry.bounding_box = geometry.bounds;
    bounding_box_from_pixels = true;
    if (!boxed_text && warp->baseline > 0.5) {
      // Photoshop anchors a point-text transform at the first-line BASELINE (its
      // own warped files store box top = -ascent): shift the top-left-origin
      // Patchy geometry there or a type re-render in Photoshop drops the text by
      // one descent (the buldge_test jump, July 2026). The shifted box top goes
      // below -0.5, which also skips the generic ink-bottom anchoring beneath.
      translate_text_geometry_local(geometry, 0.0, warp->baseline);
    }
  } else if (const auto visible = visible_pixel_local_bounds(layer.pixels()); visible.has_value()) {
    if (const auto local_bounds = visible_text_local_bounds_from_layer_pixels(layer, *visible, geometry.transform);
        local_bounds.has_value()) {
      geometry.bounding_box = *local_bounds;
      bounding_box_from_pixels = true;
    }
  }
  const auto preserve_imported_geometry = should_preserve_imported_text_geometry(layer);
  if (preserve_imported_geometry) {
    if (const auto bounds = layer_metadata_value(layer, kLayerMetadataPsdTextBounds); bounds.has_value()) {
      if (const auto parsed = parse_text_bounds_metadata(*bounds); parsed.has_value()) {
        geometry.bounds = *parsed;
      }
    }
    if (const auto bounds = layer_metadata_value(layer, kLayerMetadataPsdTextBoundingBox); bounds.has_value()) {
      if (const auto parsed = parse_text_bounds_metadata(*bounds); parsed.has_value()) {
        geometry.bounding_box = *parsed;
      }
    } else if (!bounding_box_from_pixels) {
      geometry.bounding_box = geometry.bounds;
    }
    if (const auto bounds = layer_metadata_value(layer, kLayerMetadataPsdTextBoxBounds); bounds.has_value()) {
      if (const auto parsed = parse_text_bounds_metadata(*bounds); parsed.has_value()) {
        geometry.box_bounds = *parsed;
      }
    } else if (boxed_text) {
      geometry.box_bounds = PsdTextBoundsD{0.0, 0.0, geometry.bounds.right, geometry.bounds.bottom};
    }
    if (const auto tail = layer_metadata_value(layer, kLayerMetadataPsdTextTailBounds); tail.has_value()) {
      if (const auto parsed = parse_int_array4(*tail); parsed.has_value()) {
        geometry.tail_bounds = *parsed;
      }
    }
  } else if (boxed_text) {
    geometry.box_bounds = PsdTextBoundsD{0.0, 0.0, geometry.bounds.right, geometry.bounds.bottom};
  } else if (!bounding_box_from_pixels) {
    geometry.bounding_box = geometry.bounds;
  }
  if (!boxed_text && point_text_geometry_needs_baseline_anchor(layer, geometry)) {
    translate_text_geometry_local(geometry, 0.0, point_text_baseline_offset(layer, geometry));
  }
  if (const auto index = layer_metadata_value(layer, kLayerMetadataPsdTextIndex); index.has_value()) {
    geometry.text_index = std::max(0, parse_int_or(*index, 0));
  }
  return geometry;
}

std::optional<std::vector<std::uint8_t>> photoshop_type_tool_payload_for_layer(const Layer& layer,
                                                                               const Rect& bounds) {
  const auto text = layer_metadata_value(layer, kLayerMetadataText);
  if (!text.has_value() || text->empty()) {
    return std::nullopt;
  }
  if (should_preserve_imported_text_geometry(layer)) {
    if (const auto templated_payload = photoshop_type_tool_payload_from_template(layer, *text);
        templated_payload.has_value()) {
      return templated_payload;
    }
  }
  const auto runs = text_runs_for_layer(layer, *text);
  if (runs.empty()) {
    return std::nullopt;
  }
  const auto paragraph_runs = paragraph_runs_for_layer(layer, *text);
  auto text_bounds = bounds;
  const auto boxed_text = layer_metadata_value(layer, kLayerMetadataTextFlow).value_or(std::string_view{}) == "box";
  if (boxed_text) {
    if (const auto width = layer_metadata_value(layer, kLayerMetadataTextBoxWidth); width.has_value()) {
      text_bounds.width = std::max(1, parse_int_or(*width, bounds.width));
    }
    if (const auto height = layer_metadata_value(layer, kLayerMetadataTextBoxHeight); height.has_value()) {
      text_bounds.height = std::max(1, parse_int_or(*height, bounds.height));
    }
  }
  const auto warp = text_warp_from_layer(layer);
  const bool warp_active = warp.has_value() && !text_warp_is_identity(*warp);
  const auto geometry = text_geometry_for_layer(layer, text_bounds, boxed_text,
                                                warp_active ? &*warp : nullptr);
  const auto anti_alias_metadata = layer_metadata_value(layer, kLayerMetadataTextAntiAlias);
  const auto anti_alias = anti_alias_metadata.has_value() ? parse_int_or(*anti_alias_metadata, 3) : 3;
  const auto engine_data =
      engine_data_for_text(*text, runs, paragraph_runs, boxed_text, geometry.box_bounds, anti_alias);
  const auto descriptor_text = photoshop_engine_text(*text);

  BigEndianWriter writer;
  writer.write_u16(1);
  for (const auto value : geometry.transform) {
    write_f64(writer, value);
  }
  writer.write_u16(50);
  writer.write_u32(16);
  write_text_descriptor(writer, descriptor_text, engine_data, geometry);
  writer.write_u16(1);
  writer.write_u32(16);
  write_warp_descriptor(writer, warp_active ? &*warp : nullptr);
  if (warp_active) {
    // Photoshop stores the warp's reference box (the text 'bounds') as four
    // big-endian float32s in the TySh tail when warped, zeros otherwise.
    write_f32(writer, static_cast<float>(geometry.bounds.left));
    write_f32(writer, static_cast<float>(geometry.bounds.top));
    write_f32(writer, static_cast<float>(geometry.bounds.right));
    write_f32(writer, static_cast<float>(geometry.bounds.bottom));
  } else {
    for (const auto value : geometry.tail_bounds) {
      write_i32(writer, value);
    }
  }
  return writer.bytes();
}

bool should_write_generated_text_block(const EncodedLayer& encoded) {
  if (encoded.layer == nullptr || encoded.kind != EncodedLayerKind::Pixel || !layer_is_text(*encoded.layer)) {
    return false;
  }
  const auto raster_status = layer_metadata_value(*encoded.layer, kLayerMetadataTextRasterStatus).value_or(std::string_view{});
  if ((raster_status == "psd_raster_preview" || raster_status == "placeholder") &&
      !text_transform_overrides_psd_template(*encoded.layer)) {
    return false;
  }
  return layer_metadata_value(*encoded.layer, kLayerMetadataText).has_value();
}

LayerRecord read_layer_record(BigEndianReader& reader, bool large_document,
                              const CmykColorConverter& cmyk) {
  LayerRecord record;
  const auto top = static_cast<std::int32_t>(reader.read_u32());
  const auto left = static_cast<std::int32_t>(reader.read_u32());
  const auto bottom = static_cast<std::int32_t>(reader.read_u32());
  const auto right = static_cast<std::int32_t>(reader.read_u32());
  record.bounds = Rect{left, top, right - left, bottom - top};

  const auto channel_count = reader.read_u16();
  for (std::uint16_t i = 0; i < channel_count; ++i) {
    record.channels.push_back(LayerChannelInfo{
        reader.read_u16(),
        large_document ? reader.read_u64() : static_cast<std::uint64_t>(reader.read_u32())});
  }

  const auto signature = read_signature(reader);
  if (signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
      signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
    throw std::runtime_error("Invalid PSD layer blend mode signature");
  }
  record.blend_mode = blend_mode_from_key(read_signature(reader));
  record.opacity = reader.read_u8();
  record.clipping = reader.read_u8() != 0;
  const auto flags = reader.read_u8();
  record.visible = (flags & 0x02U) == 0;
  reader.skip(1);  // filler

  const auto extra_length = read_section_length(reader, "layer extra data");
  const auto extra_end = reader.position() + extra_length;
  if (extra_length >= 8) {
    const auto mask_length = read_section_length(reader, "layer mask data");
    const auto mask_end = reader.position() + mask_length;
    if (mask_length >= 18U) {
      const auto mask_top = static_cast<std::int32_t>(reader.read_u32());
      const auto mask_left = static_cast<std::int32_t>(reader.read_u32());
      const auto mask_bottom = static_cast<std::int32_t>(reader.read_u32());
      const auto mask_right = static_cast<std::int32_t>(reader.read_u32());
      const auto default_color = reader.read_u8();
      const auto mask_flags = reader.read_u8();
      // Flag bit 0 ("position relative to layer" in the spec) is how Photoshop persists the
      // layer/mask link toggle: 1 means the chain icon is off (unlinked).
      record.mask = LayerMaskInfo{Rect{mask_left, mask_top, mask_right - mask_left, mask_bottom - mask_top}, default_color,
                                  (mask_flags & 0x02U) != 0, (mask_flags & 0x01U) == 0};
    }
    if (reader.position() < mask_end) {
      reader.skip(mask_end - reader.position());
    }
    const auto blending_ranges_length = read_section_length(reader, "layer blending ranges");
    record.blending_ranges = reader.read_bytes(blending_ranges_length);
    if (reader.position() < extra_end) {
      record.name = read_pascal_string(reader, 4);
    }
    while (reader.position() + 12 <= extra_end) {
      const auto block_signature = read_signature(reader);
      if (block_signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
          block_signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
        break;
      }
      const auto block_key = read_signature(reader);
      const auto key = key_string(block_key);
      // Photoshop's parser picks the length width BY KEY (the documented 8-byte set)
      // in PSBs; the '8B64' signature additionally marks extras like 'cinf'. Both
      // rules must apply on read: PS 2023 writes e.g. 'lnk2' as '8BIM' + u64 in
      // PSBs, and honoring the signature alone misreads the length and derails the
      // whole block walk (the 10cm-table-tent linked-SO regression).
      const bool wide_length = block_signature == std::array<char, 4>{'8', 'B', '6', '4'} ||
                               (large_document && tagged_block_length_is_u64(key));
      if (wide_length && extra_end - reader.position() < 8U) {
        break;
      }
      const auto block_length =
          wide_length ? reader.read_u64() : static_cast<std::uint64_t>(reader.read_u32());
      if (block_length > extra_end - reader.position()) {
        break;
      }
      auto payload = reader.read_bytes(static_cast<std::size_t>(block_length));
      record.additional_blocks.push_back(UnknownPsdBlock{key, payload, wide_length});
      if (key == "luni") {
        if (auto unicode_name = read_unicode_string_payload(record.additional_blocks.back().payload);
            unicode_name.has_value()) {
          record.name = *unicode_name;
        }
      }
      if (key == "TySh" || key == "tySh") {
        record.text_source_block = key;
        const auto& text_payload = record.additional_blocks.back().payload;
        record.text_patchy_generated_type_block =
            record.text_patchy_generated_type_block || payload_has_patchy_generated_text_signature(text_payload);
        if (!record.text.has_value()) {
          record.text = extract_engine_data_text(text_payload);
        }
        if (!record.text_size.has_value()) {
          record.text_size = extract_engine_data_font_size(text_payload);
        }
        if (!record.text_color.has_value()) {
          record.text_color = extract_engine_data_fill_color(text_payload, cmyk);
        }
        if (!record.text_anti_alias.has_value()) {
          record.text_anti_alias = extract_engine_data_anti_alias(text_payload);
        }
        if (record.text.has_value() && !record.text_runs.has_value()) {
          if (auto runs = extract_engine_text_runs(text_payload, *record.text, record.text_size.value_or(36),
                                                   record.text_color.value_or(RgbColor{0, 0, 0}), cmyk);
              runs.has_value()) {
            if (!runs->empty()) {
              const auto& first_run = runs->front();
              record.text_font = first_run.family;
              record.text_size = std::clamp(static_cast<int>(std::lround(first_run.size)), 1, kMaxTextSizePixels);
              record.text_color = first_run.color;
              record.text_bold = first_run.bold;
              record.text_italic = first_run.italic;
            }
            record.text_runs = serialize_patchy_text_runs(*runs);
            if (auto paragraph_runs = extract_engine_paragraph_runs(text_payload, *record.text);
                paragraph_runs.has_value()) {
              record.text_paragraph_runs = serialize_patchy_paragraph_runs(*paragraph_runs);
              record.text_html = html_from_text_runs(*record.text, *runs, *paragraph_runs);
            } else {
              record.text_html = html_from_text_runs(*record.text, *runs);
            }
          }
        }
        if (record.text.has_value() && !record.text_paragraph_runs.has_value()) {
          if (auto paragraph_runs = extract_engine_paragraph_runs(text_payload, *record.text);
              paragraph_runs.has_value()) {
            record.text_paragraph_runs = serialize_patchy_paragraph_runs(*paragraph_runs);
          }
        }
        if (!record.text_box.has_value()) {
          record.text_box = extract_type_tool_text_box(text_payload);
        }
        if (!record.text_geometry.has_value()) {
          record.text_geometry = extract_type_tool_geometry(text_payload);
        }
      }
      if (key == "lfx2") {
        merge_missing_layer_style_effects(record.layer_style,
                                          parse_lfx2_layer_style(record.additional_blocks.back().payload, cmyk));
      } else if (key == "lrFX") {
        merge_missing_layer_style_effects(record.layer_style, parse_lrfx_layer_style(record.additional_blocks.back().payload));
      } else if (key == "plFX") {
        if (auto patchy_style = parse_patchy_layer_style(record.additional_blocks.back().payload);
            patchy_style.has_value()) {
          record.layer_style = std::move(*patchy_style);
        }
      }
      if (key == "lspf" && record.additional_blocks.back().payload.size() >= 4U) {
        BigEndianReader protection_reader(record.additional_blocks.back().payload);
        record.protection_flags = protection_reader.read_u32();
      }
      if (key == "lmgm" && !record.additional_blocks.back().payload.empty()) {
        // "Layer Mask Hides Effects" blending option (first byte is the bool).
        record.layer_mask_hides_effects = record.additional_blocks.back().payload[0] != 0;
      }
      if (key == "lsct" || key == "lsdk") {
        const auto& section_payload = record.additional_blocks.back().payload;
        if (section_payload.size() >= 4U) {
          BigEndianReader section_reader(section_payload);
          record.section_divider_type = section_reader.read_u32();
          if (section_reader.remaining() >= 8U) {
            const auto section_signature = read_signature(section_reader);
            if (section_signature == std::array<char, 4>{'8', 'B', 'I', 'M'} ||
                section_signature == std::array<char, 4>{'8', 'B', '6', '4'}) {
              record.blend_mode = blend_mode_from_key(read_signature(section_reader));
            }
          }
        }
      }
      if (key == "SoLd" || key == "SoLE") {
        if (auto info = parse_placed_layer_block(key, record.additional_blocks.back().payload); info.has_value()) {
          record.placed = std::move(*info);
          record.placed_source_block = key;
          record.placed_from_sold = true;
        } else if (!record.placed_from_sold) {
          // An unreadable SoLd wins over any PlLd fallback: the layer imports as a
          // plain preview with its blobs preserved verbatim.
          record.placed.reset();
          record.placed_parse_failed = true;
        }
      }
      if ((key == "PlLd" || key == "plLd") && !record.placed_from_sold && !record.placed_parse_failed) {
        if (auto info = parse_placed_layer_block(key, record.additional_blocks.back().payload); info.has_value()) {
          record.placed = std::move(*info);
          record.placed_source_block = key;
        }
      }
    }
  }
  if (reader.position() < extra_end) {
    reader.skip(extra_end - reader.position());
  }
  if (record.name.empty()) {
    record.name = "Layer";
  }
  return record;
}

bool encoded_layer_uses_source_state(const EncodedLayer& encoded) noexcept {
  return encoded.layer != nullptr && encoded.kind != EncodedLayerKind::GroupBoundary;
}

std::string encoded_layer_name(const EncodedLayer& encoded) {
  return encoded.kind == EncodedLayerKind::GroupBoundary ? "</Layer group>" : encoded.layer->name();
}

BlendMode encoded_layer_blend_mode(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->blend_mode() : BlendMode::Normal;
}

float encoded_layer_opacity(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->opacity() : 1.0F;
}

bool encoded_layer_visible(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->visible() : true;
}

std::uint8_t encoded_layer_clipping(const EncodedLayer& encoded) noexcept {
  // Divider records (Group folders + GroupBoundary) always write clipping 0:
  // Photoshop cannot clip groups and boundary records carry no layer state.
  return encoded_layer_uses_source_state(encoded) && encoded.kind != EncodedLayerKind::Group &&
                 encoded.layer->clipped()
             ? 1U
             : 0U;
}

std::uint32_t group_section_divider_type(const Layer& layer) {
  if (!layer_group_expanded(layer)) {
    return 2U;
  }
  return 1U;
}

std::vector<std::uint8_t> section_divider_payload(std::uint32_t type, BlendMode blend_mode,
                                                  bool include_blend_mode) {
  BigEndianWriter payload;
  payload.write_u32(type);
  if (include_blend_mode) {
    write_signature(payload, {'8', 'B', 'I', 'M'});
    write_signature(payload, blend_mode_key(blend_mode));
  }
  return payload.bytes();
}

// Per-layer smart object blocks reference embedded sources stored in the document-global
// 'lnk2'/'lnkD' blocks. Photoshop opens a file where those references dangle, but its
// save pipeline fails ("disk error (-1)"), so they must never be written without the data.
bool is_smart_object_reference_block(std::string_view key) {
  return key == "PlLd" || key == "plLd" || key == "SoLd" || key == "SoLE";
}

bool should_skip_layer_block(const EncodedLayer& encoded, const UnknownPsdBlock& block, bool generated_text_block,
                             bool generated_style_block) {
  if (block.key == "luni" || block.key == "plFX" || block.key == "lspf" || block.key == "lmgm" ||
      (block.key == "plAD" && encoded.kind == EncodedLayerKind::Adjustment)) {
    return true;
  }
  // A moved/transformed smart object regenerates its placed-layer blocks (see the
  // block_dirty handling at the end of write_layer_record) instead of re-emitting the
  // stale originals.
  if (encoded.layer != nullptr && is_smart_object_reference_block(block.key) &&
      layer_smart_object_block_dirty(*encoded.layer)) {
    return true;
  }
  if (encoded.kind == EncodedLayerKind::Adjustment &&
      (block.key == "levl" || block.key == "curv" || block.key == "hue2")) {
    return true;
  }
  if (generated_style_block && (block.key == "lfx2" || block.key == "lrFX")) {
    return true;
  }
  if (generated_text_block && (block.key == "TySh" || block.key == "tySh")) {
    return true;
  }
  return encoded.kind == EncodedLayerKind::Group && (block.key == "lsct" || block.key == "lsdk");
}

bool layer_preserves_photoshop_layer_style(const Layer& layer) {
  return std::any_of(layer.unknown_psd_blocks().begin(), layer.unknown_psd_blocks().end(),
                     [](const UnknownPsdBlock& block) { return block.key == "lfx2" || block.key == "lrFX"; });
}

const UnknownPsdBlock* find_layer_block(const Layer& layer, std::string_view key) {
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == key) {
      return &block;
    }
  }
  return nullptr;
}

void write_layer_record(BigEndianWriter& writer, const EncodedLayer& encoded, bool strip_smart_object_blocks,
                        bool large_document) {
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.y));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.x));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.y + encoded.bounds.height));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.x + encoded.bounds.width));
  writer.write_u16(static_cast<std::uint16_t>(encoded.channels.size()));

  for (const auto& channel : encoded.channels) {
    writer.write_u16(channel.id);
    if (large_document) {
      writer.write_u64(channel.data.size() + 2);
    } else {
      writer.write_u32(checked_u32(channel.data.size() + 2, "layer channel data length"));
    }
  }

  write_signature(writer, {'8', 'B', 'I', 'M'});
  write_signature(writer, blend_mode_key(encoded_layer_blend_mode(encoded)));
  writer.write_u8(
      static_cast<std::uint8_t>(std::clamp(std::lround(encoded_layer_opacity(encoded) * 255.0F), 0L, 255L)));
  writer.write_u8(encoded_layer_clipping(encoded));
  // Bit 3 marks the record as Photoshop 5.0+. Without it Photoshop falls back to legacy
  // layer semantics — most visibly, an unlinked layer mask's rectangle gets treated as
  // relative to the layer, scrambling masked layers that carry effects.
  std::uint8_t record_flags = 0x08U;
  if (!encoded_layer_visible(encoded)) {
    record_flags |= 0x02U;
  }
  writer.write_u8(record_flags);
  writer.write_u8(0);

  BigEndianWriter extra;
  if (encoded.layer != nullptr &&
      (encoded.kind == EncodedLayerKind::Pixel || encoded.kind == EncodedLayerKind::Adjustment) &&
      encoded.layer->mask().has_value()) {
    const auto& mask = *encoded.layer->mask();
    BigEndianWriter mask_data;
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.y));
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.x));
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.y + mask.bounds.height));
    mask_data.write_u32(static_cast<std::uint32_t>(mask.bounds.x + mask.bounds.width));
    mask_data.write_u8(mask.default_color);
    // Bit 0 set = mask unlinked from the layer (Photoshop's chain toggle), bit 1 = mask disabled.
    std::uint8_t mask_flags = 0;
    if (!layer_mask_linked(*encoded.layer)) {
      mask_flags |= 0x01U;
    }
    if (mask.disabled) {
      mask_flags |= 0x02U;
    }
    mask_data.write_u8(mask_flags);
    mask_data.write_u16(0);
    write_length_prefixed_block(extra, mask_data.bytes());
  } else {
    extra.write_u32(0);  // layer mask data
  }
  if (encoded.blending_ranges != nullptr) {
    write_length_prefixed_block(extra, *encoded.blending_ranges);
  } else {
    extra.write_u32(0);  // layer blending ranges
  }
  const auto name = encoded_layer_name(encoded);
  write_pascal_string(extra, name, 4);
  auto unicode_name = unicode_string_payload(name);
  write_additional_layer_block(extra, {'l', 'u', 'n', 'i'}, unicode_name, large_document);

  if (encoded.kind == EncodedLayerKind::GroupBoundary) {
    const auto payload = section_divider_payload(3U, BlendMode::Normal, false);
    write_additional_layer_block(extra, {'l', 's', 'c', 't'}, payload, large_document);
  } else if (encoded.kind == EncodedLayerKind::Group) {
    const auto payload =
        section_divider_payload(group_section_divider_type(*encoded.layer), encoded.layer->blend_mode(), true);
    write_additional_layer_block(extra, {'l', 's', 'c', 't'}, payload, large_document);
  }

  bool generated_style_payload = false;
  if (encoded.layer != nullptr && !encoded.layer->layer_style().empty() &&
      !layer_preserves_photoshop_layer_style(*encoded.layer)) {
    const auto payload = photoshop_lfx2_layer_style_payload(encoded.layer->layer_style());
    write_additional_layer_block(extra, {'l', 'f', 'x', '2'}, payload, large_document);
    generated_style_payload = true;
  }

  if (encoded.layer != nullptr && encoded.kind == EncodedLayerKind::Adjustment) {
    const auto settings = adjustment_settings_from_layer(*encoded.layer);
    if (settings.has_value() && settings->kind == AdjustmentKind::Levels) {
      write_additional_layer_block(extra, kPhotoshopLevelsAdjustmentBlockKey,
                                   photoshop_levels_payload(settings->levels), large_document);
    }
    if (settings.has_value() && settings->kind == AdjustmentKind::Curves) {
      write_additional_layer_block(
          extra, kPhotoshopCurvesAdjustmentBlockKey,
          photoshop_curves_payload(settings->curves, find_layer_block(*encoded.layer, "curv")),
          large_document);
    }
    if (settings.has_value() && settings->kind == AdjustmentKind::HueSaturation) {
      write_additional_layer_block(
          extra, kPhotoshopHueSaturationBlockKey,
          photoshop_hue2_payload(settings->hue_saturation, find_layer_block(*encoded.layer, "hue2")),
          large_document);
    }
    // Native Curves carries the complete Patchy point model. Writing plAD next
    // to curv makes Photoshop report "unknown data" on every open, even though
    // it recognizes the native adjustment. Legacy plAD remains readable and is
    // migrated to curv on save; malformed native layers stay opaque above and
    // therefore retain both raw blocks.
    if (!settings.has_value() || settings->kind != AdjustmentKind::Curves) {
      const auto payload = patchy_adjustment_payload(*encoded.layer);
      if (!payload.empty()) {
        write_additional_layer_block(extra, kPatchyAdjustmentBlockKey, payload, large_document);
      }
    }
  }

  const auto generated_text_payload = should_write_generated_text_block(encoded)
                                          ? photoshop_type_tool_payload_for_layer(*encoded.layer, encoded.bounds)
                                          : std::optional<std::vector<std::uint8_t>>{};
  if (generated_text_payload.has_value()) {
    write_additional_layer_block(extra, {'T', 'y', 'S', 'h'}, *generated_text_payload, large_document);
  }

  if (encoded.layer != nullptr) {
    const auto protection_flags = layer_lock_flags(*encoded.layer) &
                                  (kPsdProtectTransparency | kPsdProtectComposite | kPsdProtectPosition);
    if (protection_flags != 0U) {
      BigEndianWriter protection;
      protection.write_u32(protection_flags);
      write_additional_layer_block(extra, {'l', 's', 'p', 'f'}, protection.bytes(), large_document);
    }

    if (encoded.layer->layer_style().layer_mask_hides_effects) {
      // "Layer Mask Hides Effects" blending option; absence means off.
      BigEndianWriter mask_hides;
      mask_hides.write_u8(1);
      mask_hides.write_u8(0);
      mask_hides.write_u16(0);
      write_additional_layer_block(extra, {'l', 'm', 'g', 'm'}, mask_hides.bytes(), large_document);
    }

    for (const auto& block : encoded.layer->unknown_psd_blocks()) {
      if (should_skip_layer_block(encoded, block, generated_text_payload.has_value(), generated_style_payload)) {
        continue;
      }
      if (strip_smart_object_blocks && is_smart_object_reference_block(block.key)) {
        continue;
      }
      if (auto key = block_key_from_string(block.key); key.has_value()) {
        write_additional_layer_block(extra, *key, block.payload, large_document, block.long_length);
      }
    }

    // A dirty smart-object placement (moved/transformed since import) re-emits its
    // placed-layer blocks with the current quad patched in; unmodeled descriptor
    // fields survive because the regeneration patches the ORIGINAL payload. A failed
    // regeneration falls back to the original bytes (stale quad beats a broken block).
    if (!strip_smart_object_blocks && layer_smart_object_block_dirty(*encoded.layer)) {
      const auto placement = smart_object_placement_from_layer(*encoded.layer);
      const auto warp = smart_object_warp_from_layer(*encoded.layer);
      for (const auto& block : encoded.layer->unknown_psd_blocks()) {
        if (!is_smart_object_reference_block(block.key)) {
          continue;
        }
        const auto key = block_key_from_string(block.key);
        if (!key.has_value()) {
          continue;
        }
        std::optional<std::vector<std::uint8_t>> regenerated;
        if (placement.has_value()) {
          regenerated = regenerate_placed_layer_payload(block.key, block.payload, *placement,
                                                        warp.has_value() ? &*warp : nullptr);
        }
        if (regenerated.has_value()) {
          write_additional_layer_block(extra, *key, *regenerated, large_document, block.long_length);
        } else {
          write_additional_layer_block(extra, *key, block.payload, large_document, block.long_length);
        }
      }
    }
  }
  if ((extra.bytes().size() % 2U) != 0) {
    extra.write_u8(0);
  }
  write_length_prefixed_block(writer, extra.bytes());
}

EncodedLayer encode_layer(const Layer& layer, bool large_document) {
  if (layer.kind() != LayerKind::Pixel) {
    throw std::runtime_error("Layered PSD export currently supports pixel and group layers only");
  }
  const auto& pixels = layer.pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3 || pixels.format().channels > 4) {
    throw std::runtime_error("Layered PSD export currently supports RGB/RGBA 8-bit layers only");
  }

  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Pixel;
  encoded.bounds = layer.bounds().empty() ? Rect::from_size(pixels.width(), pixels.height()) : layer.bounds();
  encoded.blending_ranges = &layer.raw_psd_blending_ranges();
  std::vector<std::uint16_t> channel_ids{kChannelRed, kChannelGreen, kChannelBlue};
  if (pixels.format().channels >= 4) {
    channel_ids.push_back(kChannelTransparency);
  }
  if (layer.mask().has_value() && !layer.mask()->pixels.empty()) {
    const auto& mask = *layer.mask();
    if (mask.pixels.format() != PixelFormat::gray8()) {
      throw std::runtime_error("Layered PSD export requires 8-bit grayscale layer masks");
    }
    if (mask.bounds.width != mask.pixels.width() || mask.bounds.height != mask.pixels.height()) {
      throw std::runtime_error("Layer mask bounds do not match mask pixels");
    }
    channel_ids.push_back(kChannelUserMask);
  }

  const auto pixel_count = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  encoded.channels.reserve(channel_ids.size());
  for (std::size_t channel_index = 0; channel_index < channel_ids.size(); ++channel_index) {
    const auto channel_id = channel_ids[channel_index];
    if (channel_id == kChannelUserMask) {
      const auto& mask_pixels = layer.mask()->pixels;
      encoded.channels.push_back(encode_channel(channel_id, mask_pixels.width(), mask_pixels.height(),
                                                mask_pixels.data(), large_document));
    } else {
      std::vector<std::uint8_t> channel;
      channel.resize(pixel_count);
      const auto source_channel = channel_id == kChannelTransparency ? 3 : channel_index;
      for (std::size_t i = 0; i < pixel_count; ++i) {
        channel[i] = pixels.data()[i * pixels.format().channels + source_channel];
      }
      encoded.channels.push_back(encode_channel(channel_id, pixels.width(), pixels.height(), channel, large_document));
    }
  }
  return encoded;
}

EncodedLayer encode_adjustment_layer(const Layer& layer, bool large_document) {
  if (layer.kind() != LayerKind::Adjustment || !adjustment_settings_from_layer(layer).has_value()) {
    throw std::runtime_error("Adjustment layer is missing Patchy adjustment settings");
  }

  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Adjustment;
  encoded.bounds = layer.bounds();
  encoded.blending_ranges = &layer.raw_psd_blending_ranges();
  if (layer.mask().has_value() && !layer.mask()->pixels.empty()) {
    const auto& mask = *layer.mask();
    if (mask.pixels.format() != PixelFormat::gray8()) {
      throw std::runtime_error("Layered PSD export requires 8-bit grayscale layer masks");
    }
    if (mask.bounds.width != mask.pixels.width() || mask.bounds.height != mask.pixels.height()) {
      throw std::runtime_error("Layer mask bounds do not match mask pixels");
    }
    encoded.channels.push_back(encode_channel(kChannelUserMask, mask.pixels.width(), mask.pixels.height(),
                                              mask.pixels.data(), large_document));
  }
  return encoded;
}

EncodedLayer encode_group_boundary(const Layer& layer) {
  EncodedLayer encoded;
  encoded.kind = EncodedLayerKind::GroupBoundary;
  encoded.blending_ranges = &layer.raw_psd_group_boundary_blending_ranges();
  return encoded;
}

EncodedLayer encode_group(const Layer& layer) {
  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Group;
  encoded.bounds = layer.bounds();
  encoded.blending_ranges = &layer.raw_psd_blending_ranges();
  return encoded;
}

void append_encoded_layers(const Layer& layer, std::vector<EncodedLayer>& encoded_layers, bool large_document) {
  if (layer.kind() == LayerKind::Pixel) {
    encoded_layers.push_back(encode_layer(layer, large_document));
    return;
  }

  if (layer.kind() == LayerKind::Adjustment) {
    encoded_layers.push_back(encode_adjustment_layer(layer, large_document));
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    encoded_layers.push_back(encode_group_boundary(layer));
    for (const auto& child : layer.children()) {
      append_encoded_layers(child, encoded_layers, large_document);
    }
    encoded_layers.push_back(encode_group(layer));
    return;
  }

  throw std::runtime_error("Layered PSD export currently supports pixel, adjustment, and group layers only");
}

Document read_flat_composite(BigEndianReader& reader, const Header& header,
                             const CmykToRgbTransform* cmyk_icc,
                             const ParsedCompositeChannelResources& channel_resources,
                             bool has_merged_transparency) {
  const auto format = format_from_header(header);
  const auto compression = reader.read_u16();
  const auto source_is_cmyk = is_cmyk_color_mode(header.color_mode);

  Document document(static_cast<std::int32_t>(header.width), static_cast<std::int32_t>(header.height), format);
  PixelBuffer pixels(static_cast<std::int32_t>(header.width), static_cast<std::int32_t>(header.height), format);
  const auto channel_data = read_flat_image_channels(reader, header, compression);
  const auto channel_pixels = static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.height);

  if (source_is_cmyk) {
    convert_cmyk_planes_to_rgb(pixels, channel_data[0].data(), channel_data[1].data(),
                               channel_data[2].data(), channel_data[3].data(), channel_pixels,
                               cmyk_icc);
  } else {
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
      for (std::size_t i = 0; i < channel_pixels; ++i) {
        pixels.data()[i * 3 + channel] = channel_data[channel][i];
      }
    }
  }

  const auto color_channel_count = composite_color_channel_count(header.color_mode);
  const auto first_saved_channel = static_cast<std::uint16_t>(
      color_channel_count + (has_merged_transparency ? 1U : 0U));
  if (first_saved_channel > header.channels) {
    throw std::runtime_error("PSD merged transparency flag has no matching composite channel");
  }
  Layer& background = document.add_pixel_layer("Background", std::move(pixels));
  if (has_merged_transparency) {
    const auto& merged_alpha = channel_data[color_channel_count];
    if (merged_alpha.size() != channel_pixels) {
      throw std::runtime_error("PSD merged transparency dimensions do not match the document");
    }
    PixelBuffer mask_pixels(document.width(), document.height(), PixelFormat::gray8());
    std::copy(merged_alpha.begin(), merged_alpha.end(), mask_pixels.data().begin());
    background.set_mask(LayerMask{Rect::from_size(document.width(), document.height()),
                                  std::move(mask_pixels), 255, false});
    set_layer_mask_is_document_alpha(background, true);
  }
  std::vector<std::vector<std::uint8_t>> saved_channels;
  saved_channels.reserve(static_cast<std::size_t>(header.channels - first_saved_channel));
  for (std::uint16_t channel = first_saved_channel; channel < header.channels; ++channel) {
    saved_channels.push_back(std::move(channel_data[channel]));
  }
  if (!saved_channels.empty()) {
    add_saved_composite_channels(document, std::move(saved_channels), first_saved_channel, header,
                                 channel_resources);
  }

  return document;
}

struct SmartObjectImportCounts {
  std::size_t editable{0};
  std::size_t preview_locked{0};
  std::size_t external{0};
  std::size_t dangling{0};
};

// Resolves each smart-object layer's uuid against the parsed source store: layers with
// missing sources drop their metadata (the preserved blobs stay; the writer's existing
// dangling-reference strip keeps the file Photoshop-safe), and layers whose source is
// not embedded become preview-locked with reason "external".
void finalize_smart_object_layers(std::vector<Layer>& layers, const SmartObjectStore& store,
                                  SmartObjectImportCounts& counts) {
  for (auto& layer : layers) {
    if (!layer.children().empty()) {
      finalize_smart_object_layers(layer.children(), store, counts);
    }
    if (!layer_is_smart_object(std::as_const(layer))) {
      continue;
    }
    const auto uuid = smart_object_source_uuid(std::as_const(layer));
    if (uuid.empty() && smart_object_lock_reason(std::as_const(layer)) == "unparsed") {
      // Unreadable SoLd: the uuid is unknown BY DESIGN (empty), so the dangling
      // cleanup below must not strip the badge/protection metadata.
      ++counts.preview_locked;
      continue;
    }
    const auto* source = store.find(uuid);
    if (source == nullptr) {
      clear_layer_smart_object_metadata(layer);
      ++counts.dangling;
      continue;
    }
    auto lock = smart_object_lock_reason(std::as_const(layer));
    if (source->kind != SmartObjectSourceKind::Embedded && lock.empty()) {
      layer.metadata()[kLayerMetadataSmartObjectLock] = "external";
      lock = "external";
    }
    if (lock.empty()) {
      ++counts.editable;
    } else if (lock == "external") {
      ++counts.external;
    } else {
      ++counts.preview_locked;
    }
  }
}

void append_smart_object_notices(const SmartObjectImportCounts& counts, std::vector<std::string>* notices) {
  if (notices == nullptr) {
    return;
  }
  const auto plural = [](std::size_t count) { return count == 1 ? "" : "s"; };
  if (counts.editable > 0) {
    notices->push_back("Imported " + std::to_string(counts.editable) + " smart object layer" +
                       plural(counts.editable) + "; click a layer's smart-object badge to edit its embedded contents.");
  }
  if (counts.preview_locked > 0) {
    notices->push_back(std::to_string(counts.preview_locked) + " smart object layer" +
                       plural(counts.preview_locked) +
                       " use warp, perspective, smart filters, or unsupported data; Photoshop's preview is "
                       "shown (rasterize the layer to edit it in Patchy).");
  }
  if (counts.external > 0) {
    notices->push_back(std::to_string(counts.external) + " smart object layer" + plural(counts.external) +
                       " reference external files; the embedded preview is shown.");
  }
  if (counts.dangling > 0) {
    notices->push_back(std::to_string(counts.dangling) + " smart object layer" + plural(counts.dangling) +
                       " reference missing source data and were imported as regular layers.");
  }
}

bool is_section_divider_folder(std::uint32_t type) noexcept {
  return type == 1U || type == 2U;
}

bool is_section_divider_boundary(std::uint32_t type) noexcept {
  return type == 3U;
}

void copy_layer_state(Layer& target, const Layer& source) {
  target.set_bounds(source.bounds());
  target.set_blend_mode(source.blend_mode());
  target.set_opacity(source.opacity());
  target.set_visible(source.visible());
  target.set_clipped(source.clipped());
  target.set_lock_flags(source.lock_flags());
  target.layer_style() = source.layer_style();
  target.metadata() = source.metadata();
  target.mask() = source.mask();
  target.raw_psd_blending_ranges() = source.raw_psd_blending_ranges();
  target.raw_psd_group_boundary_blending_ranges() = source.raw_psd_group_boundary_blending_ranges();
  target.unknown_psd_blocks() = source.unknown_psd_blocks();
}

std::vector<Layer> build_group_hierarchy(std::vector<DecodedLayer> flat_layers) {
  struct GroupFrame {
    std::vector<Layer> children;
    std::vector<std::uint8_t> boundary_blending_ranges;
  };

  std::vector<GroupFrame> stack;
  stack.emplace_back();

  for (auto& decoded : flat_layers) {
    if (is_section_divider_boundary(decoded.section_divider_type)) {
      stack.push_back(GroupFrame{{}, std::as_const(decoded.layer).raw_psd_blending_ranges()});
      continue;
    }

    if (is_section_divider_folder(decoded.section_divider_type)) {
      std::vector<Layer> children;
      std::vector<std::uint8_t> boundary_blending_ranges;
      if (stack.size() > 1U) {
        children = std::move(stack.back().children);
        boundary_blending_ranges = std::move(stack.back().boundary_blending_ranges);
        stack.pop_back();
      }

      Layer group(0, decoded.layer.name(), LayerKind::Group);
      copy_layer_state(group, decoded.layer);
      group.raw_psd_group_boundary_blending_ranges() = std::move(boundary_blending_ranges);
      set_layer_group_expanded(group, decoded.section_divider_type == 1U);
      group.children() = std::move(children);
      stack.back().children.push_back(std::move(group));
      continue;
    }

    stack.back().children.push_back(std::move(decoded.layer));
  }

  while (stack.size() > 1U) {
    auto orphaned_children = std::move(stack.back().children);
    stack.pop_back();
    stack.back().children.insert(stack.back().children.end(), std::make_move_iterator(orphaned_children.begin()),
                                 std::make_move_iterator(orphaned_children.end()));
  }

  return std::move(stack.front().children);
}

Layer clone_layer_with_document_ids(Document& document, const Layer& source) {
  Layer cloned = source.kind() == LayerKind::Pixel
                     ? Layer(document.allocate_layer_id(), source.name(), source.pixels())
                     : Layer(document.allocate_layer_id(), source.name(), source.kind());
  copy_layer_state(cloned, source);
  if (source.kind() == LayerKind::Group) {
    for (const auto& child : source.children()) {
      cloned.add_child(clone_layer_with_document_ids(document, child));
    }
  }
  return cloned;
}

std::vector<Layer> read_layers(BigEndianReader& layer_reader, std::int32_t canvas_width, std::int32_t canvas_height,
                               std::uint16_t source_color_mode, float global_light_angle,
                               float global_light_altitude, bool large_document,
                               const CmykToRgbTransform* cmyk_icc,
                               bool& has_merged_transparency) {
  has_merged_transparency = false;
  const auto layer_info_length = large_document
                                     ? read_section_length_u64(layer_reader, "layer info")
                                     : static_cast<std::uint64_t>(read_section_length(layer_reader, "layer info"));
  if (layer_info_length == 0) {
    return {};
  }

  const auto layer_info_end = layer_reader.position() + static_cast<std::size_t>(layer_info_length);
  const auto layer_count_raw = static_cast<std::int16_t>(layer_reader.read_u16());
  has_merged_transparency = layer_count_raw < 0;
  const auto layer_count = static_cast<std::uint16_t>(
      layer_count_raw < 0 ? -static_cast<std::int32_t>(layer_count_raw)
                          : static_cast<std::int32_t>(layer_count_raw));
  const CmykColorConverter cmyk_converter{cmyk_icc};
  std::vector<LayerRecord> records;
  records.reserve(layer_count);
  for (std::uint16_t i = 0; i < layer_count; ++i) {
    records.push_back(read_layer_record(layer_reader, large_document, cmyk_converter));
  }

  std::vector<DecodedLayer> decoded_layers;
  decoded_layers.reserve(layer_count);
  for (const auto& record : records) {
    const auto width = std::max(0, record.bounds.width);
    const auto height = std::max(0, record.bounds.height);
    const auto source_is_cmyk = is_cmyk_color_mode(source_color_mode);
    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto has_color = std::any_of(record.channels.begin(), record.channels.end(), [source_color_mode](LayerChannelInfo channel) {
      return is_source_color_channel(channel.id, source_color_mode);
    });
    const auto has_alpha = std::any_of(record.channels.begin(), record.channels.end(), [](LayerChannelInfo channel) {
      return channel.id == kChannelTransparency;
    });
    PixelBuffer pixels(width, height, (has_alpha || !has_color) ? PixelFormat::rgba8() : PixelFormat::rgb8());
    if (has_alpha) {
      for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
          pixels.pixel(x, y)[3] = 255;
        }
      }
    }
    std::array<std::vector<std::uint8_t>, 4> cmyk_channels;
    if (source_is_cmyk) {
      for (auto& component : cmyk_channels) {
        component.resize(pixel_count, 0);
      }
    }

    std::optional<LayerMask> decoded_mask;
    for (const auto channel : record.channels) {
      if (channel.length == 0) {
        // Old Photoshop writes zero-length channel data (no compression marker at
        // all) for empty layers; treat it as an empty channel.
        continue;
      }
      if (channel.length < 2) {
        throw std::runtime_error("Invalid PSD layer channel length");
      }
      const auto compression = layer_reader.read_u16();
      const auto payload_length = channel.length - 2;
      if (compression != kCompressionRaw && compression != kCompressionRle) {
        layer_reader.skip(payload_length);
        continue;
      }
      const auto channel_start = layer_reader.position();
      const auto channel_width = channel.id == kChannelUserMask && record.mask.has_value()
                                     ? std::max(0, record.mask->bounds.width)
                                     : width;
      const auto channel_height = channel.id == kChannelUserMask && record.mask.has_value()
                                      ? std::max(0, record.mask->bounds.height)
                                      : height;
      const auto channel_pixel_count =
          static_cast<std::size_t>(channel_width) * static_cast<std::size_t>(channel_height);
      if (compression == kCompressionRaw && payload_length < channel_pixel_count) {
        throw std::runtime_error("PSD layer channel data is truncated");
      }
      const auto channel_data =
          read_channel_data(layer_reader, compression, channel_width, channel_height, large_document);
      if (channel.id == kChannelUserMask && record.mask.has_value() && channel_width > 0 && channel_height > 0) {
        PixelBuffer mask_pixels(channel_width, channel_height, PixelFormat::gray8());
        std::copy(channel_data.begin(), channel_data.end(), mask_pixels.data().begin());
        decoded_mask = LayerMask{record.mask->bounds, std::move(mask_pixels), record.mask->default_color,
                                 record.mask->disabled};
      }
      const auto target_channel = channel.id == kChannelRed      ? 0
                                  : channel.id == kChannelGreen  ? 1
                                  : channel.id == kChannelBlue   ? 2
                                  : channel.id == kChannelTransparency ? 3
                                                                       : -1;
      if (source_is_cmyk) {
        if (channel.id <= kChannelBlack && channel_data.size() == pixel_count) {
          cmyk_channels[channel.id] = channel_data;
        } else if (target_channel == 3) {
          for (std::size_t i = 0; i < channel_data.size(); ++i) {
            pixels.data()[i * pixels.format().channels + 3U] = channel_data[i];
          }
        }
      } else {
        for (std::size_t i = 0; i < channel_data.size(); ++i) {
          if (target_channel >= 0 && target_channel < pixels.format().channels) {
            pixels.data()[i * pixels.format().channels + static_cast<std::size_t>(target_channel)] = channel_data[i];
          }
        }
      }
      const auto consumed = layer_reader.position() - channel_start;
      if (payload_length > consumed) {
        layer_reader.skip(payload_length - consumed);
      }
    }
    if (source_is_cmyk) {
      convert_cmyk_planes_to_rgb(pixels, cmyk_channels[0].data(), cmyk_channels[1].data(),
                                 cmyk_channels[2].data(), cmyk_channels[3].data(), pixel_count,
                                 cmyk_icc);
    }

    bool text_placeholder_rendered = false;
    bool text_regenerated_rendered = false;
    if (record.text.has_value() && !has_visible_alpha(pixels)) {
      pixels = render_placeholder_text(*record.text, width, height);
      text_placeholder_rendered = true;
    } else if (should_regenerate_imported_text_preview(record, pixels)) {
      if (auto regenerated = render_regenerated_imported_text_pixels(record, width, height);
          regenerated.has_value() && has_visible_alpha(*regenerated)) {
        pixels = std::move(*regenerated);
        text_regenerated_rendered = true;
      }
    }

    std::optional<AdjustmentSettings> native_adjustment_settings;
    std::optional<AdjustmentSettings> native_curves_settings;
    std::optional<AdjustmentSettings> patchy_adjustment_settings;
    bool has_native_curves = false;
    for (const auto& block : record.additional_blocks) {
      if (block.key == "levl") {
        native_adjustment_settings = parse_photoshop_levels_adjustment(block.payload);
      } else if (block.key == "hue2") {
        if (auto parsed = parse_photoshop_hue2_adjustment(block.payload); parsed.has_value()) {
          native_adjustment_settings = parsed;
        }
      } else if (block.key == "curv") {
        has_native_curves = true;
        if (auto parsed = parse_photoshop_curves_adjustment(block.payload); parsed.has_value()) {
          native_curves_settings = parsed;
        }
      } else if (block.key == "plAD") {
        patchy_adjustment_settings = parse_patchy_adjustment(block.payload);
      }
    }
    // Native Photoshop adjustment data is authoritative over Patchy's private
    // fallback. A valid curv block is editable; an unrecognized one deliberately
    // stays on the opaque regular-layer path so a stale plAD can never shadow it.
    auto adjustment_settings = has_native_curves
                                   ? native_curves_settings
                                   : (native_adjustment_settings.has_value() ? native_adjustment_settings
                                                                             : patchy_adjustment_settings);
    if (adjustment_settings.has_value() && native_adjustment_settings.has_value() &&
        patchy_adjustment_settings.has_value() && adjustment_settings->kind == AdjustmentKind::Levels &&
        patchy_adjustment_settings->kind == AdjustmentKind::Levels) {
      adjustment_settings->levels.channel = patchy_adjustment_settings->levels.channel;
    }

    Layer layer = adjustment_settings.has_value() ? Layer(0, record.name, LayerKind::Adjustment)
                                                  : Layer(0, record.name, std::move(pixels));
    if (adjustment_settings.has_value()) {
      configure_adjustment_layer(layer, *adjustment_settings);
    }
    if (adjustment_settings.has_value()) {
      // Photoshop writes adjustment layer records with an empty rect; Patchy's
      // convention (matching its own authored adjustment layers) is canvas-sized
      // bounds. Empty bounds render fine (the compositor treats them as
      // unbounded) but starve rect-based invalidation - the canvas and the undo
      // render diff would repaint nothing for an adjustment-only change.
      layer.set_bounds(Rect::from_size(canvas_width, canvas_height));
    } else {
      const auto layer_width = std::max(width, layer.pixels().width());
      const auto layer_height = std::max(height, layer.pixels().height());
      layer.set_bounds(Rect{std::clamp(record.bounds.x, -canvas_width, canvas_width * 2),
                            std::clamp(record.bounds.y, -canvas_height, canvas_height * 2), layer_width,
                            layer_height});
    }
    layer.set_blend_mode(record.blend_mode);
    layer.set_opacity(static_cast<float>(record.opacity) / 255.0F);
    layer.set_visible(record.visible);
    layer.raw_psd_blending_ranges() = record.blending_ranges;
    if (record.section_divider_type == 0U) {
      // Divider/folder records never carry the clipping flag into the model, so
      // groups always build unclipped even from stray bytes.
      layer.set_clipped(record.clipping);
    }
    layer.layer_style() = record.layer_style;
    layer.layer_style().layer_mask_hides_effects = record.layer_mask_hides_effects;
    resolve_global_light(layer.layer_style(), global_light_angle, global_light_altitude);
    set_layer_lock_flags(layer,
                         record.protection_flags &
                             (kPsdProtectTransparency | kPsdProtectComposite | kPsdProtectPosition));
    if (decoded_mask.has_value()) {
      layer.set_mask(std::move(*decoded_mask));
      if (record.mask.has_value() && !record.mask->linked) {
        set_layer_mask_linked(layer, false);
      }
    }
    for (auto& block : record.additional_blocks) {
      layer.unknown_psd_blocks().push_back(std::move(block));
    }
    if (record.placed.has_value()) {
      set_layer_smart_object_metadata(layer, record.placed->placement, record.placed->placed_uuid,
                                      record.placed_source_block, record.placed->lock_reason,
                                      kSmartObjectRasterStatusPhotoshop);
      if (record.placed->warp.has_value() && record.placed->lock_reason.empty()) {
        // A supported (re-renderable) warp rides layer metadata so every re-render
        // and regeneration path sees it.
        layer.metadata()[kLayerMetadataSmartObjectWarp] = serialize_smart_object_warp(*record.placed->warp);
      }
    } else if (record.placed_parse_failed) {
      // An unreadable SoLd (e.g. Photoshop's ObAr warp-mesh values): the layer still
      // gets the smart-object badge and edit/move protection ("unparsed", empty
      // uuid), so its pixels can never drift from the verbatim blobs PS re-renders.
      layer.metadata()[kLayerMetadataSmartObject] = "";
      layer.metadata()[kLayerMetadataSmartObjectLock] = "unparsed";
      layer.metadata()[kLayerMetadataSmartObjectRasterStatus] = kSmartObjectRasterStatusPhotoshop;
    }
    if (record.text.has_value()) {
      layer.metadata()[kLayerMetadataText] = *record.text;
      if (record.text_html.has_value()) {
        layer.metadata()[kLayerMetadataTextHtml] = *record.text_html;
      }
      if (record.text_runs.has_value()) {
        layer.metadata()[kLayerMetadataTextRuns] = *record.text_runs;
      }
      if (record.text_paragraph_runs.has_value()) {
        layer.metadata()[kLayerMetadataTextParagraphRuns] = *record.text_paragraph_runs;
      }
      const auto text_box_width = record.text_box.has_value() ? std::max(1, record.text_box->width)
                                                              : std::max(1, layer.bounds().width);
      const auto text_box_height = record.text_box.has_value() ? std::max(1, record.text_box->height)
                                                               : std::max(1, layer.bounds().height);
      layer.metadata()[kLayerMetadataTextFlow] = record.text_box.has_value() ? "box" : "point";
      layer.metadata()[kLayerMetadataTextBoxWidth] = std::to_string(text_box_width);
      layer.metadata()[kLayerMetadataTextBoxHeight] = std::to_string(text_box_height);
      layer.metadata()[kLayerMetadataTextFont] = record.text_font.value_or(std::string("PSD Text"));
      layer.metadata()[kLayerMetadataTextSize] =
          std::to_string(record.text_size.value_or(estimate_text_size_from_alpha(layer.pixels())));
      layer.metadata()[kLayerMetadataTextColor] =
          record.text_color.has_value() ? rgb_hex_color(*record.text_color) : "#000000";
      if (record.text_bold.has_value()) {
        layer.metadata()[kLayerMetadataTextBold] = *record.text_bold ? "true" : "false";
      }
      if (record.text_italic.has_value()) {
        layer.metadata()[kLayerMetadataTextItalic] = *record.text_italic ? "true" : "false";
      }
      if (record.text_anti_alias.has_value()) {
        layer.metadata()[kLayerMetadataTextAntiAlias] = std::to_string(*record.text_anti_alias);
      }
      if (record.text_source_block.has_value()) {
        layer.metadata()[kLayerMetadataTextSourceBlock] = *record.text_source_block;
        layer.metadata()[kLayerMetadataTextRasterStatus] = text_regenerated_rendered ||
                                                                  record.text_patchy_generated_type_block
                                                              ? "patchy_raster"
                                                          : text_placeholder_rendered ? "placeholder"
                                                                                      : "psd_raster_preview";
        // Photoshop-authored type layers re-render with the Photoshop leading model; Patchy's
        // own exported type blocks keep native layout (see kLayerMetadataTextLayoutMode) --
        // EXCEPT converted-then-saved layers, whose regenerated (Patchy-signed) engine data
        // still carries fixed leading/tracking that only a Photoshop import can produce.
        if (record.text_runs.has_value() &&
            (!record.text_patchy_generated_type_block ||
             serialized_runs_have_photoshop_leading_signals(*record.text_runs))) {
          layer.metadata()[kLayerMetadataTextLayoutMode] = kTextLayoutModePhotoshop;
        }
      }
      if (record.text_geometry.has_value()) {
        layer.metadata()[kLayerMetadataTextTransform] = serialize_double_array(record.text_geometry->transform);
        layer.metadata()[kLayerMetadataPsdTextTransform] = serialize_double_array(record.text_geometry->transform);
        layer.metadata()[kLayerMetadataPsdTextBounds] = serialize_text_bounds(record.text_geometry->bounds);
        layer.metadata()[kLayerMetadataPsdTextBoundingBox] = serialize_text_bounds(record.text_geometry->bounding_box);
        layer.metadata()[kLayerMetadataPsdTextBoxBounds] = serialize_text_bounds(record.text_geometry->box_bounds);
        layer.metadata()[kLayerMetadataPsdTextTailBounds] = serialize_int_array(record.text_geometry->tail_bounds);
        layer.metadata()[kLayerMetadataPsdTextIndex] = std::to_string(record.text_geometry->text_index);
        if (record.text_geometry->warp.has_value()) {
          layer.metadata()[kLayerMetadataTextWarp] = serialize_text_warp(*record.text_geometry->warp);
        }
      }
    }
    decoded_layers.push_back(DecodedLayer{std::move(layer), record.section_divider_type});
  }

  if (layer_reader.position() < layer_info_end) {
    layer_reader.skip(layer_info_end - layer_reader.position());
  }
  if ((layer_info_length % 2U) != 0 && layer_reader.remaining() > 0) {
    layer_reader.skip(1);
  }
  return build_group_hierarchy(std::move(decoded_layers));
}

bool read_merged_transparency_flag_and_skip_layer_mask(BigEndianReader& reader,
                                                        std::uint64_t layer_mask_length,
                                                        bool large_document) {
  if (layer_mask_length > reader.remaining()) {
    throw std::runtime_error("Invalid PSD layer and mask information length");
  }
  if (layer_mask_length == 0U) {
    return false;
  }
  const auto prefix_size = large_document ? 8U : 4U;
  if (layer_mask_length < prefix_size) {
    reader.skip(static_cast<std::size_t>(layer_mask_length));
    return false;
  }
  const auto layer_info_length =
      large_document ? reader.read_u64() : static_cast<std::uint64_t>(reader.read_u32());
  std::size_t consumed = prefix_size;
  bool has_merged_transparency = false;
  if (layer_info_length >= 2U && layer_mask_length - consumed >= 2U) {
    has_merged_transparency = static_cast<std::int16_t>(reader.read_u16()) < 0;
    consumed += 2U;
  }
  reader.skip(static_cast<std::size_t>(layer_mask_length) - consumed);
  return has_merged_transparency;
}

}  // namespace

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 4 && bytes[0] == '8' && bytes[1] == 'B' && bytes[2] == 'P' && bytes[3] == 'S';
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, ReadOptions options) {
  BigEndianReader reader(bytes);
  const auto header = read_header(reader);
  const auto format = format_from_header(header);

  skip_length_block(reader, "color mode data");
  auto image_resources = read_length_block(reader, "image resources");
  const auto channel_resources = parse_composite_channel_resources(image_resources);

  Document document(static_cast<std::int32_t>(header.width), static_cast<std::int32_t>(header.height), format);
  document.metadata().raw_psd_image_resources = image_resources;
  if (auto icc_profile = find_image_resource_payload(image_resources, kImageResourceIccProfile);
      header.color_mode == kColorModeRgb && icc_profile.has_value()) {
    document.color_state().embedded_icc_profile = std::move(*icc_profile);
  }
  // CMYK sources convert through the file's embedded ICC profile when one is usable,
  // matching Photoshop; otherwise the naive ink mix is the fallback. The CMYK profile is
  // deliberately NOT promoted into color_state() (it does not describe the converted RGB
  // pixels and is stripped from RGB re-exports).
  std::optional<CmykToRgbTransform> cmyk_icc_transform;
  if (is_cmyk_color_mode(header.color_mode)) {
    if (auto icc_profile = find_image_resource_payload(image_resources, kImageResourceIccProfile);
        icc_profile.has_value()) {
      cmyk_icc_transform = CmykToRgbTransform::from_icc_profile(*icc_profile);
      if (options.notices != nullptr) {
        if (cmyk_icc_transform.has_value()) {
          const auto& description = cmyk_icc_transform->profile_description();
          options.notices->push_back(
              "Converted CMYK colors to RGB using the document's embedded color profile" +
              (description.empty() ? std::string(".") : " '" + description + "'."));
        } else {
          options.notices->push_back(
              "The document's embedded CMYK color profile could not be used; colors were "
              "converted with a basic CMYK-to-RGB formula.");
        }
      }
    }
  }
  const auto* cmyk_icc = cmyk_icc_transform.has_value() ? &*cmyk_icc_transform : nullptr;
  if (auto resolution = find_image_resource_payload(image_resources, kImageResourceResolutionInfo);
      resolution.has_value()) {
    if (auto print_settings = print_settings_from_resolution_resource(*resolution); print_settings.has_value()) {
      document.print_settings() = *print_settings;
    }
  }
  if (auto grid_guides = find_image_resource_payload(image_resources, kImageResourceGridAndGuidesInfo);
      grid_guides.has_value()) {
    if (auto parsed_grid_guides = grid_guides_from_resource(*grid_guides); parsed_grid_guides.has_value()) {
      document.grid_settings() = parsed_grid_guides->first;
      document.guides() = std::move(parsed_grid_guides->second);
    }
  }
  // Document-wide light direction for effects marked "use global light" (signed degrees).
  auto global_light_angle = kDefaultGlobalLightAngle;
  auto global_light_altitude = kDefaultGlobalLightAltitude;
  if (auto angle = find_image_resource_payload(image_resources, kImageResourceGlobalLightAngle);
      angle.has_value() && angle->size() >= 4U) {
    global_light_angle = static_cast<float>(static_cast<std::int32_t>(BigEndianReader(*angle).read_u32()));
  }
  if (auto altitude = find_image_resource_payload(image_resources, kImageResourceGlobalLightAltitude);
      altitude.has_value() && altitude->size() >= 4U) {
    global_light_altitude = static_cast<float>(static_cast<std::int32_t>(BigEndianReader(*altitude).read_u32()));
  }
  const auto layer_mask_length =
      header.large_document
          ? read_section_length_u64(reader, "layer and mask information")
          : static_cast<std::uint64_t>(read_section_length(reader, "layer and mask information"));
  bool has_merged_transparency = false;
  if (options.prefer_flat_composite) {
    has_merged_transparency =
        read_merged_transparency_flag_and_skip_layer_mask(reader, layer_mask_length, header.large_document);

    auto metadata = std::move(document.metadata());
    auto color_state = std::move(document.color_state());
    auto print_settings = document.print_settings();
    auto grid_settings = document.grid_settings();
    auto guides = std::move(document.guides());
    document = read_flat_composite(reader, header, cmyk_icc, channel_resources,
                                   has_merged_transparency);
    document.metadata() = std::move(metadata);
    document.color_state().embedded_icc_profile = std::move(color_state.embedded_icc_profile);
    document.color_state().ocio_view = std::move(color_state.ocio_view);
    document.print_settings() = print_settings;
    document.grid_settings() = grid_settings;
    document.guides() = std::move(guides);
    document.metadata().values["psd.version"] = header.large_document ? "PSB" : "PSD";
    document.metadata().values["psd.color_mode"] = color_mode_name(header.color_mode);
    if (auto palette = find_image_resource_payload(image_resources, kImageResourcePatchyPalette);
        palette.has_value()) {
      apply_patchy_palette_resource(document, *palette);
    }
    return document;
  }

  if (layer_mask_length > 0) {
    auto layer_mask_payload = reader.read_bytes(static_cast<std::size_t>(layer_mask_length));
    BigEndianReader layer_reader(layer_mask_payload);
    auto layers = read_layers(layer_reader, document.width(), document.height(), header.color_mode,
                              global_light_angle, global_light_altitude, header.large_document, cmyk_icc,
                              has_merged_transparency);
    const auto add_layer = [&document](const Layer& source) {
      document.add_layer(clone_layer_with_document_ids(document, source));
    };

    // Photoshop stores layer records bottom-to-top. Older Patchy builds wrote
    // them top-to-bottom, which is detectable when a full Background record is last.
    if (records_look_like_legacy_top_to_bottom(layers, document.width(), document.height())) {
      for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        add_layer(*it);
      }
    } else {
      for (const auto& source : layers) {
        add_layer(source);
      }
    }

    // Preserve what follows the layer info: the global layer mask info and the
    // document-global tagged blocks. Dropping these orphans smart-object layers —
    // their 'PlLd'/'SoLd' blocks reference embedded sources stored in the global
    // 'lnk2' block, and Photoshop can open but no longer save a file where those
    // references dangle.
    if (layer_reader.remaining() >= 4U) {
      const auto global_mask_length = layer_reader.read_u32();
      if (global_mask_length <= layer_reader.remaining()) {
        document.metadata().raw_psd_global_layer_mask_info = layer_reader.read_bytes(global_mask_length);
      }
    }
    std::size_t global_block_index = 0;
    while (layer_reader.remaining() >= 12U) {
      const auto block_signature = read_signature(layer_reader);
      if (block_signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
          block_signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
        break;
      }
      const auto block_key = read_signature(layer_reader);
      const auto key = std::string(block_key.begin(), block_key.end());
      // Same width rule as the per-layer walk: '8B64' signature OR (PSB and the key
      // is in the documented 8-byte set). PS 2023 writes 'lnk2' as '8BIM' + u64.
      const bool wide_length = block_signature == std::array<char, 4>{'8', 'B', '6', '4'} ||
                               (header.large_document && tagged_block_length_is_u64(key));
      if (wide_length && layer_reader.remaining() < 8U) {
        break;
      }
      const auto block_length =
          wide_length ? layer_reader.read_u64() : static_cast<std::uint64_t>(layer_reader.read_u32());
      if (block_length > layer_reader.remaining()) {
        break;
      }
      auto payload = layer_reader.read_bytes(static_cast<std::size_t>(block_length));
      if (key.rfind("lnk", 0) == 0 || key.rfind("Lnk", 0) == 0) {
        // Smart-object source blocks parse into the store (payloads shared_ptr-held so
        // undo snapshots stop duplicating embedded files); unparseable ones stay opaque.
        SmartObjectLinkBlock link_block;
        link_block.key = key;
        link_block.long_length = wide_length;
        link_block.original_global_index = global_block_index;
        link_block.original_payload = std::make_shared<const std::vector<std::uint8_t>>(payload);
        if (auto sources = parse_linked_layer_block(payload); sources.has_value()) {
          link_block.sources = std::move(*sources);
        } else {
          link_block.opaque = true;
        }
        document.metadata().smart_objects.blocks.push_back(std::move(link_block));
      } else {
        document.metadata().unknown_psd_resources.push_back(UnknownPsdBlock{key, std::move(payload), wide_length});
      }
      ++global_block_index;
      // Global tagged blocks are padded to 4-byte boundaries.
      const auto padding = (4U - (block_length % 4U)) % 4U;
      layer_reader.skip(std::min<std::size_t>(static_cast<std::size_t>(padding), layer_reader.remaining()));
    }
  }

  if (document.layers().empty()) {
    auto metadata = std::move(document.metadata());
    auto color_state = std::move(document.color_state());
    auto print_settings = document.print_settings();
    auto grid_settings = document.grid_settings();
    auto guides = std::move(document.guides());
    document = read_flat_composite(reader, header, cmyk_icc, channel_resources,
                                   has_merged_transparency);
    document.metadata() = std::move(metadata);
    document.color_state().embedded_icc_profile = std::move(color_state.embedded_icc_profile);
    document.color_state().ocio_view = std::move(color_state.ocio_view);
    document.print_settings() = print_settings;
    document.grid_settings() = grid_settings;
    document.guides() = std::move(guides);
  } else {
    // The editable layer data is authoritative. Decode RGB only for the optional
    // first-paint cache; when only saved channels are needed, raw color planes and
    // encoded RLE rows are skipped without allocating a base composite.
    const auto color_channel_count = composite_color_channel_count(header.color_mode);
    const auto first_saved_channel = static_cast<std::uint16_t>(
        color_channel_count + (has_merged_transparency ? 1U : 0U));
    if (first_saved_channel > header.channels) {
      throw std::runtime_error("PSD merged transparency flag has no matching composite channel");
    }
    const auto saved_channel_count = static_cast<std::size_t>(header.channels - first_saved_channel);
    if (options.retain_flat_composite) {
      try {
        auto flat_composite = read_flat_composite(reader, header, cmyk_icc, channel_resources,
                                                  has_merged_transparency);
        if (!flat_composite.layers().empty() && flat_composite.layers().front().kind() == LayerKind::Pixel) {
          document.metadata().psd_flat_composite =
              std::as_const(flat_composite).layers().front().pixels();
          for (auto& channel : flat_composite.channels()) {
            document.add_channel(std::move(channel));
          }
        }
      } catch (const std::exception&) {
        document.metadata().psd_flat_composite.reset();
        if (saved_channel_count != 0U) {
          throw;
        }
      }
    } else if (saved_channel_count != 0U) {
      if (reader.remaining() < 2U) {
        throw std::runtime_error("PSD composite image data is missing");
      }
      const auto compression = reader.read_u16();
      auto saved_channels =
          read_flat_image_channels_from(reader, header, compression, first_saved_channel);
      add_saved_composite_channels(document, std::move(saved_channels), first_saved_channel, header,
                                   channel_resources);
    }
  }

  SmartObjectImportCounts smart_object_counts;
  finalize_smart_object_layers(document.layers(), document.metadata().smart_objects, smart_object_counts);
  append_smart_object_notices(smart_object_counts, options.notices);

  document.metadata().values["psd.version"] = header.large_document ? "PSB" : "PSD";
  document.metadata().values["psd.color_mode"] = color_mode_name(header.color_mode);
  if (auto palette = find_image_resource_payload(image_resources, kImageResourcePatchyPalette);
      palette.has_value()) {
    apply_patchy_palette_resource(document, *palette);
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, ReadOptions options) {
  const auto bytes = read_file_bytes(path);
  return read(bytes, options);
}

std::vector<std::uint8_t> DocumentIo::write_flat_rgb8(const Document& document, WriteOptions options) {
  check_write_dimensions(document, options.large_document);

  auto composite = document_alpha_composite(document);
  if (!composite.has_value()) {
    composite = merged_flatten_composite(document);
    // A flat file has no layer section to carry the spec's negative-layer-count
    // "merged transparency" flag, so its alpha exports under the saved-channel
    // convention instead; the flat reader imports "Alpha 1" as a DocumentChannel.
    if (!composite->channel_name.empty()) {
      composite->channel_name = "Alpha 1";
    }
  }

  std::vector<std::span<const std::uint8_t>> extra_channels;
  std::vector<CompositeChannelInfo> channel_info;
  if (!composite->channel_name.empty()) {
    extra_channels.emplace_back(composite->alpha);
    channel_info.push_back(CompositeChannelInfo{composite->channel_name, false, true, std::nullopt,
                                                DocumentChannelDisplayInfo{}, {}});
  }
  append_document_channels_for_write(document, extra_channels, channel_info);
  check_composite_channel_limit(extra_channels.size());

  BigEndianWriter writer;
  write_header(writer, Header{options.large_document,
                              static_cast<std::uint16_t>(3U + extra_channels.size()),
                              static_cast<std::uint32_t>(composite->rgb.height()),
                              static_cast<std::uint32_t>(composite->rgb.width()),
                              8,
                              kColorModeRgb});

  writer.write_u32(0);  // Color mode data section.
  write_length_prefixed_block(writer, image_resources_for_document(document, channel_info));
  if (options.large_document) {
    writer.write_u64(0);  // Layer and mask information section.
  } else {
    writer.write_u32(0);  // Layer and mask information section.
  }
  if (!extra_channels.empty()) {
    write_rgb8_image_data_with_extra_channels(writer, composite->rgb, extra_channels,
                                              options.large_document);
  } else {
    write_rgb8_image_data(writer, composite->rgb, options.large_document);
  }

  return writer.bytes();
}

void DocumentIo::write_flat_rgb8_file(const Document& document, const std::filesystem::path& path,
                                      WriteOptions options) {
  const auto bytes = write_flat_rgb8(document, options);
  write_file_bytes(path, bytes);
}

std::vector<std::uint8_t> DocumentIo::write_layered_rgb8(const Document& document, WriteOptions options) {
  check_write_dimensions(document, options.large_document);

  auto composite = merged_flatten_composite(document);

  const bool merged_transparency_channel = composite.channel_name == "Transparency";
  std::vector<std::span<const std::uint8_t>> extra_channels;
  std::vector<CompositeChannelInfo> channel_info;
  if (!composite.channel_name.empty()) {
    DocumentChannelDisplayInfo display_info;
    if (merged_transparency_channel) {
      display_info.opacity = 1.0F;
    }
    extra_channels.emplace_back(composite.alpha);
    channel_info.push_back(CompositeChannelInfo{composite.channel_name, merged_transparency_channel,
                                                !merged_transparency_channel, std::nullopt,
                                                display_info, {}});
  }
  append_document_channels_for_write(document, extra_channels, channel_info);
  check_composite_channel_limit(extra_channels.size());

  std::vector<EncodedLayer> encoded_layers;
  encoded_layers.reserve(document.layers().size());
  // Photoshop stores layer records in stack order from bottom to top. Patchy's
  // document model uses the same order, so write it directly instead of reversing.
  for (const auto& layer : document.layers()) {
    append_encoded_layers(layer, encoded_layers, options.large_document);
  }

  BigEndianWriter layer_info;
  // A NEGATIVE layer count is the spec's "first alpha channel contains the merged
  // transparency" flag: without it Photoshop surfaces the composite's 4th channel as
  // a phantom saved channel named "Transparency" in the Channels panel (PS's own
  // transparent-canvas files write the negative form, verified July 2026 via COM
  // channel counts). Saved document channels follow this derived plane.
  const auto layer_count = static_cast<std::int16_t>(encoded_layers.size());
  if (merged_transparency_channel && layer_count == 0) {
    throw std::runtime_error("A layered PSD needs a layer record to identify merged transparency");
  }
  layer_info.write_u16(static_cast<std::uint16_t>(merged_transparency_channel ? -layer_count : layer_count));
  const auto& global_blocks = document.metadata().unknown_psd_resources;
  const auto has_smart_object_sources =
      !document.metadata().smart_objects.blocks.empty() ||
      std::any_of(global_blocks.begin(), global_blocks.end(), [](const UnknownPsdBlock& block) {
        return block.key.rfind("lnk", 0) == 0 || block.key.rfind("Lnk", 0) == 0;
      });
  for (const auto& encoded : encoded_layers) {
    write_layer_record(layer_info, encoded, !has_smart_object_sources, options.large_document);
  }
  for (const auto& encoded : encoded_layers) {
    for (const auto& channel : encoded.channels) {
      layer_info.write_u16(channel.compression);
      layer_info.write_bytes(channel.data);
    }
  }
  // The layer info section is rounded up to an even length; Photoshop's own PSB output
  // pads to 2 as well (verified against a PS 2026 PSB), not to 4.
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  BigEndianWriter layer_mask;
  if (options.large_document) {
    layer_mask.write_u64(layer_info.bytes().size());
    layer_mask.write_bytes(layer_info.bytes());
  } else {
    write_length_prefixed_block(layer_mask, layer_info.bytes());
  }
  write_length_prefixed_block(layer_mask, document.metadata().raw_psd_global_layer_mask_info);
  // Re-interleave the smart-object source blocks (parsed into the store on read) with
  // the preserved unknown blocks in their original file order; authored store blocks
  // (original_global_index == SIZE_MAX) land after everything else.
  const auto emit_global_payload = [&layer_mask, &options](std::string_view key_text,
                                                           std::span<const std::uint8_t> payload,
                                                           bool long_length) {
    const auto key = block_key_from_string(key_text);
    if (!key.has_value()) {
      return;
    }
    write_additional_layer_block(layer_mask, *key, payload, options.large_document, long_length);
    // Global tagged blocks are padded to 4-byte boundaries.
    const auto padding = (4U - (payload.size() % 4U)) % 4U;
    for (std::size_t i = 0; i < padding; ++i) {
      layer_mask.write_u8(0);
    }
  };
  {
    const auto& store = document.metadata().smart_objects;
    std::vector<std::vector<std::uint8_t>> link_payloads(store.blocks.size());
    std::vector<bool> link_emitted(store.blocks.size(), false);
    for (std::size_t i = 0; i < store.blocks.size(); ++i) {
      link_payloads[i] = serialize_linked_layer_block(store.blocks[i]);
    }
    std::size_t unknown_cursor = 0;
    const auto total_positions = store.blocks.size() + global_blocks.size();
    for (std::size_t position = 0; position < total_positions; ++position) {
      bool emitted_link = false;
      for (std::size_t i = 0; i < store.blocks.size(); ++i) {
        if (!link_emitted[i] && store.blocks[i].original_global_index == position) {
          emit_global_payload(store.blocks[i].key, link_payloads[i], store.blocks[i].long_length);
          link_emitted[i] = true;
          emitted_link = true;
          break;
        }
      }
      if (!emitted_link && unknown_cursor < global_blocks.size()) {
        const auto& block = global_blocks[unknown_cursor++];
        emit_global_payload(block.key, block.payload, block.long_length);
      }
    }
    for (std::size_t i = 0; i < store.blocks.size(); ++i) {
      if (!link_emitted[i]) {
        emit_global_payload(store.blocks[i].key, link_payloads[i], store.blocks[i].long_length);
      }
    }
  }

  BigEndianWriter writer;
  write_header(writer, Header{options.large_document,
                              static_cast<std::uint16_t>(3U + extra_channels.size()),
                              static_cast<std::uint32_t>(document.height()),
                              static_cast<std::uint32_t>(document.width()),
                              8,
                              kColorModeRgb});
  writer.write_u32(0);
  write_length_prefixed_block(writer, image_resources_for_document(document, channel_info));
  if (options.large_document) {
    writer.write_u64(layer_mask.bytes().size());
    writer.write_bytes(layer_mask.bytes());
  } else {
    write_length_prefixed_block(writer, layer_mask.bytes());
  }
  if (!extra_channels.empty()) {
    write_rgb8_image_data_with_extra_channels(writer, composite.rgb, extra_channels,
                                              options.large_document);
  } else {
    write_rgb8_image_data(writer, composite.rgb, options.large_document);
  }
  return writer.bytes();
}

void DocumentIo::write_layered_rgb8_file(const Document& document, const std::filesystem::path& path,
                                         WriteOptions options) {
  const auto bytes = write_layered_rgb8(document, options);
  write_file_bytes(path, bytes);
}

}  // namespace patchy::psd
