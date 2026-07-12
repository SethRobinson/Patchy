#include "psd/grd_io.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace patchy::psd {
namespace {

constexpr std::size_t kMaxGrdBytes = 32U * 1024U * 1024U;
constexpr std::size_t kMaxGradients = 4096U;
constexpr std::size_t kMaxStops = 256U;

const DescriptorValue *value(const DescriptorObject &object,
                             std::string_view key) {
  const auto found = object.values.find(std::string(key));
  return found == object.values.end() ? nullptr : &found->second;
}

const DescriptorObject *object(const DescriptorObject &parent,
                               std::string_view key) {
  const auto *found = value(parent, key);
  return found != nullptr && found->type == DescriptorValue::Type::Object &&
                 found->object_value
             ? found->object_value.get()
             : nullptr;
}

double number(const DescriptorObject &object, std::string_view key,
              double fallback = 0.0) {
  const auto *found = value(object, key);
  if (found == nullptr)
    return fallback;
  if (found->type == DescriptorValue::Type::Integer)
    return found->integer_value;
  if (found->type == DescriptorValue::Type::Double ||
      found->type == DescriptorValue::Type::UnitFloat)
    return found->double_value;
  return fallback;
}

bool boolean(const DescriptorObject &object, std::string_view key,
             bool fallback) {
  const auto *found = value(object, key);
  return found != nullptr && found->type == DescriptorValue::Type::Bool
             ? found->bool_value
             : fallback;
}

std::string text(const DescriptorObject &object, std::string_view key) {
  const auto *found = value(object, key);
  return found != nullptr && found->type == DescriptorValue::Type::String
             ? found->string_value
             : std::string{};
}

std::string enum_text(const DescriptorObject &object, std::string_view key,
                      std::string fallback = {}) {
  const auto *found = value(object, key);
  return found != nullptr && found->type == DescriptorValue::Type::Enum
             ? found->enum_value
             : std::move(fallback);
}

std::string display_name(std::string source) {
  if (source.rfind("$$$/", 0) == 0) {
    const auto equals = source.rfind('=');
    if (equals != std::string::npos && equals + 1U < source.size())
      source.erase(0, equals + 1U);
  }
  return source;
}

RgbColor color(const DescriptorObject &stop) {
  const auto *c = object(stop, "Clr ");
  if (c == nullptr)
    return {};
  if (c->class_id == "Grsc") {
    const auto gray = static_cast<std::uint8_t>(
        std::clamp(std::lround(number(*c, "Gry ") * 2.55), 0L, 255L));
    return RgbColor{gray, gray, gray};
  }
  return RgbColor{static_cast<std::uint8_t>(
                      std::clamp(std::lround(number(*c, "Rd  ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(number(*c, "Grn ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(number(*c, "Bl  ")), 0L, 255L))};
}

std::array<std::uint16_t, 4> range(const DescriptorObject &object,
                                   std::string_view key,
                                   std::uint16_t fallback) {
  std::array<std::uint16_t, 4> result{fallback, fallback, fallback, fallback};
  const auto *list = value(object, key);
  if (list == nullptr || list->type != DescriptorValue::Type::List)
    return result;
  for (std::size_t i = 0; i < result.size() && i < list->list_value.size();
       ++i) {
    if (list->list_value[i].type == DescriptorValue::Type::Integer)
      result[i] = static_cast<std::uint16_t>(
          std::clamp(list->list_value[i].integer_value, 0, 100));
  }
  return result;
}

std::optional<GrdGradient> parse_gradient(const DescriptorObject &wrapper,
                                          std::string fallback,
                                          std::vector<std::string> &warnings) {
  const auto *source = object(wrapper, "Grad");
  if (source == nullptr)
    source = &wrapper;
  GrdGradient result;
  result.name = display_name(text(*source, "Nm  "));
  if (result.name.empty())
    result.name = std::move(fallback);
  result.definition.name = result.name;
  const auto form = enum_text(*source, "GrdF", "CstS");
  result.definition.form = form == "ClNs" ? GradientDefinitionForm::Noise
                                          : GradientDefinitionForm::Solid;
  result.definition.smoothness = static_cast<std::uint16_t>(std::clamp(
      static_cast<int>(std::lround(number(*source, "Intr", 4096.0))), 0, 4096));
  if (result.definition.form == GradientDefinitionForm::Noise) {
    auto &noise = result.definition.noise;
    noise.add_transparency = boolean(*source, "ShTr", false);
    noise.restrict_colors = boolean(*source, "VctC", true);
    const auto model = enum_text(*source, "ClrS", "RGBC");
    noise.color_model = model == "HSBC"   ? GradientNoiseColorModel::HSB
                        : model == "LABC" ? GradientNoiseColorModel::Lab
                                          : GradientNoiseColorModel::RGB;
    noise.seed =
        static_cast<std::uint32_t>(std::max(0.0, number(*source, "RndS")));
    noise.roughness = static_cast<std::uint16_t>(std::clamp(
        static_cast<int>(std::lround(number(*source, "Smth", 2048.0))), 0,
        4096));
    noise.minimum = range(*source, "Mnm ", 0);
    noise.maximum = range(*source, "Mxm ", 100);
    return result;
  }
  const auto *colors = value(*source, "Clrs");
  if (colors != nullptr && colors->type == DescriptorValue::Type::List) {
    for (const auto &item : colors->list_value) {
      if (item.type != DescriptorValue::Type::Object || !item.object_value)
        continue;
      const auto type = enum_text(*item.object_value, "Type", "UsrS");
      auto kind = GradientColorStop::Kind::User;
      if (type == "FrgC")
        kind = GradientColorStop::Kind::Foreground;
      if (type == "BckC")
        kind = GradientColorStop::Kind::Background;
      result.definition.color_stops.push_back(GradientColorStop{
          std::clamp(
              static_cast<float>(number(*item.object_value, "Lctn") / 4096.0),
              0.0F, 1.0F),
          kind == GradientColorStop::Kind::Background
              ? RgbColor{255, 255, 255}
              : color(*item.object_value),
          std::clamp(static_cast<float>(
                         number(*item.object_value, "Mdpn", 50.0) / 100.0),
                     0.0F, 1.0F),
          kind});
      if (result.definition.color_stops.size() >= kMaxStops)
        break;
    }
  }
  const auto *transparency = value(*source, "Trns");
  if (transparency != nullptr &&
      transparency->type == DescriptorValue::Type::List) {
    for (const auto &item : transparency->list_value) {
      if (item.type != DescriptorValue::Type::Object || !item.object_value)
        continue;
      result.definition.alpha_stops.push_back(GradientAlphaStop{
          std::clamp(
              static_cast<float>(number(*item.object_value, "Lctn") / 4096.0),
              0.0F, 1.0F),
          std::clamp(static_cast<float>(
                         number(*item.object_value, "Opct", 100.0) / 100.0),
                     0.0F, 1.0F),
          std::clamp(static_cast<float>(
                         number(*item.object_value, "Mdpn", 50.0) / 100.0),
                     0.0F, 1.0F)});
      if (result.definition.alpha_stops.size() >= kMaxStops)
        break;
    }
  }
  if (result.definition.color_stops.size() < 2U) {
    warnings.push_back(
        result.name +
        ": gradient has fewer than two color stops and was skipped");
    return std::nullopt;
  }
  if (result.definition.alpha_stops.empty())
    result.definition.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  return result;
}

DescriptorValue text_value(std::string value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::String;
  result.string_value = std::move(value);
  return result;
}
DescriptorValue integer_value(std::int32_t value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Integer;
  result.integer_value = value;
  return result;
}
DescriptorValue double_value(double value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Double;
  result.double_value = value;
  return result;
}
DescriptorValue bool_value(bool value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Bool;
  result.bool_value = value;
  return result;
}
DescriptorValue percent_value(double value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::UnitFloat;
  result.unit = "#Prc";
  result.double_value = value;
  return result;
}
DescriptorValue enum_value(std::string type, std::string value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Enum;
  result.enum_type = std::move(type);
  result.enum_value = std::move(value);
  return result;
}
DescriptorValue object_value(DescriptorObject object) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Object;
  result.object_value = std::make_shared<DescriptorObject>(std::move(object));
  return result;
}
void append(DescriptorObject &object, std::string key, DescriptorValue value) {
  const auto long_form = key.size() != 4U;
  object.key_order.push_back({key, long_form});
  object.values.emplace(std::move(key), std::move(value));
}

DescriptorObject rgb_object(RgbColor color) {
  DescriptorObject result;
  result.class_id = "RGBC";
  append(result, "Rd  ", double_value(color.red));
  append(result, "Grn ", double_value(color.green));
  append(result, "Bl  ", double_value(color.blue));
  return result;
}

DescriptorObject gradient_object(const GrdGradient &item) {
  const auto &definition = item.definition;
  DescriptorObject result;
  result.class_id = "Grdn";
  append(result, "Nm  ", text_value(item.name));
  append(result, "GrdF",
         enum_value("GrdF", definition.form == GradientDefinitionForm::Noise
                                ? "ClNs"
                                : "CstS"));
  if (definition.form == GradientDefinitionForm::Noise) {
    append(result, "ShTr", bool_value(definition.noise.add_transparency));
    append(result, "VctC", bool_value(definition.noise.restrict_colors));
    append(result, "ClrS",
           enum_value("ClrS", definition.noise.color_model ==
                                      GradientNoiseColorModel::HSB
                                  ? "HSBC"
                              : definition.noise.color_model ==
                                      GradientNoiseColorModel::Lab
                                  ? "LABC"
                                  : "RGBC"));
    append(result, "RndS",
           integer_value(static_cast<std::int32_t>(definition.noise.seed)));
    append(result, "Smth", integer_value(definition.noise.roughness));
    auto range_value = [](const std::array<std::uint16_t, 4> &values) {
      DescriptorValue list;
      list.type = DescriptorValue::Type::List;
      for (auto value : values)
        list.list_value.push_back(
            integer_value(std::clamp<std::uint16_t>(value, 0, 100)));
      return list;
    };
    append(result, "Mnm ", range_value(definition.noise.minimum));
    append(result, "Mxm ", range_value(definition.noise.maximum));
    return result;
  }
  append(result, "Intr", double_value(definition.smoothness));
  DescriptorValue colors;
  colors.type = DescriptorValue::Type::List;
  for (const auto &stop : definition.color_stops) {
    DescriptorObject entry;
    entry.class_id = "Clrt";
    if (stop.kind == GradientColorStop::Kind::User)
      append(entry, "Clr ", object_value(rgb_object(stop.color)));
    append(entry, "Type",
           enum_value("Clry", stop.kind == GradientColorStop::Kind::Foreground
                                  ? "FrgC"
                              : stop.kind == GradientColorStop::Kind::Background
                                  ? "BckC"
                                  : "UsrS"));
    append(entry, "Lctn",
           integer_value(static_cast<int>(
               std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F))));
    append(entry, "Mdpn",
           integer_value(static_cast<int>(
               std::lround(std::clamp(stop.midpoint, 0.0F, 1.0F) * 100.0F))));
    colors.list_value.push_back(object_value(std::move(entry)));
  }
  append(result, "Clrs", std::move(colors));
  DescriptorValue alphas;
  alphas.type = DescriptorValue::Type::List;
  for (const auto &stop : definition.alpha_stops) {
    DescriptorObject entry;
    entry.class_id = "TrnS";
    append(entry, "Opct",
           percent_value(std::clamp(stop.opacity, 0.0F, 1.0F) * 100.0));
    append(entry, "Lctn",
           integer_value(static_cast<int>(
               std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F))));
    append(entry, "Mdpn",
           integer_value(static_cast<int>(
               std::lround(std::clamp(stop.midpoint, 0.0F, 1.0F) * 100.0F))));
    alphas.list_value.push_back(object_value(std::move(entry)));
  }
  append(result, "Trns", std::move(alphas));
  return result;
}

std::vector<std::string> hierarchy_folders(const DescriptorObject &hierarchy,
                                           std::size_t count) {
  std::vector<std::string> folders(count);
  const auto *list = value(hierarchy, "hierarchy");
  if (list == nullptr || list->type != DescriptorValue::Type::List)
    return folders;
  std::vector<std::string> stack;
  std::size_t gradient = 0;
  for (const auto &item : list->list_value) {
    if (item.type != DescriptorValue::Type::Object || !item.object_value)
      continue;
    if (item.object_value->class_id == "Grup") {
      stack.push_back(display_name(text(*item.object_value, "Nm  ")));
    } else if (item.object_value->class_id == "groupEnd") {
      if (!stack.empty())
        stack.pop_back();
    } else if (item.object_value->class_id == "preset" &&
               gradient < folders.size()) {
      std::string path;
      for (const auto &part : stack) {
        if (!path.empty())
          path += '/';
        path += part;
      }
      folders[gradient++] = std::move(path);
    }
  }
  return folders;
}

std::vector<std::string> folder_parts(std::string_view folder) {
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin < folder.size()) {
    const auto end = folder.find('/', begin);
    const auto part = folder.substr(begin, end == std::string_view::npos
                                               ? folder.size() - begin
                                               : end - begin);
    if (!part.empty())
      parts.emplace_back(part);
    if (end == std::string_view::npos)
      break;
    begin = end + 1U;
  }
  return parts;
}

DescriptorValue empty_marker(std::string class_id) {
  DescriptorObject marker;
  marker.class_id = std::move(class_id);
  return object_value(std::move(marker));
}

DescriptorObject gradient_hierarchy(std::span<const GrdGradient> gradients) {
  DescriptorObject root;
  root.class_id = "null";
  DescriptorValue hierarchy;
  hierarchy.type = DescriptorValue::Type::List;
  std::vector<std::string> open_folders;
  for (const auto &gradient : gradients) {
    const auto target = folder_parts(gradient.folder);
    std::size_t common = 0;
    while (common < open_folders.size() && common < target.size() &&
           open_folders[common] == target[common]) {
      ++common;
    }
    while (open_folders.size() > common) {
      hierarchy.list_value.push_back(empty_marker("groupEnd"));
      open_folders.pop_back();
    }
    while (open_folders.size() < target.size()) {
      DescriptorObject group;
      group.class_id = "Grup";
      append(group, "Nm  ", text_value(target[open_folders.size()]));
      hierarchy.list_value.push_back(object_value(std::move(group)));
      open_folders.push_back(target[open_folders.size()]);
    }
    hierarchy.list_value.push_back(empty_marker("preset"));
  }
  while (!open_folders.empty()) {
    hierarchy.list_value.push_back(empty_marker("groupEnd"));
    open_folders.pop_back();
  }
  append(root, "hierarchy", std::move(hierarchy));
  return root;
}

} // namespace

std::optional<GrdReadResult> read_grd(std::span<const std::uint8_t> bytes,
                                      std::string &error) {
  error.clear();
  if (bytes.size() > kMaxGrdBytes) {
    error = "GRD file exceeds the 32 MiB import limit";
    return std::nullopt;
  }
  GrdReadResult result;
  try {
    BigEndianReader reader(bytes);
    if (key_string(read_signature(reader)) != "8BGR") {
      error = "Not a Photoshop GRD file";
      return std::nullopt;
    }
    const auto version = reader.read_u16();
    if (version != 5U) {
      error = "Unsupported GRD version " + std::to_string(version);
      return std::nullopt;
    }
    if (reader.read_u32() != 16U) {
      error = "Unsupported GRD descriptor version";
      return std::nullopt;
    }
    const auto root = read_descriptor(reader);
    const auto *list = value(root, "GrdL");
    if (list == nullptr || list->type != DescriptorValue::Type::List ||
        list->list_value.size() > kMaxGradients) {
      error = "GRD gradient list is missing or too large";
      return std::nullopt;
    }
    std::size_t index = 0;
    for (const auto &item : list->list_value) {
      ++index;
      if (item.type != DescriptorValue::Type::Object || !item.object_value) {
        result.warnings.push_back("Gradient " + std::to_string(index) +
                                  ": damaged entry was skipped");
        continue;
      }
      if (auto parsed = parse_gradient(*item.object_value,
                                       "Gradient " + std::to_string(index),
                                       result.warnings))
        result.gradients.push_back(std::move(*parsed));
    }
    if (reader.remaining() >= 12U &&
        key_string(read_signature(reader)) == "8BIM" &&
        key_string(read_signature(reader)) == "phry") {
      const auto length = reader.read_u32();
      if (length <= reader.remaining()) {
        BigEndianReader hierarchy_reader(
            bytes.subspan(reader.position(), length));
        if (hierarchy_reader.read_u32() == 16U) {
          const auto hierarchy = read_descriptor(hierarchy_reader);
          const auto folders =
              hierarchy_folders(hierarchy, result.gradients.size());
          for (std::size_t i = 0; i < folders.size(); ++i)
            result.gradients[i].folder = folders[i];
        }
      }
    }
  } catch (const std::exception &) {
    if (result.gradients.empty()) {
      error = "GRD file is damaged";
      return std::nullopt;
    }
    result.warnings.push_back(
        "The file is damaged past the last decoded gradient");
  }
  if (result.gradients.empty()) {
    error = "The file contains no usable gradients";
    return std::nullopt;
  }
  return result;
}

std::vector<std::uint8_t> write_grd(std::span<const GrdGradient> gradients) {
  BigEndianWriter writer;
  for (char ch : std::string_view("8BGR"))
    writer.write_u8(static_cast<std::uint8_t>(ch));
  writer.write_u16(5);
  writer.write_u32(16);
  DescriptorObject root;
  root.class_id = "null";
  DescriptorValue list;
  list.type = DescriptorValue::Type::List;
  for (const auto &gradient : gradients) {
    DescriptorObject wrapper;
    wrapper.name = "Gradient";
    wrapper.class_id = "Grdn";
    append(wrapper, "Grad", object_value(gradient_object(gradient)));
    list.list_value.push_back(object_value(std::move(wrapper)));
  }
  append(root, "GrdL", std::move(list));
  write_descriptor(writer, root);

  BigEndianWriter hierarchy_writer;
  hierarchy_writer.write_u32(16);
  write_descriptor(hierarchy_writer, gradient_hierarchy(gradients));
  for (char ch : std::string_view("8BIMphry"))
    writer.write_u8(static_cast<std::uint8_t>(ch));
  writer.write_u32(static_cast<std::uint32_t>(hierarchy_writer.bytes().size()));
  writer.write_bytes(hierarchy_writer.bytes());
  return writer.bytes();
}

} // namespace patchy::psd
