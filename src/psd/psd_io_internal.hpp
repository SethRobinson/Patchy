#pragma once

// Constants, structs, and helper declarations shared by the psd_*.cpp split of
// the PSD codec (psd_document_io.cpp, psd_io_common.cpp, psd_channel_data.cpp,
// psd_adjustments.cpp, psd_image_resources.cpp). Helpers used by more than one
// of those translation units are promoted out of the per-file anonymous
// namespaces into this header. Internal to the codec implementation - do not
// include this from outside src/psd.

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/text_warp.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_smart_objects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::psd {

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
// Invert carries no settings: Photoshop writes the block with an empty payload.
constexpr std::array<char, 4> kPhotoshopInvertBlockKey{'n', 'v', 'r', 't'};
// Posterize and Threshold are 4 bytes each: u16 value + 2 zero pad bytes
// (pinned by PS 2026 captures: photoshop-posterize.psd levels 6 = 00 06 00 00,
// photoshop-threshold.psd level 96 = 00 60 00 00).
constexpr std::array<char, 4> kPhotoshopPosterizeBlockKey{'p', 'o', 's', 't'};
constexpr std::array<char, 4> kPhotoshopThresholdBlockKey{'t', 'h', 'r', 's'};
// Brightness/Contrast: legacy-mode PS 2026 writes ONLY the 8-byte 'brit'
// (brightness i16, contrast i16, mean u16 = 127, lab u8 = 0, pad u8 = 0);
// modern mode writes an all-zero 'brit' plus a 'CgEd' descriptor (u32 version
// 16, class "null", items Vrsn=1, Brgh, Cntr, means=127, "Lab "=false,
// useLegacy, Auto=false). A parseable CgEd is authoritative over brit.
constexpr std::array<char, 4> kPhotoshopBrightnessContrastBlockKey{'b', 'r', 'i', 't'};
constexpr std::array<char, 4> kPhotoshopBrightnessContrastDescriptorBlockKey{'C', 'g', 'E', 'd'};
// version u16, colorize u8, pad u8, colorize h/s/l i16 x3, master h/s/l i16 x3.
constexpr std::size_t kPhotoshopHueSaturationHeaderSize = 16;
constexpr int kMaxTextSizePixels = 8192;
constexpr std::uint32_t kPsdProtectTransparency = 1U << 0U;
constexpr std::uint32_t kPsdProtectComposite = 1U << 1U;
constexpr std::uint32_t kPsdProtectPosition = 1U << 2U;
// Photoshop's PSD/PSB dimension caps; a document over the PSD cap must be saved as PSB.
constexpr std::int32_t kMaxPsdDimension = 30000;
constexpr std::int32_t kMaxPsbDimension = 300000;

[[nodiscard]] bool tagged_block_length_is_u64(std::string_view key) noexcept;

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
  std::optional<std::uint8_t> fill_opacity;
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
  // True once 'lmfx' supplied the style: the multi-instance block is
  // authoritative over the single-instance compatibility lfx2 beside it.
  bool layer_style_from_lmfx{false};
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

struct DocumentAlphaComposite {
  PixelBuffer rgb;                  // canvas-sized RGB8 (unmasked, original colors)
  std::vector<std::uint8_t> alpha;  // canvas-sized grayscale, row-major
  std::string_view channel_name;    // 1006 label: "Alpha 1" (saved channel) or "Transparency"
};

// Naive CMYK ink mix used when no usable ICC profile is present (definition in
// psd_channel_data.cpp; see CmykColorConverter below).
RgbColor rgb_from_cmyk_ink_fractions(double cyan, double magenta, double yellow, double black);

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

// Generic byte-level plumbing shared across the split TUs (definitions in
// psd_io_common.cpp).
std::uint32_t checked_u32(std::size_t value, const char* field);
std::uint16_t checked_u16(std::size_t value, const char* field);
void check_write_dimensions(const Document& document, bool large_document);
void skip_length_block(BigEndianReader& reader, const char* section_name);
std::vector<std::uint8_t> read_length_block(BigEndianReader& reader, const char* section_name);
PixelFormat format_from_header(const Header& header);
std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path);
void write_file_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
std::uint32_t read_section_length(BigEndianReader& reader, const char* section_name);
std::uint64_t read_section_length_u64(BigEndianReader& reader, const char* section_name);
std::uint32_t write_length_prefixed_block(BigEndianWriter& writer, const std::vector<std::uint8_t>& payload);
void write_signature(BigEndianWriter& writer, const std::array<char, 4>& signature);
bool is_source_color_channel(std::uint16_t channel_id, std::uint16_t source_color_mode) noexcept;
std::string read_pascal_string(BigEndianReader& reader, std::size_t padded_multiple);
void write_pascal_string(BigEndianWriter& writer, const std::string& value, std::size_t padded_multiple);
std::vector<std::uint16_t> utf8_to_utf16(std::string_view text);
std::optional<std::string> read_unicode_string_payload(std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> unicode_string_payload(std::string_view text);
#ifdef _WIN32
std::wstring wide_from_utf8(std::string_view text);
std::string utf8_from_wide(std::wstring_view text);
#endif
std::array<char, 4> blend_mode_key(BlendMode mode);
BlendMode blend_mode_from_key(const std::array<char, 4>& key);
BlendMode blend_mode_from_descriptor_enum(std::string_view value, const std::array<char, 4>& fallback_key);
std::optional<std::array<char, 4>> block_key_from_string(std::string_view key);
void write_additional_layer_block(BigEndianWriter& writer, const std::array<char, 4>& key,
                                  std::span<const std::uint8_t> payload, bool large_document,
                                  bool force_wide = false);

// Generic descriptor-writing primitives (definitions in psd_io_common.cpp).
void write_descriptor_item_header(BigEndianWriter& writer, std::string_view key, const std::array<char, 4>& type);
void write_descriptor_enum_item(BigEndianWriter& writer, std::string_view key, std::string_view enum_type,
                                std::string_view enum_value);
void write_descriptor_bool_item(BigEndianWriter& writer, std::string_view key, bool value);
void write_descriptor_long_item(BigEndianWriter& writer, std::string_view key, std::int32_t value);
void write_descriptor_double_item(BigEndianWriter& writer, std::string_view key, double value);
void write_descriptor_unit_float_item(BigEndianWriter& writer, std::string_view key, const std::array<char, 4>& unit,
                                      double value);
void write_descriptor_unit_float_item(BigEndianWriter& writer, std::string_view key, double value);
void write_descriptor_object_header(BigEndianWriter& writer, std::string_view name, std::string_view class_id,
                                    std::uint32_t item_count);
void write_descriptor_raw_item(BigEndianWriter& writer, std::string_view key, std::span<const std::uint8_t> payload);
void write_descriptor_object_item(BigEndianWriter& writer, std::string_view key, double left, double top,
                                  double right, double bottom);
void write_descriptor_object_item(BigEndianWriter& writer, std::string_view key, const PsdTextBoundsD& bounds);
void write_descriptor_text_item(BigEndianWriter& writer, std::string_view key, std::string_view text);

// Channel/composite image-data codec helpers (definitions in psd_channel_data.cpp).
EncodedChannel encode_channel(std::uint16_t id, std::int32_t width, std::int32_t height,
                              std::span<const std::uint8_t> raw_data, bool wide_rle_counts);
void write_rgb8_image_data(BigEndianWriter& writer, const PixelBuffer& pixels, bool wide_rle_counts);
[[nodiscard]] std::optional<DocumentAlphaComposite> document_alpha_composite(const Document& document);
[[nodiscard]] DocumentAlphaComposite merged_flatten_composite(const Document& document);
void write_rgb8_image_data_with_extra_channels(
    BigEndianWriter& writer, const PixelBuffer& pixels,
    std::span<const std::span<const std::uint8_t>> extra_channels, bool wide_rle_counts);
// Rebuilds an embedded PSD/PSB whose merged-composite RLE rows include odd byte
// counts, splitting one literal per odd row (identical decode). Photoshop's
// smart-object embed parser rejects odd composite rows outright; see the
// make_packbits_row_even note in psd_channel_data.cpp. Returns nullopt when the
// bytes are not an 8-bit RLE-composite PSD/PSB or are already compliant.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> even_composite_rows_normalized(
    std::span<const std::uint8_t> file_bytes);
std::vector<std::uint8_t> read_channel_data(BigEndianReader& reader, std::uint16_t compression, std::int32_t width,
                                            std::int32_t height, bool wide_rle_counts);
std::vector<std::vector<std::uint8_t>> read_flat_image_channels(BigEndianReader& reader, const Header& header,
                                                                std::uint16_t compression);
std::vector<std::vector<std::uint8_t>> read_flat_image_channels_from(
    BigEndianReader& reader, const Header& header, std::uint16_t compression,
    std::uint16_t first_channel);
bool is_cmyk_color_mode(std::uint16_t color_mode) noexcept;
void convert_cmyk_planes_to_rgb(PixelBuffer& pixels, const std::uint8_t* cyan,
                                const std::uint8_t* magenta, const std::uint8_t* yellow,
                                const std::uint8_t* black, std::size_t pixel_count,
                                const CmykToRgbTransform* icc);

// Adjustment-layer codec: the Photoshop levl/curv/hue2 payloads and the private
// plAD block (definitions in psd_adjustments.cpp). hue2 payloads patch in place
// and curv payloads preserve imported bytes exactly - persistence contracts.
void write_i32(BigEndianWriter& writer, int value);
int read_i32(BigEndianReader& reader);
std::vector<std::uint8_t> photoshop_levels_payload(LevelsAdjustment settings);
std::optional<AdjustmentSettings> parse_photoshop_levels_adjustment(std::span<const std::uint8_t> payload);
std::optional<AdjustmentSettings> parse_photoshop_hue2_adjustment(std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> photoshop_hue2_payload(const HueSaturationAdjustment& settings,
                                                 const UnknownPsdBlock* original);
std::optional<AdjustmentSettings> parse_photoshop_curves_adjustment(
    std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> photoshop_curves_payload(const CurvesAdjustment& curves,
                                                   const UnknownPsdBlock* original);
std::optional<AdjustmentSettings> parse_photoshop_posterize_adjustment(std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> photoshop_posterize_payload(const PosterizeAdjustment& settings,
                                                      const UnknownPsdBlock* original);
std::optional<AdjustmentSettings> parse_photoshop_threshold_adjustment(std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> photoshop_threshold_payload(const ThresholdAdjustment& settings,
                                                      const UnknownPsdBlock* original);
std::optional<AdjustmentSettings> parse_photoshop_brightness_contrast_adjustment(
    std::span<const std::uint8_t> payload);
struct BrightnessContrastDescriptorParse {
  AdjustmentSettings settings;
  bool use_legacy{false};
};
std::optional<BrightnessContrastDescriptorParse> parse_photoshop_brightness_contrast_descriptor(
    std::span<const std::uint8_t> payload);
// Re-emits the imported 'brit' bytes when the layer's settings still match the
// imported state (CgEd authoritative); regenerates the legacy 8-byte shape on
// a real edit. `layer` provides the preserved original blocks.
std::vector<std::uint8_t> photoshop_brightness_contrast_payload(const BrightnessContrastAdjustment& settings,
                                                                const Layer& layer);
// True when a Brightness/Contrast edit must drop the preserved 'CgEd' block
// (a stale descriptor would win over the regenerated 'brit' in Photoshop).
[[nodiscard]] bool brightness_contrast_descriptor_is_stale(const Layer& layer);
std::vector<std::uint8_t> patchy_adjustment_payload(const Layer& layer);
std::optional<AdjustmentSettings> parse_patchy_adjustment(std::span<const std::uint8_t> payload);
// plAD's kind byte only encodes the original v4 kinds; old builds read unknown
// values as Levels, so newer kinds must never be written into plAD.
[[nodiscard]] bool patchy_plad_supports_kind(AdjustmentKind kind);

// Layer-style codecs: lfx2/lrFX parse, global-light resolution, and the private
// plFX block (definitions in psd_layer_styles.cpp). The public lfx2 write API
// shared with the .asl codec is declared in psd/psd_layer_effects.hpp.
void write_f32(BigEndianWriter& writer, float value);
LayerStyle parse_lfx2_layer_style(std::span<const std::uint8_t> payload,
                                  const CmykColorConverter& cmyk);
LayerStyle parse_lrfx_layer_style(std::span<const std::uint8_t> payload);
void resolve_global_light(LayerStyle& style, float angle_degrees, float altitude_degrees);
void merge_missing_layer_style_effects(LayerStyle& target, LayerStyle source);
std::optional<LayerStyle> parse_patchy_layer_style(std::span<const std::uint8_t> payload);

// Layer-record codec: the per-layer record read (bounds/channels/blend/flags/
// mask/blending-ranges/name and the tagged-block walk) and the write/encode
// pipeline for the layer info section (definitions in psd_layer_records.cpp).
LayerRecord read_layer_record(BigEndianReader& reader, bool large_document,
                              const CmykColorConverter& cmyk);
// synthesized_photoshop_layer_id: nonzero writes a fresh 'lyid' block for a
// smart-object layer that has none preserved (see write_layer_record).
void write_layer_record(BigEndianWriter& writer, const EncodedLayer& encoded, bool strip_smart_object_blocks,
                        bool large_document, std::uint32_t synthesized_photoshop_layer_id);
void append_encoded_layers(const Layer& layer, std::vector<EncodedLayer>& encoded_layers, bool large_document);

// Image-resources (8BIM) section codec (definitions in psd_image_resources.cpp).
std::optional<std::vector<std::uint8_t>> find_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id);
ParsedCompositeChannelResources parse_composite_channel_resources(
    std::span<const std::uint8_t> image_resources);
std::uint16_t composite_color_channel_count(std::uint16_t color_mode) noexcept;
void add_saved_composite_channels(Document& document,
                                  std::vector<std::vector<std::uint8_t>> channel_planes,
                                  std::uint16_t first_saved_channel, const Header& header,
                                  const ParsedCompositeChannelResources& resources);
std::optional<DocumentPrintSettings> print_settings_from_resolution_resource(std::span<const std::uint8_t> payload);
std::optional<std::pair<DocumentGridSettings, std::vector<DocumentGuide>>>
grid_guides_from_resource(std::span<const std::uint8_t> payload);
void apply_patchy_palette_resource(Document& document, std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> image_resources_for_document(const Document& document,
                                                       std::span<const CompositeChannelInfo> channels);


// Engine-data (TySh) text READ codec: engine-data parsing, run serialization
// (runs metadata v1-v3 are persistence contracts), the placeholder preview
// renderer, and TySh descriptor-geometry extraction (definitions in
// psd_text_read.cpp).
std::optional<std::string> extract_engine_data_text(std::span<const std::uint8_t> payload);
std::optional<int> extract_engine_data_font_size(std::span<const std::uint8_t> payload);
std::optional<RgbColor> extract_engine_data_fill_color(std::span<const std::uint8_t> payload,
                                                       const CmykColorConverter& cmyk);
std::optional<int> extract_engine_data_anti_alias(std::span<const std::uint8_t> payload);
std::string rgb_hex_color(RgbColor color);
std::optional<RgbColor> rgb_color_from_hex(std::string_view text);
std::string percent_decode(std::string_view text);
std::optional<std::vector<PsdTextStyleRun>> extract_engine_text_runs(std::span<const std::uint8_t> payload,
                                                                     std::string_view text,
                                                                     int fallback_size,
                                                                     RgbColor fallback_color,
                                                                     const CmykColorConverter& cmyk);
std::optional<std::vector<PsdTextParagraphRun>> extract_engine_paragraph_runs(std::span<const std::uint8_t> payload,
                                                                              std::string_view text);
std::string serialize_paragraph_metric(double value);
std::string serialize_patchy_text_runs(std::span<const PsdTextStyleRun> runs);
std::string serialize_patchy_paragraph_runs(std::span<const PsdTextParagraphRun> runs);
std::string html_from_text_runs(std::string_view text, std::span<const PsdTextStyleRun> runs,
                                std::span<const PsdTextParagraphRun> paragraph_runs = {});
PixelBuffer render_placeholder_text(std::string_view text, std::int32_t width, std::int32_t height);
bool has_visible_alpha(const PixelBuffer& pixels);
std::optional<Rect> visible_pixel_local_bounds(const PixelBuffer& pixels);
std::optional<PsdTextBoundsD> visible_text_local_bounds_from_layer_pixels(const Layer& layer, const Rect& visible,
                                                                          const std::array<double, 6>& transform);
int estimate_text_size_from_alpha(const PixelBuffer& pixels);
std::optional<PsdTextGeometry> extract_type_tool_geometry(std::span<const std::uint8_t> payload);
std::optional<Rect> extract_type_tool_text_box(std::span<const std::uint8_t> payload);

// Text write-prep and TySh generation: metadata field serialization, the
// imported-text preview regeneration, and the generated engine-data/TySh
// type-tool payload (definitions in psd_text_write.cpp).
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
bool serialized_runs_have_photoshop_leading_signals(std::string_view runs_text);
std::string serialize_text_bounds(const PsdTextBoundsD& bounds);
std::string serialize_int_array(const std::array<int, 4>& values);
bool should_regenerate_imported_text_preview(const LayerRecord& record, const PixelBuffer& pixels);
std::optional<PixelBuffer> render_regenerated_imported_text_pixels(const LayerRecord& record,
                                                                   std::int32_t width,
                                                                   std::int32_t height);
std::optional<std::vector<std::uint8_t>> photoshop_type_tool_payload_for_layer(const Layer& layer,
                                                                               const Rect& bounds);
bool should_write_generated_text_block(const EncodedLayer& encoded);
}  // namespace patchy::psd
