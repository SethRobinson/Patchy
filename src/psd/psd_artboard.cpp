#include "psd/psd_artboard.hpp"

#include "psd/psd_descriptor.hpp"
#include "psd/psd_io_internal.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace patchy::psd {
namespace {

constexpr std::uint32_t kDescriptorVersion = 16;

DescriptorValue double_value(double value) {
  DescriptorValue entry;
  entry.type = DescriptorValue::Type::Double;
  entry.double_value = value;
  return entry;
}

DescriptorValue integer_value(std::int32_t value) {
  DescriptorValue entry;
  entry.type = DescriptorValue::Type::Integer;
  entry.integer_value = value;
  return entry;
}

DescriptorValue bool_value(bool value) {
  DescriptorValue entry;
  entry.type = DescriptorValue::Type::Bool;
  entry.bool_value = value;
  return entry;
}

DescriptorValue string_value(std::string value) {
  DescriptorValue entry;
  entry.type = DescriptorValue::Type::String;
  entry.string_value = std::move(value);
  return entry;
}

DescriptorValue object_value(DescriptorObject object) {
  DescriptorValue entry;
  entry.type = DescriptorValue::Type::Object;
  entry.object_value = std::make_shared<DescriptorObject>(std::move(object));
  return entry;
}

void add(DescriptorObject& object, std::string key, bool long_form, DescriptorValue value) {
  object.key_order.push_back({key, long_form});
  object.values.insert_or_assign(std::move(key), std::move(value));
}

DescriptorObject object_of_class(std::string class_id, bool long_form) {
  DescriptorObject object;
  object.class_id = std::move(class_id);
  object.class_id_long_form = long_form;
  return object;
}

// A number stored as a double or an integer (artd writes its color as 'long').
std::optional<double> number_of(const DescriptorObject& object, std::string_view key) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr) {
    return std::nullopt;
  }
  switch (value->type) {
    case DescriptorValue::Type::Double:
    case DescriptorValue::Type::UnitFloat:
      return value->double_value;
    case DescriptorValue::Type::Integer:
      return static_cast<double>(value->integer_value);
    case DescriptorValue::Type::LargeInteger:
      return static_cast<double>(value->large_integer_value);
    default:
      return std::nullopt;
  }
}

std::optional<RgbColor> color_of(const DescriptorObject& object, std::string_view key) {
  const auto* color = descriptor_object(object, key);
  if (color == nullptr) {
    return std::nullopt;
  }
  // HSB, Lab, CMYK, gray and float colors decode through the shared descriptor decoder
  // (read as RGB keys they came out black). A plain RGB color still needs all three
  // channels, or the artboard keeps its default.
  const bool other_class = color->class_id == "CMYC" || color->class_id == "Grsc" ||
                           color->class_id == "LbCl" || color->class_id == "HSBC";
  const bool float_rgb = descriptor_value(*color, "redFloat") != nullptr ||
                         descriptor_value(*color, "greenFloat") != nullptr ||
                         descriptor_value(*color, "blueFloat") != nullptr;
  if (!other_class && !float_rgb &&
      (!number_of(*color, "Rd  ").has_value() || !number_of(*color, "Grn ").has_value() ||
       !number_of(*color, "Bl  ").has_value())) {
    return std::nullopt;
  }
  return descriptor_rgb_color(object, key, CmykColorConverter{});
}

ArtboardBackground background_of(std::int32_t type) {
  switch (type) {
    case 2:
      return ArtboardBackground::Black;
    case 3:
      return ArtboardBackground::Transparent;
    case 4:
      return ArtboardBackground::Custom;
    default:
      return ArtboardBackground::White;
  }
}

DescriptorObject point_object(double horizontal, double vertical) {
  auto point = object_of_class("Pnt ", false);
  add(point, "Hrzn", false, double_value(horizontal));
  add(point, "Vrtc", false, double_value(vertical));
  return point;
}

std::vector<std::uint8_t> versioned(const DescriptorObject& descriptor, bool pad_to_four) {
  BigEndianWriter writer;
  writer.write_u32(kDescriptorVersion);
  write_descriptor(writer, descriptor);
  auto bytes = writer.bytes();
  if (pad_to_four) {
    // Photoshop pads its per-layer artboard record to a multiple of four.
    while (bytes.size() % 4U != 0U) {
      bytes.push_back(0U);
    }
  }
  return bytes;
}

}  // namespace

bool is_artboard_block_key(std::string_view key) noexcept {
  return key == "artb" || key == "abdd" || key == "artd";
}

std::optional<LayerArtboard> parse_artboard_block(std::span<const std::uint8_t> payload) {
  if (payload.size() < 4U) {
    return std::nullopt;
  }
  try {
    BigEndianReader reader(payload);
    if (reader.read_u32() != kDescriptorVersion) {
      return std::nullopt;
    }
    const auto descriptor = read_descriptor(reader);
    const auto* rect = descriptor_object(descriptor, "artboardRect");
    if (rect == nullptr) {
      return std::nullopt;  // a document-level 'artd' under a layer key, or something else
    }
    const auto top = number_of(*rect, "Top ");
    const auto left = number_of(*rect, "Left");
    const auto bottom = number_of(*rect, "Btom");
    const auto right = number_of(*rect, "Rght");
    if (!top.has_value() || !left.has_value() || !bottom.has_value() || !right.has_value()) {
      return std::nullopt;
    }
    LayerArtboard artboard;
    const auto x0 = static_cast<std::int32_t>(std::lround(*left));
    const auto y0 = static_cast<std::int32_t>(std::lround(*top));
    const auto x1 = static_cast<std::int32_t>(std::lround(*right));
    const auto y1 = static_cast<std::int32_t>(std::lround(*bottom));
    if (x1 <= x0 || y1 <= y0) {
      return std::nullopt;
    }
    artboard.rect = Rect{x0, y0, x1 - x0, y1 - y0};
    if (const auto type = number_of(descriptor, "artboardBackgroundType"); type.has_value()) {
      artboard.background = background_of(static_cast<std::int32_t>(std::lround(*type)));
    }
    if (const auto color = color_of(descriptor, "Clr "); color.has_value()) {
      artboard.color = *color;
    }
    if (const auto* name = descriptor_value(descriptor, "artboardPresetName");
        name != nullptr && name->type == DescriptorValue::Type::String) {
      artboard.preset_name = name->string_value;
    }
    if (const auto* guides = descriptor_value(descriptor, "guideIndeces");
        guides != nullptr && guides->type == DescriptorValue::Type::List) {
      for (const auto& guide : guides->list_value) {
        if (guide.type == DescriptorValue::Type::Integer) {
          artboard.guide_indices.push_back(guide.integer_value);
        } else if (guide.type == DescriptorValue::Type::Double) {
          artboard.guide_indices.push_back(static_cast<std::int32_t>(std::lround(guide.double_value)));
        }
      }
    }
    return artboard;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> artboard_block_payload(const LayerArtboard& artboard) {
  auto descriptor = object_of_class("artboard", true);
  auto rect = object_of_class("classFloatRect", true);
  add(rect, "Top ", false, double_value(artboard.rect.y));
  add(rect, "Left", false, double_value(artboard.rect.x));
  add(rect, "Btom", false, double_value(static_cast<double>(artboard.rect.y) + artboard.rect.height));
  add(rect, "Rght", false, double_value(static_cast<double>(artboard.rect.x) + artboard.rect.width));
  add(descriptor, "artboardRect", true, object_value(std::move(rect)));
  DescriptorValue guides;
  guides.type = DescriptorValue::Type::List;
  for (const auto index : artboard.guide_indices) {
    guides.list_value.push_back(integer_value(index));
  }
  add(descriptor, "guideIndeces", true, std::move(guides));
  add(descriptor, "artboardPresetName", true, string_value(artboard.preset_name));
  auto color = object_of_class("RGBC", false);
  add(color, "Rd  ", false, double_value(artboard.color.red));
  add(color, "Grn ", false, double_value(artboard.color.green));
  add(color, "Bl  ", false, double_value(artboard.color.blue));
  add(descriptor, "Clr ", false, object_value(std::move(color)));
  add(descriptor, "artboardBackgroundType", true, integer_value(static_cast<std::int32_t>(artboard.background)));
  return versioned(descriptor, true);
}

std::optional<DocumentArtboardBlock> parse_document_artboard_block(std::span<const std::uint8_t> payload) {
  if (payload.size() < 4U) {
    return std::nullopt;
  }
  try {
    BigEndianReader reader(payload);
    if (reader.read_u32() != kDescriptorVersion) {
      return std::nullopt;
    }
    const auto descriptor = read_descriptor(reader);
    DocumentArtboardBlock block;
    if (const auto count = number_of(descriptor, "Cnt "); count.has_value()) {
      block.count = static_cast<std::int32_t>(std::lround(*count));
    }
    auto& settings = block.settings;
    if (const auto* offset = descriptor_object(descriptor, "autoExpandOffset"); offset != nullptr) {
      settings.auto_expand_offset_x = number_of(*offset, "Hrzn").value_or(0.0);
      settings.auto_expand_offset_y = number_of(*offset, "Vrtc").value_or(0.0);
    }
    if (const auto* origin = descriptor_object(descriptor, "origin"); origin != nullptr) {
      settings.origin_x = number_of(*origin, "Hrzn").value_or(0.0);
      settings.origin_y = number_of(*origin, "Vrtc").value_or(0.0);
    }
    settings.auto_expand = descriptor_bool(descriptor, "autoExpandEnabled", settings.auto_expand);
    settings.auto_nest = descriptor_bool(descriptor, "autoNestEnabled", settings.auto_nest);
    settings.auto_position = descriptor_bool(descriptor, "autoPositionEnabled", settings.auto_position);
    settings.shrinkwrap_on_save = descriptor_bool(descriptor, "shrinkwrapOnSaveEnabled", settings.shrinkwrap_on_save);
    if (const auto color = color_of(descriptor, "docDefaultNewArtboardBackgroundColor"); color.has_value()) {
      settings.default_color = *color;
    }
    if (const auto type = number_of(descriptor, "docDefaultNewArtboardBackgroundType"); type.has_value()) {
      settings.default_background = background_of(static_cast<std::int32_t>(std::lround(*type)));
    }
    return block;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> document_artboard_block_payload(const DocumentArtboardSettings& settings,
                                                          std::int32_t count) {
  auto descriptor = object_of_class("null", false);
  add(descriptor, "Cnt ", false, integer_value(count));
  add(descriptor, "autoExpandOffset", true,
      object_value(point_object(settings.auto_expand_offset_x, settings.auto_expand_offset_y)));
  add(descriptor, "origin", true, object_value(point_object(settings.origin_x, settings.origin_y)));
  add(descriptor, "autoExpandEnabled", true, bool_value(settings.auto_expand));
  add(descriptor, "autoNestEnabled", true, bool_value(settings.auto_nest));
  add(descriptor, "autoPositionEnabled", true, bool_value(settings.auto_position));
  add(descriptor, "shrinkwrapOnSaveEnabled", true, bool_value(settings.shrinkwrap_on_save));
  auto color = object_of_class("RGBC", false);
  add(color, "Rd  ", false, integer_value(settings.default_color.red));
  add(color, "Grn ", false, integer_value(settings.default_color.green));
  add(color, "Bl  ", false, integer_value(settings.default_color.blue));
  add(descriptor, "docDefaultNewArtboardBackgroundColor", true, object_value(std::move(color)));
  add(descriptor, "docDefaultNewArtboardBackgroundType", true,
      integer_value(static_cast<std::int32_t>(settings.default_background)));
  // Global blocks are 4-aligned by the block writer, outside the declared length.
  return versioned(descriptor, false);
}

}  // namespace patchy::psd
