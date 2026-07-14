// Image-resources (8BIM) section codec for the PSD reader/writer: resource
// parse/serialize, alpha-channel names/identifiers/display-info records, the
// resolution (1005) and grid/guides (1032) resources, and the private Patchy
// palette resource (4210). Split out of psd_document_io.cpp as a pure move.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"
#include "core/style_contour.hpp"
#include "core/text_warp.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_patterns.hpp"
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

std::vector<std::uint8_t> resolution_resource_for_document(const Document& document) {
  const auto& print_settings = document.print_settings();
  BigEndianWriter writer;
  writer.write_u32(double_to_fixed_16_16(print_settings.horizontal_ppi));
  writer.write_u16(print_settings.horizontal_resolution_display_unit);
  writer.write_u16(print_settings.width_display_unit);
  writer.write_u32(double_to_fixed_16_16(print_settings.vertical_ppi));
  writer.write_u16(print_settings.vertical_resolution_display_unit);
  writer.write_u16(print_settings.height_display_unit);
  return writer.bytes();
}

std::int32_t sanitized_grid_cycle_32(std::int32_t value) noexcept {
  return value > 0 ? value : kDefaultGridCycle32;
}

std::int32_t sanitized_guide_position_32(std::int32_t value) noexcept {
  return std::max<std::int32_t>(0, value);
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

}  // namespace

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

std::optional<DocumentPrintSettings> print_settings_from_resolution_resource(std::span<const std::uint8_t> payload) {
  if (payload.size() < 16U) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  const auto horizontal = fixed_16_16_to_double(reader.read_u32());
  const auto horizontal_unit = reader.read_u16();
  const auto width_unit = reader.read_u16();
  const auto vertical = fixed_16_16_to_double(reader.read_u32());
  const auto vertical_unit = reader.read_u16();
  const auto height_unit = reader.read_u16();

  DocumentPrintSettings settings;
  // hRes/vRes are ALWAYS pixels/inch; the unit fields are display-only. Verified
  // against Photoshop 2026 by byte-patching a 144 PPI file's units to 2 (px/cm):
  // PS still reports resolution 144. (The previous x2.54 conversion misread real
  // px/cm-display files as 2.54x their resolution.)
  settings.horizontal_ppi = sanitized_print_ppi(horizontal);
  settings.vertical_ppi = sanitized_print_ppi(vertical);
  settings.horizontal_resolution_display_unit = horizontal_unit;
  settings.vertical_resolution_display_unit = vertical_unit;
  settings.width_display_unit = width_unit;
  settings.height_display_unit = height_unit;
  return settings;
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

}  // namespace patchy::psd
