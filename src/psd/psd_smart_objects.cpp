#include "psd/psd_smart_objects.hpp"

#include "core/warp_mesh.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_layer_effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

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

bool known_smart_filter_blend_mode(std::string_view value, BlendMode& mode) {
  // Photoshop 2026 writes full stringIDs here, just like lfx2. Do not let the
  // shared converter's unknown-to-Normal fallback make an imported stack look
  // executable when its native blend mode is actually unsupported.
  static constexpr std::array<std::string_view, 21> kKnown{
      "normal",           "multiply",         "screen",
      "overlay",          "darken",           "lighten",
      "colorDodge",       "colorBurn",        "hardLight",
      "softLight",        "difference",       "linearBurn",
      "pinLight",         "saturation",       "luminosity",
      "exclusion",        "hue",              "color",
      "linearDodge",      "blendSubtraction", "blendDivide",
  };
  if (std::find(kKnown.begin(), kKnown.end(), value) == kKnown.end()) {
    return false;
  }
  mode = blend_mode_from_lfx2_enum(value);
  return true;
}

DescriptorValue smart_filter_bool(bool value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Bool;
  result.bool_value = value;
  return result;
}

DescriptorValue smart_filter_integer(std::int32_t value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Integer;
  result.integer_value = value;
  return result;
}

DescriptorValue smart_filter_double(double value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Double;
  result.double_value = value;
  return result;
}

DescriptorValue smart_filter_text(std::string value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::String;
  result.string_value = std::move(value);
  return result;
}

DescriptorValue smart_filter_unit(std::string unit, double value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::UnitFloat;
  result.unit = std::move(unit);
  result.double_value = value;
  return result;
}

DescriptorValue smart_filter_enum(std::string type, bool type_long_form,
                                  std::string value, bool value_long_form) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Enum;
  result.enum_type = std::move(type);
  result.enum_type_long_form = type_long_form;
  result.enum_value = std::move(value);
  result.enum_value_long_form = value_long_form;
  return result;
}

DescriptorValue smart_filter_object(std::string class_id,
                                    bool class_long_form,
                                    std::string name = {}) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Object;
  result.object_value = std::make_shared<DescriptorObject>();
  result.object_value->name = std::move(name);
  result.object_value->class_id = std::move(class_id);
  result.object_value->class_id_long_form = class_long_form;
  return result;
}

void add_smart_filter_value(DescriptorObject& object, std::string key,
                            bool long_form, DescriptorValue value) {
  object.key_order.push_back({key, long_form});
  object.values.emplace(std::move(key), std::move(value));
}

DescriptorValue smart_filter_color(RgbColor color) {
  auto value = smart_filter_object("RGBC", false);
  add_smart_filter_value(*value.object_value, "Rd  ", false,
                         smart_filter_double(color.red));
  add_smart_filter_value(*value.object_value, "Grn ", false,
                         smart_filter_double(color.green));
  add_smart_filter_value(*value.object_value, "Bl  ", false,
                         smart_filter_double(color.blue));
  return value;
}

std::optional<DescriptorValue> make_smart_filter_entry_descriptor(
    const SmartFilterEntry& entry) {
  const auto* gaussian =
      std::get_if<GaussianBlurSmartFilter>(&entry.parameters);
  if (entry.kind != SmartFilterKind::GaussianBlur || gaussian == nullptr ||
      !std::isfinite(gaussian->radius_pixels) ||
      gaussian->radius_pixels < 0.1 || gaussian->radius_pixels > 1000.0) {
    return std::nullopt;
  }
  auto item = smart_filter_object("filterFX", true);
  add_smart_filter_value(
      *item.object_value, "Nm  ", false,
      smart_filter_text(entry.native_name.empty() ? "Gaussian Blur..."
                                                  : entry.native_name));
  auto blend = smart_filter_object("blendOptions", true);
  add_smart_filter_value(
      *blend.object_value, "Opct", false,
      smart_filter_unit("#Prc", std::clamp(entry.opacity, 0.0, 1.0) * 100.0));
  add_smart_filter_value(
      *blend.object_value, "Md  ", false,
      smart_filter_enum("BlnM", false,
                        std::string(blend_mode_lfx2_string(entry.blend_mode)),
                        true));
  add_smart_filter_value(*item.object_value, "blendOptions", true,
                         std::move(blend));
  add_smart_filter_value(*item.object_value, "enab", false,
                         smart_filter_bool(entry.enabled));
  add_smart_filter_value(*item.object_value, "hasoptions", true,
                         smart_filter_bool(entry.has_options));
  add_smart_filter_value(*item.object_value, "FrgC", false,
                         smart_filter_color(entry.foreground));
  add_smart_filter_value(*item.object_value, "BckC", false,
                         smart_filter_color(entry.background));
  auto filter = smart_filter_object("GsnB", false, "Gaussian Blur");
  add_smart_filter_value(*filter.object_value, "Rds ", false,
                         smart_filter_unit("#Pxl", gaussian->radius_pixels));
  add_smart_filter_value(*item.object_value, "Fltr", false,
                         std::move(filter));
  add_smart_filter_value(
      *item.object_value, "filterID", true,
      smart_filter_integer(static_cast<std::int32_t>(0x47736e42U)));
  return item;
}

std::optional<DescriptorValue>
make_smart_filter_descriptor(const SmartFilterStack& stack) {
  if (stack.support != SmartFilterStackSupport::Supported ||
      stack.entries.empty()) {
    return std::nullopt;
  }
  auto root = smart_filter_object("filterFXStyle", true);
  add_smart_filter_value(*root.object_value, "enab", false,
                         smart_filter_bool(stack.enabled));
  add_smart_filter_value(*root.object_value, "validAtPosition", true,
                         smart_filter_bool(stack.valid_at_position));
  add_smart_filter_value(*root.object_value, "filterMaskEnable", true,
                         smart_filter_bool(stack.mask.enabled));
  add_smart_filter_value(*root.object_value, "filterMaskLinked", true,
                         smart_filter_bool(stack.mask.linked));
  add_smart_filter_value(*root.object_value, "filterMaskExtendWithWhite", true,
                         smart_filter_bool(stack.mask.extend_with_white));

  DescriptorValue list;
  list.type = DescriptorValue::Type::List;
  list.list_value.reserve(stack.entries.size());
  for (const auto& entry : stack.entries) {
    auto item = make_smart_filter_entry_descriptor(entry);
    if (!item.has_value()) {
      return std::nullopt;
    }
    list.list_value.push_back(std::move(*item));
  }
  add_smart_filter_value(*root.object_value, "filterFXList", true,
                         std::move(list));
  return root;
}

DescriptorValue clone_descriptor_value(const DescriptorValue& source);

std::shared_ptr<DescriptorObject> clone_descriptor_object(
    const DescriptorObject& source) {
  auto result = std::make_shared<DescriptorObject>();
  result->name = source.name;
  result->class_id = source.class_id;
  result->class_id_long_form = source.class_id_long_form;
  result->key_order = source.key_order;
  for (const auto& [key, value] : source.values) {
    result->values.emplace(key, clone_descriptor_value(value));
  }
  return result;
}

DescriptorValue clone_descriptor_value(const DescriptorValue& source) {
  DescriptorValue result = source;
  if (source.object_value != nullptr) {
    result.object_value = clone_descriptor_object(*source.object_value);
  }
  result.list_value.clear();
  result.list_value.reserve(source.list_value.size());
  for (const auto& item : source.list_value) {
    result.list_value.push_back(clone_descriptor_value(item));
  }
  return result;
}

bool set_smart_filter_bool(DescriptorObject& object, std::string_view key,
                           bool value) {
  auto* field = const_cast<DescriptorValue*>(descriptor_value(object, key));
  if (field == nullptr || field->type != DescriptorValue::Type::Bool) {
    return false;
  }
  field->bool_value = value;
  return true;
}

bool set_smart_filter_color(DescriptorObject& object, std::string_view key,
                            RgbColor color) {
  auto* value = const_cast<DescriptorObject*>(descriptor_object(object, key));
  if (value == nullptr || value->class_id != "RGBC") {
    return false;
  }
  const std::array<std::pair<const char*, std::uint8_t>, 3> channels{{
      {"Rd  ", color.red}, {"Grn ", color.green}, {"Bl  ", color.blue}}};
  for (const auto& [channel_key, channel] : channels) {
    auto* field =
        const_cast<DescriptorValue*>(descriptor_value(*value, channel_key));
    if (field == nullptr || (field->type != DescriptorValue::Type::Double &&
                             field->type != DescriptorValue::Type::Integer)) {
      return false;
    }
    if (field->type == DescriptorValue::Type::Double) {
      field->double_value = channel;
    } else {
      field->integer_value = channel;
    }
  }
  return true;
}

bool patch_smart_filter_descriptor(
    DescriptorValue& value, const SmartFilterStack& stack,
    const std::vector<std::optional<std::size_t>>& entry_sources) {
  if (value.type != DescriptorValue::Type::Object ||
      value.object_value == nullptr ||
      value.object_value->class_id != "filterFXStyle" ||
      stack.support != SmartFilterStackSupport::Supported) {
    return false;
  }
  auto& root = *value.object_value;
  if (!set_smart_filter_bool(root, "enab", stack.enabled) ||
      !set_smart_filter_bool(root, "validAtPosition", stack.valid_at_position) ||
      !set_smart_filter_bool(root, "filterMaskEnable", stack.mask.enabled) ||
      !set_smart_filter_bool(root, "filterMaskLinked", stack.mask.linked) ||
      !set_smart_filter_bool(root, "filterMaskExtendWithWhite",
                             stack.mask.extend_with_white)) {
    return false;
  }
  auto* list = const_cast<DescriptorValue*>(descriptor_value(root, "filterFXList"));
  if (list == nullptr || list->type != DescriptorValue::Type::List) {
    return false;
  }
  if (!entry_sources.empty()) {
    if (entry_sources.size() != stack.entries.size()) {
      return false;
    }
    const auto& original_entries = list->list_value;
    std::vector<DescriptorValue> desired_entries;
    desired_entries.reserve(stack.entries.size());
    for (std::size_t index = 0; index < stack.entries.size(); ++index) {
      const auto source = entry_sources[index];
      if (source.has_value()) {
        if (*source >= original_entries.size()) {
          return false;
        }
        desired_entries.push_back(
            clone_descriptor_value(original_entries[*source]));
        continue;
      }
      auto authored = make_smart_filter_entry_descriptor(stack.entries[index]);
      if (!authored.has_value()) {
        return false;
      }
      desired_entries.push_back(std::move(*authored));
    }
    list->list_value = std::move(desired_entries);
  } else if (list->list_value.size() != stack.entries.size()) {
    return false;
  }
  const auto mutable_value_either = [](DescriptorObject& object,
                                       std::string_view first,
                                       std::string_view second)
      -> DescriptorValue* {
    if (auto found = object.values.find(std::string(first));
        found != object.values.end()) {
      return &found->second;
    }
    if (auto found = object.values.find(std::string(second));
        found != object.values.end()) {
      return &found->second;
    }
    return nullptr;
  };
  for (std::size_t index = 0; index < stack.entries.size(); ++index) {
    const auto& entry = stack.entries[index];
    const auto* gaussian =
        std::get_if<GaussianBlurSmartFilter>(&entry.parameters);
    auto& item_value = list->list_value[index];
    if (entry.kind != SmartFilterKind::GaussianBlur || gaussian == nullptr ||
        !std::isfinite(gaussian->radius_pixels) ||
        gaussian->radius_pixels < 0.1 || gaussian->radius_pixels > 1000.0 ||
        item_value.type != DescriptorValue::Type::Object ||
        item_value.object_value == nullptr ||
        item_value.object_value->class_id != "filterFX") {
      return false;
    }
    auto& item = *item_value.object_value;
    if (!set_smart_filter_bool(item, "enab", entry.enabled) ||
        !set_smart_filter_bool(item, "hasoptions", entry.has_options) ||
        !set_smart_filter_color(item, "FrgC", entry.foreground) ||
        !set_smart_filter_color(item, "BckC", entry.background)) {
      return false;
    }
    auto* name = mutable_value_either(item, "Nm  ", "Nm");
    auto* filter_id =
        const_cast<DescriptorValue*>(descriptor_value(item, "filterID"));
    auto* blend =
        const_cast<DescriptorObject*>(descriptor_object(item, "blendOptions"));
    auto* filter = const_cast<DescriptorObject*>(descriptor_object(item, "Fltr"));
    if (name == nullptr || name->type != DescriptorValue::Type::String ||
        filter_id == nullptr || filter_id->type != DescriptorValue::Type::Integer ||
        blend == nullptr || blend->class_id != "blendOptions" || filter == nullptr ||
        filter->class_id != "GsnB") {
      return false;
    }
    if (!entry.native_name.empty()) {
      name->string_value = entry.native_name;
    }
    filter_id->integer_value = static_cast<std::int32_t>(0x47736e42U);
    auto* opacity =
        const_cast<DescriptorValue*>(descriptor_value(*blend, "Opct"));
    auto* mode = mutable_value_either(*blend, "Md  ", "Md");
    auto* radius = mutable_value_either(*filter, "Rds ", "Rds");
    if (opacity == nullptr || opacity->type != DescriptorValue::Type::UnitFloat ||
        opacity->unit != "#Prc" || mode == nullptr ||
        mode->type != DescriptorValue::Type::Enum || mode->enum_type != "BlnM" ||
        radius == nullptr || radius->type != DescriptorValue::Type::UnitFloat ||
        radius->unit != "#Pxl") {
      return false;
    }
    opacity->double_value = std::clamp(entry.opacity, 0.0, 1.0) * 100.0;
    mode->enum_value = std::string(blend_mode_lfx2_string(entry.blend_mode));
    mode->enum_value_long_form = true;
    radius->double_value = gaussian->radius_pixels;
  }
  return true;
}

bool apply_smart_filter_descriptor_edit(DescriptorObject& descriptor,
                                        SmartFilterDescriptorEdit edit) {
  if (edit.action == SmartFilterDescriptorAction::Preserve) {
    return true;
  }
  auto found = descriptor.values.find("filterFX");
  if (edit.action == SmartFilterDescriptorAction::Remove) {
    descriptor.values.erase("filterFX");
    descriptor.key_order.erase(
        std::remove_if(descriptor.key_order.begin(), descriptor.key_order.end(),
                       [](const DescriptorObject::KeyEntry& entry) {
                         return entry.key == "filterFX";
                       }),
        descriptor.key_order.end());
    return true;
  }
  if (edit.stack == nullptr) {
    return false;
  }
  if (!edit.entry_sources.empty() &&
      edit.entry_sources.size() != edit.stack->entries.size()) {
    return false;
  }
  if (found != descriptor.values.end()) {
    return patch_smart_filter_descriptor(found->second, *edit.stack,
                                         edit.entry_sources);
  }
  if (!edit.entry_sources.empty() &&
      std::any_of(edit.entry_sources.begin(), edit.entry_sources.end(),
                  [](const std::optional<std::size_t>& source) {
                    return source.has_value();
                  })) {
    return false;
  }
  auto authored = make_smart_filter_descriptor(*edit.stack);
  if (!authored.has_value()) {
    return false;
  }
  const auto insertion = std::find_if(
      descriptor.key_order.begin(), descriptor.key_order.end(),
      [](const DescriptorObject::KeyEntry& entry) { return entry.key == "comp"; });
  descriptor.key_order.insert(insertion, {"filterFX", true});
  descriptor.values.emplace("filterFX", std::move(*authored));
  return true;
}

const DescriptorValue* descriptor_value_either(const DescriptorObject& object,
                                                std::string_view first,
                                                std::string_view second) {
  if (const auto* value = descriptor_value(object, first); value != nullptr) {
    return value;
  }
  return descriptor_value(object, second);
}

bool read_smart_filter_bool(const DescriptorObject& object, std::string_view key,
                            bool& result) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::Bool) {
    return false;
  }
  result = value->bool_value;
  return true;
}

std::optional<RgbColor> smart_filter_rgb_color(const DescriptorObject& parent,
                                               std::string_view key) {
  const auto* color = descriptor_object(parent, key);
  if (color == nullptr || color->class_id != "RGBC") {
    return std::nullopt;
  }
  const auto read_channel = [color](std::string_view key_text) -> std::optional<std::uint8_t> {
    const auto* value = descriptor_value(*color, key_text);
    if (value == nullptr || (value->type != DescriptorValue::Type::Double &&
                             value->type != DescriptorValue::Type::Integer)) {
      return std::nullopt;
    }
    const double number = value->type == DescriptorValue::Type::Double
                              ? value->double_value
                              : static_cast<double>(value->integer_value);
    if (!std::isfinite(number)) {
      return std::nullopt;
    }
    return static_cast<std::uint8_t>(std::clamp(std::lround(number), 0L, 255L));
  };
  const auto red = read_channel("Rd  ");
  const auto green = read_channel("Grn ");
  const auto blue = read_channel("Bl  ");
  if (!red.has_value() || !green.has_value() || !blue.has_value()) {
    return std::nullopt;
  }
  return RgbColor{*red, *green, *blue};
}

std::optional<SmartFilterStack> smart_filter_stack_from_descriptor(
    const DescriptorObject& placed_descriptor) {
  const auto* filter_value = descriptor_value(placed_descriptor, "filterFX");
  if (filter_value == nullptr) {
    return std::nullopt;
  }

  SmartFilterStack stack;
  bool supported = filter_value->type == DescriptorValue::Type::Object &&
                   filter_value->object_value != nullptr;
  if (!supported) {
    return stack;
  }
  const auto& root = *filter_value->object_value;
  supported = root.class_id == "filterFXStyle";
  const bool root_enabled_valid = read_smart_filter_bool(root, "enab", stack.enabled);
  const bool root_position_valid =
      read_smart_filter_bool(root, "validAtPosition", stack.valid_at_position);
  const bool mask_enabled_valid =
      read_smart_filter_bool(root, "filterMaskEnable", stack.mask.enabled);
  const bool mask_linked_valid =
      read_smart_filter_bool(root, "filterMaskLinked", stack.mask.linked);
  const bool mask_extension_valid = read_smart_filter_bool(
      root, "filterMaskExtendWithWhite", stack.mask.extend_with_white);
  const bool root_flags_valid = root_enabled_valid && root_position_valid &&
                                mask_enabled_valid && mask_linked_valid &&
                                mask_extension_valid;
  // Photoshop 27.8 ignored attempts to author a linked Smart Filter mask, so
  // true-state behavior is not calibrated. Preserve it, but keep the layer
  // preview-locked until linked mask semantics are implemented.
  supported = supported && root_flags_valid && !stack.mask.linked;
  stack.mask.default_color = stack.mask.extend_with_white ? 255 : 0;

  const auto* list = descriptor_value(root, "filterFXList");
  if (list == nullptr || list->type != DescriptorValue::Type::List || list->list_value.empty()) {
    supported = false;
  } else {
    stack.entries.reserve(list->list_value.size());
    for (const auto& item : list->list_value) {
      SmartFilterEntry entry;
      bool entry_supported = item.type == DescriptorValue::Type::Object && item.object_value != nullptr;
      if (!entry_supported) {
        stack.entries.push_back(std::move(entry));
        supported = false;
        continue;
      }

      const auto& native_entry = *item.object_value;
      entry_supported = native_entry.class_id == "filterFX";
      const auto* name =
          descriptor_value_either(native_entry, "Nm  ", "Nm");
      if (name != nullptr && name->type == DescriptorValue::Type::String) {
        entry.native_name = name->string_value;
      } else {
        entry_supported = false;
      }
      const bool entry_enabled_valid =
          read_smart_filter_bool(native_entry, "enab", entry.enabled);
      const bool entry_options_valid =
          read_smart_filter_bool(native_entry, "hasoptions", entry.has_options);
      const bool entry_flags_valid = entry_enabled_valid && entry_options_valid;
      entry_supported = entry_supported && entry_flags_valid;
      if (const auto* id = descriptor_value(native_entry, "filterID");
          id != nullptr && id->type == DescriptorValue::Type::Integer) {
        entry.native_filter_id = static_cast<std::uint32_t>(id->integer_value);
      } else {
        entry_supported = false;
      }

      if (const auto* blend = descriptor_object(native_entry, "blendOptions");
          blend != nullptr && blend->class_id == "blendOptions") {
        const auto* opacity = descriptor_value_either(*blend, "Opct", "Opct");
        if (opacity != nullptr && opacity->type == DescriptorValue::Type::UnitFloat &&
            opacity->unit == "#Prc" && std::isfinite(opacity->double_value) &&
            opacity->double_value >= 0.0 && opacity->double_value <= 100.0) {
          entry.opacity = opacity->double_value / 100.0;
        } else {
          entry_supported = false;
        }
        const auto* blend_mode = descriptor_value_either(*blend, "Md  ", "Md");
        if (blend_mode == nullptr || blend_mode->type != DescriptorValue::Type::Enum ||
            blend_mode->enum_type != "BlnM" ||
            !known_smart_filter_blend_mode(blend_mode->enum_value, entry.blend_mode)) {
          entry_supported = false;
        }
      } else {
        entry_supported = false;
      }

      if (const auto foreground = smart_filter_rgb_color(native_entry, "FrgC");
          foreground.has_value()) {
        entry.foreground = *foreground;
      } else {
        entry_supported = false;
      }
      if (const auto background = smart_filter_rgb_color(native_entry, "BckC");
          background.has_value()) {
        entry.background = *background;
      } else {
        entry_supported = false;
      }

      const auto* filter = descriptor_object(native_entry, "Fltr");
      if (filter != nullptr) {
        entry.native_class_id = filter->class_id;
      }
      if (filter != nullptr && filter->class_id == "GsnB" &&
          entry.native_filter_id == 0x47736e42U) {
        const auto* radius = descriptor_value_either(*filter, "Rds ", "Rds");
        if (radius != nullptr && radius->type == DescriptorValue::Type::UnitFloat &&
            radius->unit == "#Pxl" && std::isfinite(radius->double_value) &&
            radius->double_value >= 0.1 && radius->double_value <= 1000.0) {
          entry.kind = SmartFilterKind::GaussianBlur;
          entry.parameters = GaussianBlurSmartFilter{radius->double_value};
        } else {
          entry_supported = false;
        }
      } else {
        entry_supported = false;
      }

      if (!entry_supported) {
        entry.kind = SmartFilterKind::Unsupported;
      }
      supported = supported && entry_supported;
      // Photoshop serializes filterFXList in native execution order; its layer
      // panel presents the reverse order. Multi-filter COM fixtures pin this.
      stack.entries.push_back(std::move(entry));
    }
  }
  stack.support = supported ? SmartFilterStackSupport::Supported
                            : SmartFilterStackSupport::Unsupported;
  return stack;
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
    info.smart_filters = smart_filter_stack_from_descriptor(descriptor);

    // Extract the warp object (present in every SoLd; meaningful when style is not
    // warpNone or a custom envelope mesh exists).
    if (const auto* warp_object = descriptor_object(descriptor, "warp"); warp_object != nullptr) {
      SmartObjectWarp warp;
      if (const auto* style = descriptor_value(*warp_object, "warpStyle");
          style != nullptr && style->type == DescriptorValue::Type::Enum) {
        warp.style = style->enum_value;
      }
      warp.value = descriptor_number(*warp_object, "warpValue", 0.0);
      warp.perspective = descriptor_number(*warp_object, "warpPerspective", 0.0);
      warp.perspective_other = descriptor_number(*warp_object, "warpPerspectiveOther", 0.0);
      if (const auto* rotate = descriptor_value(*warp_object, "warpRotate");
          rotate != nullptr && rotate->type == DescriptorValue::Type::Enum) {
        warp.rotate = rotate->enum_value;
      }
      if (const auto* bounds = descriptor_object(*warp_object, "bounds"); bounds != nullptr) {
        warp.bounds_top = descriptor_number(*bounds, "Top ", 0.0);
        warp.bounds_left = descriptor_number(*bounds, "Left", 0.0);
        warp.bounds_bottom = descriptor_number(*bounds, "Btom", 0.0);
        warp.bounds_right = descriptor_number(*bounds, "Rght", 0.0);
      }
      warp.u_order = static_cast<int>(descriptor_number(*warp_object, "uOrder", 4.0));
      warp.v_order = static_cast<int>(descriptor_number(*warp_object, "vOrder", 4.0));
      if (const auto* envelope = descriptor_object(*warp_object, "customEnvelopeWarp"); envelope != nullptr) {
        if (const auto* points = descriptor_value(*envelope, "meshPoints");
            points != nullptr && points->type == DescriptorValue::Type::ObjectArray &&
            points->object_value != nullptr) {
          const auto* horizontal = descriptor_value(*points->object_value, "Hrzn");
          const auto* vertical = descriptor_value(*points->object_value, "Vrtc");
          if (horizontal != nullptr && horizontal->type == DescriptorValue::Type::UnitFloatArray &&
              vertical != nullptr && vertical->type == DescriptorValue::Type::UnitFloatArray) {
            warp.mesh_xs = horizontal->unit_floats;
            warp.mesh_ys = vertical->unit_floats;
          }
        }
      }
      if (warp.style != "warpNone" || !warp.mesh_xs.empty() || warp.value != 0.0 ||
          warp.perspective != 0.0 || warp.perspective_other != 0.0) {
        info.warp = std::move(warp);
      }
    }

    if (auto warp_reason = warp_lock_reason(descriptor); !warp_reason.empty()) {
      // A SUPPORTED warp re-renders in Patchy: zero perspective, no quiltWarp, and
      // Trnf == nonAffineTransform (the renderer maps the mesh hull onto Trnf, so an
      // extra perspective in nonAffine would be dropped silently), plus either a
      // sane custom-envelope mesh or a preset style Patchy can bake itself (E9).
      const auto non_affine = transform_from_list(descriptor_value(descriptor, "nonAffineTransform"));
      const bool base_supported =
          info.warp.has_value() && info.warp->perspective == 0.0 &&
          info.warp->perspective_other == 0.0 && descriptor_value(descriptor, "quiltWarp") == nullptr &&
          (!non_affine.has_value() || !transforms_differ(*transform, *non_affine));
      bool supported =
          base_supported && !info.warp->mesh_xs.empty() &&
          info.warp->mesh_xs.size() == info.warp->mesh_ys.size() && info.warp->u_order >= 2 &&
          info.warp->u_order <= 4 && info.warp->v_order >= 2 && info.warp->v_order <= 4 &&
          info.warp->mesh_xs.size() ==
              static_cast<std::size_t>(info.warp->u_order) * static_cast<std::size_t>(info.warp->v_order);
      if (!supported && base_supported && info.warp->mesh_xs.empty() &&
          can_generate_style_warp_mesh(info.warp->style) &&
          (info.warp->rotate == "Hrzn" || info.warp->rotate == "Vrtc")) {
        // Style-only SoLd (no customEnvelopeWarp): synthesize the mesh Photoshop
        // would bake, for RENDERING only -- mesh_generated keeps the writer from
        // adding meshPoints the file never had.
        const auto generated = generate_style_warp_mesh(
            info.warp->style, info.warp->value, info.warp->rotate == "Vrtc",
            info.warp->bounds_right - info.warp->bounds_left,
            info.warp->bounds_bottom - info.warp->bounds_top);
        if (generated.has_value()) {
          info.warp->u_order = generated->u_order;
          info.warp->v_order = generated->v_order;
          info.warp->mesh_xs = generated->xs;
          info.warp->mesh_ys = generated->ys;
          info.warp->mesh_generated = true;
          supported = true;
        }
      }
      if (!supported) {
        info.lock_reason = warp_reason;
      }
    } else if (const auto non_affine = transform_from_list(descriptor_value(descriptor, "nonAffineTransform"));
               non_affine.has_value() && transforms_differ(*transform, *non_affine)) {
      info.lock_reason = "non_affine";
    } else if (!quad_is_affine(*transform)) {
      info.lock_reason = "non_affine";
    }
    if (info.smart_filters.has_value() && info.lock_reason.empty()) {
      // The global FEid association is finalized only after every document
      // block has been parsed. Until then a filter-bearing layer stays locked;
      // finalize_smart_filter_layers promotes a wholly supported stack.
      info.lock_reason = "filters";
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
  // Trailer (layout pinned from the 10cm-table-tent capture, v7 elements): liFE
  // carries an 'ExternalFileLink' descriptor {descVersion, Nm, fullPath, originalPath,
  // relPath}, a 16-byte modification-date struct, and the linked file's byte size;
  // every kind then carries the versioned tail (v5+ child doc id, v6+ asset mod time,
  // v7+ lock state). Best-effort: any surprise degrades to the verbatim skip below,
  // and untouched elements still re-emit byte-identically from original_element_bytes.
  try {
    if (parsed.source.kind == SmartObjectSourceKind::ExternalFile && reader.position() + 4U <= element_end) {
      (void)reader.read_u32();  // descriptor version (16)
      const auto link = read_descriptor(reader);
      parsed.source.external_link_desc_version =
          static_cast<std::int32_t>(descriptor_number(link, "descVersion", 2.0));
      const auto link_text = [&link](const char* key) -> std::string {
        const auto* value = descriptor_value(link, key);
        if (value != nullptr && value->type == DescriptorValue::Type::String) {
          return value->string_value;
        }
        return {};
      };
      parsed.source.external_full_path = link_text("fullPath");
      parsed.source.external_original_path = link_text("originalPath");
      parsed.source.external_rel_path = link_text("relPath");
      if (version > 3 && reader.position() + 16U <= element_end) {
        parsed.source.external_mod_year = static_cast<std::int32_t>(reader.read_u32());
        parsed.source.external_mod_month = reader.read_u8();
        parsed.source.external_mod_day = reader.read_u8();
        parsed.source.external_mod_hour = reader.read_u8();
        parsed.source.external_mod_minute = reader.read_u8();
        parsed.source.external_mod_seconds = read_f64(reader);
      }
      if (version > 3 && reader.position() + 8U <= element_end) {
        parsed.source.external_file_size = reader.read_u64();
      }
    }
    if (version >= 5 && reader.position() + 4U <= element_end) {
      parsed.source.child_doc_id = read_descriptor_unicode_string(reader);
    }
    if (version >= 6 && reader.position() + 8U <= element_end) {
      parsed.source.asset_mod_time = read_f64(reader);
    }
    if (version >= 7 && reader.position() < element_end) {
      parsed.source.asset_lock_state = reader.read_u8();
    }
  } catch (const std::exception&) {
    // Unmodeled trailer variant: the verbatim skip below keeps the element intact.
  }
  // Everything left (liFE variants, version 8+ trailers) rides inside the verbatim
  // element span; skip to the end.
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

// Serializes a fresh version-7 'liFE' (linked external file) element mirroring
// Photoshop's own layout byte-for-byte in shape (pinned from the 10cm-table-tent
// capture; see docs/smart-objects.md): NUL-padded creator, open descriptor {null; compInfo},
// ExternalFileLink descriptor, date struct, file size, then the versioned tail.
std::vector<std::uint8_t> serialize_external_element(const SmartObjectSource& source) {
  const auto text = [](std::string value) {
    DescriptorValue result;
    result.type = DescriptorValue::Type::String;
    result.string_value = std::move(value);
    return result;
  };
  const auto integer = [](std::int32_t value) {
    DescriptorValue result;
    result.type = DescriptorValue::Type::Integer;
    result.integer_value = value;
    return result;
  };
  const auto add = [](DescriptorObject& object, std::string key, bool long_form, DescriptorValue value) {
    object.key_order.push_back(DescriptorObject::KeyEntry{key, long_form});
    object.values.emplace(std::move(key), std::move(value));
  };

  BigEndianWriter body;
  for (const char ch : {'l', 'i', 'F', 'E'}) {
    body.write_u8(static_cast<std::uint8_t>(ch));
  }
  body.write_u32(7);  // version
  write_pascal_string(body, source.uuid);
  write_descriptor_unicode_string(body, source.filename);
  const auto write_ostype = [&body](std::string_view type, char pad) {
    for (std::size_t i = 0; i < 4U; ++i) {
      body.write_u8(static_cast<std::uint8_t>(i < type.size() ? type[i] : pad));
    }
  };
  write_ostype(source.filetype, ' ');
  // Photoshop writes the liFE creator as four NUL bytes.
  write_ostype(std::string_view{}, '\0');
  body.write_u64(0);  // datasize: external elements embed no bytes
  body.write_u8(1);   // open descriptor present
  {
    DescriptorObject open_descriptor;
    open_descriptor.class_id = "null";
    auto comp_info = DescriptorValue{};
    comp_info.type = DescriptorValue::Type::Object;
    comp_info.object_value = std::make_shared<DescriptorObject>();
    comp_info.object_value->class_id = "null";
    add(*comp_info.object_value, "compID", true, integer(-1));
    add(*comp_info.object_value, "originalCompID", true, integer(-1));
    add(open_descriptor, "compInfo", true, std::move(comp_info));
    body.write_u32(16);
    write_descriptor(body, open_descriptor);
  }
  {
    DescriptorObject link;
    link.class_id = "ExternalFileLink";
    link.class_id_long_form = true;
    add(link, "descVersion", true, integer(source.external_link_desc_version));
    add(link, "Nm  ", false, text(source.filename));
    add(link, "fullPath", true, text(source.external_full_path));
    add(link, "originalPath", true, text(source.external_original_path));
    add(link, "relPath", true, text(source.external_rel_path));
    body.write_u32(16);
    write_descriptor(body, link);
  }
  body.write_u32(static_cast<std::uint32_t>(source.external_mod_year));
  body.write_u8(source.external_mod_month);
  body.write_u8(source.external_mod_day);
  body.write_u8(source.external_mod_hour);
  body.write_u8(source.external_mod_minute);
  write_f64(body, source.external_mod_seconds);
  body.write_u64(source.external_file_size);
  write_descriptor_unicode_string(body, source.child_doc_id);
  write_f64(body, source.asset_mod_time);
  body.write_u8(source.asset_lock_state);

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
    if (source.kind == SmartObjectSourceKind::ExternalFile) {
      const auto element = serialize_external_element(source);
      payload.insert(payload.end(), element.begin(), element.end());
      continue;
    }
    if (source.kind != SmartObjectSourceKind::Embedded) {
      // Alias sources are never authored or edited by Patchy; while clean they
      // re-emit verbatim above. A dirty one without original bytes cannot round-trip.
      continue;
    }
    const auto element = serialize_embedded_element(source);
    payload.insert(payload.end(), element.begin(), element.end());
  }
  return payload;
}

std::optional<std::vector<std::uint8_t>> regenerate_placed_layer_payload(
    std::string_view key, std::span<const std::uint8_t> original_payload, const SmartObjectPlacement& placement,
    const SmartObjectWarp* warp, std::string_view placed_uuid,
    SmartFilterDescriptorEdit smart_filter_edit) {
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
      if (!placed_uuid.empty()) {
        if (auto* placed = const_cast<DescriptorValue*>(descriptor_value(descriptor, "placed"));
            placed != nullptr && placed->type == DescriptorValue::Type::String) {
          placed->string_value = std::string(placed_uuid);
        }
      }
      // Trnf takes the new quad; nonAffineTransform moves by the SAME per-corner
      // delta instead of being overwritten, so a translated non-affine placement
      // keeps its perspective (for editable layers the two are equal either way).
      const auto original_transform = transform_from_list(descriptor_value(descriptor, "Trnf"));
      if (auto* value = const_cast<DescriptorValue*>(descriptor_value(descriptor, "Trnf"));
          value != nullptr && value->type == DescriptorValue::Type::List && value->list_value.size() == 8U) {
        for (std::size_t i = 0; i < 8U; ++i) {
          value->list_value[i].double_value = placement.transform[i];
        }
      }
      if (auto* non_affine = const_cast<DescriptorValue*>(descriptor_value(descriptor, "nonAffineTransform"));
          non_affine != nullptr && non_affine->type == DescriptorValue::Type::List &&
          non_affine->list_value.size() == 8U && original_transform.has_value()) {
        for (std::size_t i = 0; i < 8U; ++i) {
          non_affine->list_value[i].double_value += placement.transform[i] - (*original_transform)[i];
        }
      }
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
      const auto set_warp_bounds = [](DescriptorObject& bounds, double top, double left, double bottom,
                                      double right) {
        const auto set_bound = [&bounds](const char* bound_key, double bound_value) {
          if (auto* value = const_cast<DescriptorValue*>(descriptor_value(bounds, bound_key));
              value != nullptr && value->type == DescriptorValue::Type::Double) {
            value->double_value = bound_value;
          }
        };
        set_bound("Top ", top);
        set_bound("Left", left);
        set_bound("Btom", bottom);
        set_bound("Rght", right);
      };
      if (auto* warp_object = const_cast<DescriptorObject*>(descriptor_object(descriptor, "warp"));
          warp_object != nullptr) {
        if (warp != nullptr) {
          // Patch the whole warp from the layer's metadata (the warp tool / warp-aware
          // rescales own these values).
          if (auto* style = const_cast<DescriptorValue*>(descriptor_value(*warp_object, "warpStyle"));
              style != nullptr && style->type == DescriptorValue::Type::Enum) {
            style->enum_value = warp->style;
          }
          const auto set_number = [warp_object](const char* number_key, double number_value) {
            if (auto* value = const_cast<DescriptorValue*>(descriptor_value(*warp_object, number_key));
                value != nullptr && value->type == DescriptorValue::Type::Double) {
              value->double_value = number_value;
            }
          };
          set_number("warpValue", warp->value);
          set_number("warpPerspective", warp->perspective);
          set_number("warpPerspectiveOther", warp->perspective_other);
          if (auto* rotate = const_cast<DescriptorValue*>(descriptor_value(*warp_object, "warpRotate"));
              rotate != nullptr && rotate->type == DescriptorValue::Type::Enum) {
            rotate->enum_value = warp->rotate;
          }
          if (auto* bounds = const_cast<DescriptorObject*>(descriptor_object(*warp_object, "bounds"));
              bounds != nullptr) {
            set_warp_bounds(*bounds, warp->bounds_top, warp->bounds_left, warp->bounds_bottom,
                            warp->bounds_right);
          }
          const auto set_order = [warp_object](const char* order_key, int order_value) {
            if (auto* value = const_cast<DescriptorValue*>(descriptor_value(*warp_object, order_key));
                value != nullptr && value->type == DescriptorValue::Type::Integer) {
              value->integer_value = order_value;
            }
          };
          // A generated mesh exists for rendering only: a style-only SoLd keeps its
          // own orders and stays without customEnvelopeWarp (Photoshop re-derives
          // the bake from the style, and injecting meshPoints it never wrote could
          // change how PS interprets the file).
          if (!warp->mesh_generated) {
            set_order("uOrder", warp->u_order);
            set_order("vOrder", warp->v_order);
          }
          if (!warp->mesh_xs.empty() && !warp->mesh_generated) {
            auto* envelope = const_cast<DescriptorObject*>(descriptor_object(*warp_object, "customEnvelopeWarp"));
            if (envelope == nullptr) {
              // Freshly warped in Patchy: insert the envelope after vOrder (PS's order).
              DescriptorValue envelope_value;
              envelope_value.type = DescriptorValue::Type::Object;
              envelope_value.object_value = std::make_shared<DescriptorObject>();
              envelope_value.object_value->class_id = "customEnvelopeWarp";
              envelope_value.object_value->class_id_long_form = true;
              DescriptorValue points_value;
              points_value.type = DescriptorValue::Type::ObjectArray;
              points_value.object_value = std::make_shared<DescriptorObject>();
              points_value.object_value->class_id = "rationalPoint";
              points_value.object_value->class_id_long_form = true;
              DescriptorValue horizontal;
              horizontal.type = DescriptorValue::Type::UnitFloatArray;
              horizontal.unit = "#Pxl";
              DescriptorValue vertical = horizontal;
              points_value.object_value->key_order.push_back({"Hrzn", false});
              points_value.object_value->values.emplace("Hrzn", std::move(horizontal));
              points_value.object_value->key_order.push_back({"Vrtc", false});
              points_value.object_value->values.emplace("Vrtc", std::move(vertical));
              envelope_value.object_value->key_order.push_back({"meshPoints", true});
              envelope_value.object_value->values.emplace("meshPoints", std::move(points_value));
              warp_object->key_order.push_back({"customEnvelopeWarp", true});
              warp_object->values.emplace("customEnvelopeWarp", std::move(envelope_value));
              envelope = const_cast<DescriptorObject*>(descriptor_object(*warp_object, "customEnvelopeWarp"));
            }
            if (auto* points = const_cast<DescriptorValue*>(descriptor_value(*envelope, "meshPoints"));
                points != nullptr && points->type == DescriptorValue::Type::ObjectArray &&
                points->object_value != nullptr) {
              points->integer_value = static_cast<std::int32_t>(warp->mesh_xs.size());
              if (auto* horizontal =
                      const_cast<DescriptorValue*>(descriptor_value(*points->object_value, "Hrzn"));
                  horizontal != nullptr && horizontal->type == DescriptorValue::Type::UnitFloatArray) {
                horizontal->unit_floats = warp->mesh_xs;
              }
              if (auto* vertical =
                      const_cast<DescriptorValue*>(descriptor_value(*points->object_value, "Vrtc"));
                  vertical != nullptr && vertical->type == DescriptorValue::Type::UnitFloatArray) {
                vertical->unit_floats = warp->mesh_ys;
              }
            }
          }
        } else {
          // Unwarped placements keep their warp bounds as the CONTENT rect
          // (0,0,height,width): the E5 captures show Photoshop rewriting them to the
          // new content size on replace, never to document coordinates.
          const auto* style = descriptor_value(*warp_object, "warpStyle");
          const bool warp_none = style != nullptr && style->type == DescriptorValue::Type::Enum &&
                                 style->enum_value == "warpNone";
          if (warp_none) {
            if (auto* bounds = const_cast<DescriptorObject*>(descriptor_object(*warp_object, "bounds"));
                bounds != nullptr) {
              set_warp_bounds(*bounds, 0.0, 0.0, placement.height, placement.width);
            }
          }
        }
      }
      if (!apply_smart_filter_descriptor_edit(descriptor, smart_filter_edit)) {
        return std::nullopt;
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

std::vector<std::uint8_t> author_placed_layer_sold_payload(const SmartObjectPlacement& placement,
                                                           std::string_view placed_uuid,
                                                           const SmartFilterStack* smart_filters) {
  const auto text = [](std::string value) {
    DescriptorValue result;
    result.type = DescriptorValue::Type::String;
    result.string_value = std::move(value);
    return result;
  };
  const auto integer = [](std::int32_t value) {
    DescriptorValue result;
    result.type = DescriptorValue::Type::Integer;
    result.integer_value = value;
    return result;
  };
  const auto number = [](double value) {
    DescriptorValue result;
    result.type = DescriptorValue::Type::Double;
    result.double_value = value;
    return result;
  };
  const auto make_object = [](std::string class_id, bool class_long_form) {
    DescriptorValue result;
    result.type = DescriptorValue::Type::Object;
    result.object_value = std::make_shared<DescriptorObject>();
    result.object_value->class_id = std::move(class_id);
    result.object_value->class_id_long_form = class_long_form;
    return result;
  };
  const auto make_enum = [](std::string enum_type, bool type_long_form, std::string enum_value,
                            bool value_long_form) {
    DescriptorValue result;
    result.type = DescriptorValue::Type::Enum;
    result.enum_type = std::move(enum_type);
    result.enum_type_long_form = type_long_form;
    result.enum_value = std::move(enum_value);
    result.enum_value_long_form = value_long_form;
    return result;
  };
  const auto add = [](DescriptorObject& object, std::string key, bool long_form, DescriptorValue value) {
    object.key_order.push_back(DescriptorObject::KeyEntry{key, long_form});
    object.values.emplace(std::move(key), std::move(value));
  };
  const auto quad_list = [&placement]() {
    DescriptorValue result;
    result.type = DescriptorValue::Type::List;
    for (const auto coordinate : placement.transform) {
      DescriptorValue item;
      item.type = DescriptorValue::Type::Double;
      item.double_value = coordinate;
      result.list_value.push_back(std::move(item));
    }
    return result;
  };
  const auto frame_rational = [&] {
    auto result = make_object("null", false);
    add(*result.object_value, "numerator", true, integer(0));
    add(*result.object_value, "denominator", true, integer(600));
    return result;
  };

  // Field order and id forms mirror Photoshop 2026's own converted/placed SoLd
  // byte-for-byte in shape (E1 captures; see docs/smart-objects.md).
  DescriptorObject root;
  root.class_id = "null";
  add(root, "Idnt", false, text(placement.uuid));
  add(root, "placed", true, text(std::string(placed_uuid)));
  add(root, "PgNm", false, integer(1));
  add(root, "totalPages", true, integer(1));
  add(root, "Crop", false, integer(1));
  add(root, "frameStep", true, frame_rational());
  add(root, "duration", true, frame_rational());
  add(root, "frameCount", true, integer(1));
  add(root, "Annt", false, integer(placement.anti_alias));
  add(root, "Type", false, integer(placement.placed_type));
  add(root, "Trnf", false, quad_list());
  add(root, "nonAffineTransform", true, quad_list());
  auto warp = make_object("warp", true);
  add(*warp.object_value, "warpStyle", true, make_enum("warpStyle", true, "warpNone", true));
  add(*warp.object_value, "warpValue", true, number(0.0));
  add(*warp.object_value, "warpPerspective", true, number(0.0));
  add(*warp.object_value, "warpPerspectiveOther", true, number(0.0));
  add(*warp.object_value, "warpRotate", true, make_enum("Ornt", false, "Hrzn", false));
  auto warp_bounds = make_object("classFloatRect", true);
  add(*warp_bounds.object_value, "Top ", false, number(0.0));
  add(*warp_bounds.object_value, "Left", false, number(0.0));
  add(*warp_bounds.object_value, "Btom", false, number(placement.height));
  add(*warp_bounds.object_value, "Rght", false, number(placement.width));
  add(*warp.object_value, "bounds", true, std::move(warp_bounds));
  add(*warp.object_value, "uOrder", true, integer(4));
  add(*warp.object_value, "vOrder", true, integer(4));
  add(root, "warp", true, std::move(warp));
  auto size = make_object("Pnt ", false);
  add(*size.object_value, "Wdth", false, number(placement.width));
  add(*size.object_value, "Hght", false, number(placement.height));
  add(root, "Sz  ", false, std::move(size));
  DescriptorValue resolution;
  resolution.type = DescriptorValue::Type::UnitFloat;
  resolution.unit = "#Rsl";
  resolution.double_value = placement.resolution;
  add(root, "Rslt", false, std::move(resolution));
  if (smart_filters != nullptr) {
    auto filter_fx = make_smart_filter_descriptor(*smart_filters);
    if (!filter_fx.has_value()) {
      throw std::runtime_error("Unsupported authored Smart Filter stack");
    }
    add(root, "filterFX", true, std::move(*filter_fx));
  }
  add(root, "comp", false, integer(-1));
  auto comp_info = make_object("null", false);
  add(*comp_info.object_value, "compID", true, integer(-1));
  add(*comp_info.object_value, "originalCompID", true, integer(-1));
  add(root, "compInfo", true, std::move(comp_info));
  auto color_management = make_object("ClMg", false);
  add(*color_management.object_value, "placedLayerOCIOConversion", true,
      make_enum("placedLayerOCIOConversion", true, "placedLayerOCIOConvertEmbedded", true));
  add(root, "ClMg", false, std::move(color_management));

  BigEndianWriter writer;
  for (const char ch : {'s', 'o', 'L', 'D'}) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
  writer.write_u32(4);   // SoLd version
  writer.write_u32(16);  // descriptor version
  write_descriptor(writer, root);
  while ((writer.bytes().size() % 4U) != 0U) {
    writer.write_u8(0);
  }
  return writer.bytes();
}

}  // namespace patchy::psd
