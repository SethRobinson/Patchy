#include "psd/asl_io.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace patchy::psd {

namespace {

constexpr std::size_t kMaxAslBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaxStyleCount = 4096;
constexpr std::size_t kMaxStyleIdBytes = 255;

constexpr std::array<const char*, 4> kBlendIfChannelIds{"Gry ", "Rd  ", "Grn ", "Bl  "};

// Resolves Photoshop's ZString localization form "$$$/Some/Key=Display Name"
// to its display text; plain names pass through unchanged.
[[nodiscard]] std::string resolve_zstring_name(const std::string& name) {
  if (!name.starts_with("$$$/")) {
    return name;
  }
  const auto equals = name.find('=');
  if (equals == std::string::npos || equals + 1U >= name.size()) {
    const auto slash = name.rfind('/');
    return slash == std::string::npos ? name : name.substr(slash + 1U);
  }
  return name.substr(equals + 1U);
}

[[nodiscard]] std::string descriptor_string(const DescriptorObject& object, std::string_view key) {
  const auto* value = descriptor_value(object, key);
  return value != nullptr && value->type == DescriptorValue::Type::String ? value->string_value
                                                                          : std::string();
}

[[nodiscard]] std::uint8_t clamp_range_byte(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

// One 'Blnd' list entry -> the channel index it addresses, or -1 when the
// channel reference is missing or names a channel Patchy does not model.
[[nodiscard]] int blend_if_channel_index(const DescriptorObject& range) {
  const auto* channel = descriptor_value(range, "Chnl");
  if (channel == nullptr || channel->type != DescriptorValue::Type::Reference ||
      channel->reference_items.empty()) {
    return -1;
  }
  const auto& item = channel->reference_items.front();
  if (item.form != "Enmr") {
    return -1;
  }
  for (std::size_t index = 0; index < kBlendIfChannelIds.size(); ++index) {
    if (item.id_b == kBlendIfChannelIds[index]) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

// Parses a style's blendOptions object. Returns settings when any modeled key
// (Opct, Md  , Blnd) is present; unsupported options append warnings.
[[nodiscard]] std::optional<AslBlendSettings> parse_blend_options(
    const DescriptorObject& options, const std::string& style_label,
    std::vector<std::string>& warnings) {
  bool any_modeled = false;
  AslBlendSettings settings;
  if (const auto* opacity = descriptor_value(options, "Opct");
      opacity != nullptr && (opacity->type == DescriptorValue::Type::UnitFloat ||
                             opacity->type == DescriptorValue::Type::Double)) {
    settings.opacity = static_cast<int>(std::clamp(std::lround(opacity->double_value), 0L, 100L));
    any_modeled = true;
  }
  if (const auto* mode = descriptor_value(options, "Md  ");
      mode != nullptr && mode->type == DescriptorValue::Type::Enum) {
    settings.blend_mode = blend_mode_from_lfx2_enum(mode->enum_value);
    any_modeled = true;
  }
  if (const auto* ranges = descriptor_value(options, "Blnd");
      ranges != nullptr && ranges->type == DescriptorValue::Type::List) {
    for (const auto& entry : ranges->list_value) {
      if (entry.type != DescriptorValue::Type::Object || entry.object_value == nullptr) {
        continue;
      }
      const auto& range = *entry.object_value;
      const auto channel = blend_if_channel_index(range);
      if (channel < 0) {
        warnings.push_back(style_label + ": Blend If channel is not supported and was ignored");
        continue;
      }
      BlendIfChannelRanges parsed;
      parsed.this_layer.black_low = clamp_range_byte(descriptor_number(range, "SrcB", 0.0));
      parsed.this_layer.black_high = clamp_range_byte(descriptor_number(range, "Srcl", 0.0));
      parsed.this_layer.white_low = clamp_range_byte(descriptor_number(range, "SrcW", 255.0));
      parsed.this_layer.white_high = clamp_range_byte(descriptor_number(range, "Srcm", 255.0));
      parsed.underlying_layer.black_low = clamp_range_byte(descriptor_number(range, "DstB", 0.0));
      parsed.underlying_layer.black_high = clamp_range_byte(descriptor_number(range, "Dstl", 0.0));
      parsed.underlying_layer.white_low = clamp_range_byte(descriptor_number(range, "DstW", 255.0));
      parsed.underlying_layer.white_high = clamp_range_byte(descriptor_number(range, "Dstt", 255.0));
      if (!blend_if_thresholds_are_valid(parsed.this_layer) ||
          !blend_if_thresholds_are_valid(parsed.underlying_layer)) {
        warnings.push_back(style_label + ": invalid Blend If range was ignored");
        continue;
      }
      settings.blend_if.channels[static_cast<std::size_t>(channel)] = parsed;
      any_modeled = true;
    }
  }
  if (const auto* fill = descriptor_value(options, "fillOpacity");
      fill != nullptr && (fill->type == DescriptorValue::Type::UnitFloat ||
                          fill->type == DescriptorValue::Type::Double)) {
    settings.fill_opacity =
        static_cast<int>(std::clamp(std::lround(fill->double_value), 0L, 100L));
    any_modeled = true;
  }
  std::string dropped;
  for (const auto& entry : options.key_order) {
    if (entry.key == "Opct" || entry.key == "Md  " || entry.key == "Blnd" ||
        entry.key == "fillOpacity") {
      continue;
    }
    if (!dropped.empty()) {
      dropped += ", ";
    }
    dropped += entry.key;
  }
  if (!dropped.empty()) {
    warnings.push_back(style_label + ": unsupported blending options were ignored (" + dropped +
                       ")");
  }
  if (!any_modeled) {
    return std::nullopt;
  }
  return settings;
}

// Parses one length-delimited style record (the two version-16 descriptors).
[[nodiscard]] std::optional<AslStyle> parse_style_record(std::span<const std::uint8_t> bytes,
                                                         const CmykToRgbTransform* cmyk_icc,
                                                         const std::string& fallback_label,
                                                         std::vector<std::string>& warnings) {
  BigEndianReader reader(bytes);
  if (reader.read_u32() != 16U) {
    warnings.push_back(fallback_label + ": unsupported descriptor version");
    return std::nullopt;
  }
  const auto identity = read_descriptor(reader);
  AslStyle style;
  style.name = resolve_zstring_name(descriptor_string(identity, "Nm  "));
  style.id = descriptor_string(identity, "Idnt");
  const auto label = style.name.empty() ? fallback_label : style.name;
  if (style.name.empty()) {
    style.name = fallback_label;
    warnings.push_back(fallback_label + ": style has no name");
  }
  if (style.id.empty() || style.id.size() > kMaxStyleIdBytes) {
    style.id = generate_pattern_uuid();
    warnings.push_back(label + ": style id was missing or invalid and has been replaced");
  }
  if (reader.read_u32() != 16U) {
    warnings.push_back(label + ": unsupported style descriptor version");
    return std::nullopt;
  }
  const auto styl = read_descriptor(reader);
  const auto* effects = descriptor_object(styl, "Lefx");
  if (effects != nullptr) {
    style.style = layer_style_from_lefx_descriptor(*effects, cmyk_icc);
    // Library entries are modeled data with no raw lfx2 block behind them;
    // custom Satin contours normalize to Linear at import.
    for (auto& satin : style.style.satins) {
      if (satin.unsupported_contour_options) {
        satin.unsupported_contour_options = false;
        warnings.push_back(label +
                           ": custom Satin contour options are approximated with the Linear contour");
      }
    }
  }
  if (const auto* options = descriptor_object(styl, "blendOptions"); options != nullptr) {
    style.blend_settings = parse_blend_options(*options, label, warnings);
  }
  if (effects == nullptr && !style.blend_settings.has_value()) {
    warnings.push_back(label + ": style contains no supported effects or blending options");
    return std::nullopt;
  }
  return style;
}

[[nodiscard]] DescriptorValue text_value(std::string text) {
  DescriptorValue value;
  value.type = DescriptorValue::Type::String;
  value.string_value = std::move(text);
  return value;
}

[[nodiscard]] DescriptorValue integer_value(int number) {
  DescriptorValue value;
  value.type = DescriptorValue::Type::Integer;
  value.integer_value = number;
  return value;
}

[[nodiscard]] DescriptorValue percent_value(double number) {
  DescriptorValue value;
  value.type = DescriptorValue::Type::UnitFloat;
  value.unit = "#Prc";
  value.double_value = number;
  return value;
}

[[nodiscard]] DescriptorValue object_value(DescriptorObject object) {
  DescriptorValue value;
  value.type = DescriptorValue::Type::Object;
  value.object_value = std::make_shared<DescriptorObject>(std::move(object));
  return value;
}

void append_key(DescriptorObject& object, std::string key, DescriptorValue value) {
  object.key_order.push_back(DescriptorObject::KeyEntry{key, key.size() != 4U});
  object.values.emplace(std::move(key), std::move(value));
}

// The calibrated blendOptions object: Opct, Md   (stringID enum), then a Blnd
// list with one entry per channel when Blend If is non-identity.
[[nodiscard]] DescriptorObject build_blend_options(const AslBlendSettings& settings) {
  DescriptorObject options;
  options.class_id = "blendOptions";
  append_key(options, "Opct", percent_value(static_cast<double>(settings.opacity)));
  DescriptorValue mode;
  mode.type = DescriptorValue::Type::Enum;
  mode.enum_type = "BlnM";
  mode.enum_value = std::string(blend_mode_lfx2_string(settings.blend_mode));
  mode.enum_value_long_form = mode.enum_value.size() == 4U;
  append_key(options, "Md  ", std::move(mode));
  if (settings.fill_opacity != 100) {
    append_key(options, "fillOpacity", percent_value(static_cast<double>(settings.fill_opacity)));
  }
  if (!blend_if_is_identity(settings.blend_if)) {
    DescriptorValue ranges;
    ranges.type = DescriptorValue::Type::List;
    for (std::size_t channel = 0; channel < settings.blend_if.channels.size(); ++channel) {
      const auto& channel_ranges = settings.blend_if.channels[channel];
      DescriptorObject range;
      range.class_id = "Blnd";
      DescriptorValue reference;
      reference.type = DescriptorValue::Type::Reference;
      DescriptorReferenceItem item;
      item.form = "Enmr";
      item.class_id = "Chnl";
      item.id_a = "Chnl";
      item.id_b = kBlendIfChannelIds[channel];
      reference.reference_items.push_back(std::move(item));
      append_key(range, "Chnl", std::move(reference));
      append_key(range, "SrcB", integer_value(channel_ranges.this_layer.black_low));
      append_key(range, "Srcl", integer_value(channel_ranges.this_layer.black_high));
      append_key(range, "SrcW", integer_value(channel_ranges.this_layer.white_low));
      append_key(range, "Srcm", integer_value(channel_ranges.this_layer.white_high));
      append_key(range, "DstB", integer_value(channel_ranges.underlying_layer.black_low));
      append_key(range, "Dstl", integer_value(channel_ranges.underlying_layer.black_high));
      append_key(range, "DstW", integer_value(channel_ranges.underlying_layer.white_low));
      append_key(range, "Dstt", integer_value(channel_ranges.underlying_layer.white_high));
      ranges.list_value.push_back(object_value(std::move(range)));
    }
    append_key(options, "Blnd", std::move(ranges));
  }
  return options;
}

// The effects object is the lfx2 root descriptor re-read into object form so it
// can embed as a 'Lefx' value (read -> write round-trips byte-identically).
[[nodiscard]] DescriptorObject build_lefx_object(const LayerStyle& style) {
  const auto payload = photoshop_lfx2_layer_style_payload(style);
  BigEndianReader reader(std::span<const std::uint8_t>(payload).subspan(8));
  auto object = read_descriptor(reader);
  object.class_id = "Lefx";
  return object;
}

}  // namespace

std::optional<AslReadResult> read_asl(std::span<const std::uint8_t> bytes, std::string& error,
                                      const CmykToRgbTransform* cmyk_icc) {
  error.clear();
  if (bytes.size() > kMaxAslBytes) {
    error = "ASL file exceeds the 32 MiB import limit";
    return std::nullopt;
  }
  AslReadResult result;
  try {
    BigEndianReader reader(bytes);
    const auto version = reader.read_u16();
    const auto signature = read_signature(reader);
    if (key_string(signature) != "8BSL") {
      error = "Not a Photoshop ASL file";
      return std::nullopt;
    }
    if (version != 2U) {
      error = "Unsupported ASL version " + std::to_string(version);
      return std::nullopt;
    }
    (void)reader.read_u16();  // patterns section version (PS writes 3)
    const auto patterns_length = reader.read_u32();
    if (patterns_length > reader.remaining()) {
      error = "ASL patterns section is truncated";
      return std::nullopt;
    }
    if (patterns_length > 0U) {
      result.patterns = parse_patterns_block(
          bytes.subspan(reader.position(), patterns_length), cmyk_icc);
      // Standalone files have no preserving document block; adopted tiles must
      // embed on save.
      for (auto& pattern : result.patterns) {
        pattern.provenance = PatternProvenance::Authored;
      }
      reader.skip(patterns_length);
    }
    const auto style_count = reader.read_u32();
    if (style_count > kMaxStyleCount) {
      error = "ASL style count exceeds " + std::to_string(kMaxStyleCount);
      return std::nullopt;
    }
    for (std::uint32_t index = 0; index < style_count; ++index) {
      const auto fallback_label = "Style " + std::to_string(index + 1U);
      if (reader.remaining() < 4U) {
        result.warnings.push_back(fallback_label + ": file ends before the record");
        break;
      }
      const auto record_length = reader.read_u32();
      if (record_length > reader.remaining()) {
        result.warnings.push_back(fallback_label + ": record is truncated");
        break;
      }
      const auto record = bytes.subspan(reader.position(), record_length);
      // Records are 4-byte padded; observed lengths already include the padding.
      reader.skip(std::min<std::size_t>(reader.remaining(),
                                        (static_cast<std::size_t>(record_length) + 3U) & ~3ULL));
      try {
        if (auto style = parse_style_record(record, cmyk_icc, fallback_label, result.warnings);
            style.has_value()) {
          result.styles.push_back(std::move(*style));
        }
      } catch (const std::exception&) {
        result.warnings.push_back(fallback_label + ": record is damaged and was skipped");
      }
    }
    // Optional trailing 8BIMphry hierarchy data is deliberately ignored.
  } catch (const std::exception&) {
    if (result.styles.empty()) {
      error = "ASL file is damaged";
      return std::nullopt;
    }
    result.warnings.push_back("The file is damaged past the last decoded style");
  }
  if (result.styles.empty()) {
    if (error.empty()) {
      error = "The file contains no usable styles";
    }
    return std::nullopt;
  }
  return result;
}

std::vector<std::uint8_t> write_asl(std::span<const AslStyle> styles,
                                    std::span<const PatternResource> patterns) {
  BigEndianWriter writer;
  writer.write_u16(2);
  for (const char ch : std::string_view("8BSL")) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
  writer.write_u16(3);  // patterns section version
  if (patterns.empty()) {
    // An empty section is length 0 with no count field (PS 2026 capture).
    writer.write_u32(0);
  } else {
    const auto block = serialize_patterns_block(patterns);
    writer.write_u32(static_cast<std::uint32_t>(block.size()));
    writer.write_bytes(block);
  }
  writer.write_u32(static_cast<std::uint32_t>(styles.size()));
  for (const auto& style : styles) {
    BigEndianWriter record;
    record.write_u32(16);
    DescriptorObject identity;
    identity.class_id = "null";
    append_key(identity, "Nm  ", text_value(style.name));
    append_key(identity, "Idnt", text_value(style.id));
    write_descriptor(record, identity);
    record.write_u32(16);
    DescriptorObject styl;
    styl.class_id = "Styl";
    DescriptorObject document_mode;
    document_mode.class_id = "documentMode";
    append_key(styl, "documentMode", object_value(std::move(document_mode)));
    append_key(styl, "Lefx", object_value(build_lefx_object(style.style)));
    if (style.blend_settings.has_value()) {
      append_key(styl, "blendOptions", object_value(build_blend_options(*style.blend_settings)));
    }
    write_descriptor(record, styl);
    while (record.bytes().size() % 4U != 0U) {
      record.write_u8(0);
    }
    writer.write_u32(static_cast<std::uint32_t>(record.bytes().size()));
    writer.write_bytes(record.bytes());
  }
  return writer.bytes();
}

}  // namespace patchy::psd
