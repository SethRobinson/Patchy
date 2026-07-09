#include "psd/psd_smart_objects.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace patchy::psd {

namespace {

constexpr double kAffineEpsilon = 1e-6;

std::string read_pascal_string(BigEndianReader& reader) {
  const auto length = reader.read_u8();
  const auto bytes = reader.read_bytes(length);
  return std::string(bytes.begin(), bytes.end());
}

void write_pascal_string(BigEndianWriter& writer, std::string_view text) {
  writer.write_u8(static_cast<std::uint8_t>(std::min<std::size_t>(text.size(), 255U)));
  for (std::size_t i = 0; i < std::min<std::size_t>(text.size(), 255U); ++i) {
    writer.write_u8(static_cast<std::uint8_t>(text[i]));
  }
}

std::optional<std::array<double, 8>> transform_from_list(const DescriptorValue* value) {
  if (value == nullptr || value->type != DescriptorValue::Type::List || value->list_value.size() != 8U) {
    return std::nullopt;
  }
  std::array<double, 8> transform{};
  for (std::size_t i = 0; i < 8U; ++i) {
    const auto& item = value->list_value[i];
    if (item.type != DescriptorValue::Type::Double) {
      return std::nullopt;
    }
    transform[i] = item.double_value;
  }
  return transform;
}

bool transforms_differ(const std::array<double, 8>& a, const std::array<double, 8>& b) {
  for (std::size_t i = 0; i < 8U; ++i) {
    if (std::abs(a[i] - b[i]) > kAffineEpsilon) {
      return true;
    }
  }
  return false;
}

bool quad_is_affine(const std::array<double, 8>& quad) {
  // A parallelogram satisfies corner0 + corner2 == corner1 + corner3.
  return std::abs((quad[0] + quad[4]) - (quad[2] + quad[6])) <= kAffineEpsilon &&
         std::abs((quad[1] + quad[5]) - (quad[3] + quad[7])) <= kAffineEpsilon;
}

// "" when the warp descriptor is absent or an identity ("warpNone") warp.
std::string warp_lock_reason(const DescriptorObject& parent) {
  if (descriptor_value(parent, "quiltWarp") != nullptr) {
    return "warp";
  }
  const auto* warp = descriptor_object(parent, "warp");
  if (warp == nullptr) {
    return {};
  }
  const auto* style = descriptor_value(*warp, "warpStyle");
  if (style != nullptr && style->type == DescriptorValue::Type::Enum && style->enum_value != "warpNone") {
    return "warp";
  }
  if (descriptor_number(*warp, "warpValue", 0.0) != 0.0 ||
      descriptor_number(*warp, "warpPerspective", 0.0) != 0.0 ||
      descriptor_number(*warp, "warpPerspectiveOther", 0.0) != 0.0) {
    return "warp";
  }
  return {};
}

std::optional<PlacedLayerInfo> parse_sold_block(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (key_string(read_signature(reader)) != "soLD") {
      return std::nullopt;
    }
    const auto version = reader.read_u32();
    if (version != 4 && version != 5) {
      return std::nullopt;
    }
    (void)reader.read_u32();  // descriptor version (16)
    const auto descriptor = read_descriptor(reader);

    PlacedLayerInfo info;
    const auto* identifier = descriptor_value(descriptor, "Idnt");
    if (identifier == nullptr || identifier->type != DescriptorValue::Type::String) {
      return std::nullopt;
    }
    info.placement.uuid = identifier->string_value;
    const auto transform = transform_from_list(descriptor_value(descriptor, "Trnf"));
    if (!transform.has_value()) {
      return std::nullopt;
    }
    info.placement.transform = *transform;
    if (const auto* size = descriptor_object(descriptor, "Sz  "); size != nullptr) {
      info.placement.width = descriptor_number(*size, "Wdth", 0.0);
      info.placement.height = descriptor_number(*size, "Hght", 0.0);
    }
    info.placement.resolution = descriptor_number(descriptor, "Rslt", 72.0);
    info.placement.placed_type = static_cast<int>(descriptor_number(descriptor, "Type", 2.0));
    info.placement.anti_alias = static_cast<int>(descriptor_number(descriptor, "Annt", 16.0));
    if (const auto* placed = descriptor_value(descriptor, "placed");
        placed != nullptr && placed->type == DescriptorValue::Type::String) {
      info.placed_uuid = placed->string_value;
    }

    if (descriptor_value(descriptor, "filterFX") != nullptr) {
      info.lock_reason = "filters";
    } else if (auto warp_reason = warp_lock_reason(descriptor); !warp_reason.empty()) {
      info.lock_reason = warp_reason;
    } else if (const auto non_affine = transform_from_list(descriptor_value(descriptor, "nonAffineTransform"));
               non_affine.has_value() && transforms_differ(*transform, *non_affine)) {
      info.lock_reason = "non_affine";
    } else if (!quad_is_affine(*transform)) {
      info.lock_reason = "non_affine";
    }
    return info;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<PlacedLayerInfo> parse_plld_block(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (key_string(read_signature(reader)) != "plcL") {
      return std::nullopt;
    }
    if (reader.read_u32() != 3) {  // version
      return std::nullopt;
    }
    PlacedLayerInfo info;
    info.placement.uuid = read_pascal_string(reader);
    (void)reader.read_u32();  // page number
    (void)reader.read_u32();  // total pages
    info.placement.anti_alias = static_cast<int>(reader.read_u32());
    info.placement.placed_type = static_cast<int>(reader.read_u32());
    for (auto& value : info.placement.transform) {
      value = read_f64(reader);
    }
    // No source size in the fixed layout: a PlLd-only smart object stays preview-locked
    // ("legacy") — modern Photoshop always writes a SoLd alongside, which wins.
    info.lock_reason = "legacy";
    return info;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// One parsed element span [start, end) plus the fields Patchy models.
struct ParsedElement {
  SmartObjectSource source;
};

std::optional<ParsedElement> parse_link_element(BigEndianReader& reader, std::span<const std::uint8_t> payload) {
  const auto element_length = reader.read_u64();
  const auto element_start = reader.position();
  if (element_length > reader.remaining()) {
    return std::nullopt;
  }
  const auto element_end = element_start + static_cast<std::size_t>(element_length);

  ParsedElement parsed;
  const auto kind = key_string(read_signature(reader));
  if (kind == "liFD") {
    parsed.source.kind = SmartObjectSourceKind::Embedded;
  } else if (kind == "liFE") {
    parsed.source.kind = SmartObjectSourceKind::ExternalFile;
  } else if (kind == "liFA") {
    parsed.source.kind = SmartObjectSourceKind::Alias;
  } else {
    return std::nullopt;
  }
  const auto version = reader.read_u32();
  if (version < 1 || version > 32) {
    return std::nullopt;
  }
  parsed.source.uuid = read_pascal_string(reader);
  parsed.source.filename = read_descriptor_unicode_string(reader);
  parsed.source.filetype = key_string(read_signature(reader));
  parsed.source.creator = key_string(read_signature(reader));
  const auto datasize = reader.read_u64();
  const auto has_open_descriptor = reader.read_u8();
  if (has_open_descriptor != 0) {
    (void)reader.read_u32();  // descriptor version
    (void)read_descriptor(reader);
  }
  if (parsed.source.kind == SmartObjectSourceKind::Embedded) {
    if (datasize > element_end - reader.position()) {
      return std::nullopt;
    }
    const auto data_start = reader.position();
    reader.skip(static_cast<std::size_t>(datasize));
    parsed.source.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(
        payload.begin() + static_cast<std::ptrdiff_t>(data_start),
        payload.begin() + static_cast<std::ptrdiff_t>(data_start + static_cast<std::size_t>(datasize)));
  }
  // Everything after the modeled fields (liFE descriptors/timestamps, child document
  // ids, version 8+ trailer) rides inside the verbatim element span; skip to the end.
  if (reader.position() < element_end) {
    reader.skip(element_end - reader.position());
  }
  const auto padding = (4U - (element_length % 4U)) % 4U;
  reader.skip(std::min<std::size_t>(static_cast<std::size_t>(padding), reader.remaining()));

  const auto span_end = reader.position();
  parsed.source.original_element_bytes = std::make_shared<const std::vector<std::uint8_t>>(
      payload.begin() + static_cast<std::ptrdiff_t>(element_start - 8U),
      payload.begin() + static_cast<std::ptrdiff_t>(span_end));
  return parsed;
}

// Serializes a fresh version-7 'liFD' element (length prefix through 4-byte padding).
std::vector<std::uint8_t> serialize_embedded_element(const SmartObjectSource& source) {
  BigEndianWriter body;
  for (const char ch : {'l', 'i', 'F', 'D'}) {
    body.write_u8(static_cast<std::uint8_t>(ch));
  }
  body.write_u32(7);  // version
  write_pascal_string(body, source.uuid);
  write_descriptor_unicode_string(body, source.filename);
  const auto write_ostype = [&body](std::string_view type) {
    for (std::size_t i = 0; i < 4U; ++i) {
      body.write_u8(static_cast<std::uint8_t>(i < type.size() ? type[i] : ' '));
    }
  };
  write_ostype(source.filetype);
  write_ostype(source.creator);
  const auto data_size = source.file_bytes != nullptr ? source.file_bytes->size() : 0U;
  body.write_u64(data_size);
  body.write_u8(0);  // no file-open descriptor
  if (source.file_bytes != nullptr) {
    body.write_bytes(*source.file_bytes);
  }
  body.write_u32(0);      // child document id: empty unicode string
  write_f64(body, 0.0);  // asset mod time
  body.write_u8(0);       // asset locked state

  BigEndianWriter element;
  element.write_u64(body.bytes().size());
  element.write_bytes(body.bytes());
  const auto padding = (4U - (body.bytes().size() % 4U)) % 4U;
  for (std::size_t i = 0; i < padding; ++i) {
    element.write_u8(0);
  }
  return element.bytes();
}

}  // namespace

std::optional<PlacedLayerInfo> parse_placed_layer_block(std::string_view key,
                                                        std::span<const std::uint8_t> payload) {
  if (key == "SoLd" || key == "SoLE") {
    return parse_sold_block(payload);
  }
  if (key == "PlLd" || key == "plLd") {
    return parse_plld_block(payload);
  }
  return std::nullopt;
}

std::optional<std::vector<SmartObjectSource>> parse_linked_layer_block(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    std::vector<SmartObjectSource> sources;
    while (reader.remaining() >= 8U) {
      auto parsed = parse_link_element(reader, payload);
      if (!parsed.has_value()) {
        return std::nullopt;
      }
      sources.push_back(std::move(parsed->source));
    }
    if (reader.remaining() != 0) {
      return std::nullopt;
    }
    return sources;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> serialize_linked_layer_block(const SmartObjectLinkBlock& block) {
  const auto any_dirty = std::any_of(block.sources.begin(), block.sources.end(),
                                     [](const SmartObjectSource& source) {
                                       return source.dirty || source.original_element_bytes == nullptr;
                                     });
  if (!any_dirty && block.original_payload != nullptr) {
    return *block.original_payload;
  }
  std::vector<std::uint8_t> payload;
  for (const auto& source : block.sources) {
    if (!source.dirty && source.original_element_bytes != nullptr) {
      payload.insert(payload.end(), source.original_element_bytes->begin(), source.original_element_bytes->end());
      continue;
    }
    if (source.kind != SmartObjectSourceKind::Embedded) {
      // External/alias sources are never authored or edited by Patchy; while clean they
      // re-emit verbatim above. A dirty one without original bytes cannot round-trip.
      continue;
    }
    const auto element = serialize_embedded_element(source);
    payload.insert(payload.end(), element.begin(), element.end());
  }
  return payload;
}

std::optional<std::vector<std::uint8_t>> regenerate_placed_layer_payload(
    std::string_view key, std::span<const std::uint8_t> original_payload, const SmartObjectPlacement& placement) {
  try {
    if (key == "SoLd" || key == "SoLE") {
      BigEndianReader reader(original_payload);
      if (key_string(read_signature(reader)) != "soLD") {
        return std::nullopt;
      }
      const auto version = reader.read_u32();
      const auto descriptor_version = reader.read_u32();
      auto descriptor = read_descriptor(reader);

      if (auto* identifier = const_cast<DescriptorValue*>(descriptor_value(descriptor, "Idnt"));
          identifier != nullptr && identifier->type == DescriptorValue::Type::String) {
        identifier->string_value = placement.uuid;
      }
      const auto set_transform = [&placement, &descriptor](const char* transform_key) {
        auto* value = const_cast<DescriptorValue*>(descriptor_value(descriptor, transform_key));
        if (value != nullptr && value->type == DescriptorValue::Type::List && value->list_value.size() == 8U) {
          for (std::size_t i = 0; i < 8U; ++i) {
            value->list_value[i].double_value = placement.transform[i];
          }
        }
      };
      set_transform("Trnf");
      set_transform("nonAffineTransform");
      if (auto* size = const_cast<DescriptorObject*>(descriptor_object(descriptor, "Sz  ")); size != nullptr) {
        if (auto* width = const_cast<DescriptorValue*>(descriptor_value(*size, "Wdth")); width != nullptr) {
          width->double_value = placement.width;
        }
        if (auto* height = const_cast<DescriptorValue*>(descriptor_value(*size, "Hght")); height != nullptr) {
          height->double_value = placement.height;
        }
      }
      if (auto* resolution = const_cast<DescriptorValue*>(descriptor_value(descriptor, "Rslt"));
          resolution != nullptr && resolution->type == DescriptorValue::Type::UnitFloat) {
        resolution->double_value = placement.resolution;
      }
      // Unwarped placements keep their warp bounds glued to the quad's bounding box
      // (Photoshop maintains the same invariant).
      if (auto* warp = const_cast<DescriptorObject*>(descriptor_object(descriptor, "warp")); warp != nullptr) {
        const auto* style = descriptor_value(*warp, "warpStyle");
        const bool warp_none =
            style != nullptr && style->type == DescriptorValue::Type::Enum && style->enum_value == "warpNone";
        if (warp_none) {
          if (auto* bounds = const_cast<DescriptorObject*>(descriptor_object(*warp, "bounds")); bounds != nullptr) {
            const auto min_x = std::min({placement.transform[0], placement.transform[2], placement.transform[4],
                                         placement.transform[6]});
            const auto max_x = std::max({placement.transform[0], placement.transform[2], placement.transform[4],
                                         placement.transform[6]});
            const auto min_y = std::min({placement.transform[1], placement.transform[3], placement.transform[5],
                                         placement.transform[7]});
            const auto max_y = std::max({placement.transform[1], placement.transform[3], placement.transform[5],
                                         placement.transform[7]});
            const auto set_bound = [bounds](const char* bound_key, double bound_value) {
              if (auto* value = const_cast<DescriptorValue*>(descriptor_value(*bounds, bound_key));
                  value != nullptr && value->type == DescriptorValue::Type::Double) {
                value->double_value = bound_value;
              }
            };
            set_bound("Top ", min_y);
            set_bound("Left", min_x);
            set_bound("Btom", max_y);
            set_bound("Rght", max_x);
          }
        }
      }

      BigEndianWriter writer;
      for (const char ch : {'s', 'o', 'L', 'D'}) {
        writer.write_u8(static_cast<std::uint8_t>(ch));
      }
      writer.write_u32(version);
      writer.write_u32(descriptor_version);
      write_descriptor(writer, descriptor);
      while ((writer.bytes().size() % 4U) != 0U) {
        writer.write_u8(0);
      }
      return writer.bytes();
    }

    if (key == "PlLd" || key == "plLd") {
      BigEndianReader reader(original_payload);
      if (key_string(read_signature(reader)) != "plcL" || reader.read_u32() != 3) {
        return std::nullopt;
      }
      const auto original_uuid = read_pascal_string(reader);
      const auto page_number = reader.read_u32();
      const auto total_pages = reader.read_u32();
      const auto anti_alias = reader.read_u32();
      const auto placed_type = reader.read_u32();
      for (int i = 0; i < 8; ++i) {
        (void)read_f64(reader);
      }
      // The warp version + descriptor tail re-emits verbatim.
      const auto tail_start = reader.position();
      (void)original_uuid;

      BigEndianWriter writer;
      for (const char ch : {'p', 'l', 'c', 'L'}) {
        writer.write_u8(static_cast<std::uint8_t>(ch));
      }
      writer.write_u32(3);
      write_pascal_string(writer, placement.uuid);
      writer.write_u32(page_number);
      writer.write_u32(total_pages);
      writer.write_u32(anti_alias);
      writer.write_u32(placed_type);
      for (const auto value : placement.transform) {
        write_f64(writer, value);
      }
      writer.write_bytes(std::span<const std::uint8_t>(original_payload.data() + tail_start,
                                                       original_payload.size() - tail_start));
      return writer.bytes();
    }
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace patchy::psd
