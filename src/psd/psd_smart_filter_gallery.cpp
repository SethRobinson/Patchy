#include "psd/psd_smart_filter_gallery.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::psd {

namespace {

// One gallery effect Patchy renders: its GEft id and the integer settings Photoshop
// stores beside GEfk, in Photoshop's key order (October 2026 capture).
struct GalleryEffectField {
  const char* key;
  int minimum;
  int maximum;
};

struct GalleryEffectSpec {
  const char* effect_id;
  std::array<GalleryEffectField, 3> fields;
};

// Plastic Wrap's ranges are the native PlsW filter's (docs/smart-filters-native.md).
constexpr std::array<GalleryEffectSpec, 1> kEffects{{
    {"PlsW", {{{"Hghl", 0, 20}, {"Dtl ", 1, 15}, {"Smth", 1, 15}}}},
}};

const GalleryEffectSpec* effect_spec(std::string_view effect_id) {
  for (const auto& spec : kEffects) {
    if (effect_id == spec.effect_id) {
      return &spec;
    }
  }
  return nullptr;
}

// The GEft id of one GEfc effect object, or "" when GEfk is missing or malformed.
std::string effect_id_of(const DescriptorObject& effect) {
  const auto* kind = descriptor_value(effect, "GEfk");
  if (kind == nullptr || kind->type != DescriptorValue::Type::Enum || kind->enum_type != "GEft") {
    return {};
  }
  return kind->enum_value;
}

// Reads one effect. Every setting must be present: Photoshop's dialog writes them all,
// and the defaults an action may leave out are not captured.
std::optional<FilterGalleryEffect> read_effect(const DescriptorObject& effect) {
  const auto id = effect_id_of(effect);
  const auto* spec = effect_spec(id);
  if (spec == nullptr || effect.values.size() != spec->fields.size() + 1U) {
    return std::nullopt;
  }
  std::array<int, 3> values{};
  for (std::size_t index = 0; index < spec->fields.size(); ++index) {
    const auto& field = spec->fields[index];
    const auto* value = descriptor_value(effect, field.key);
    if (value == nullptr || value->type != DescriptorValue::Type::Integer || value->integer_value < field.minimum ||
        value->integer_value > field.maximum) {
      return std::nullopt;
    }
    values[index] = value->integer_value;
  }
  FilterGalleryEffect parsed;
  parsed.effect_id = id;
  parsed.settings = PlasticWrapSmartFilter{values[0], values[1], values[2]};
  return parsed;
}

// The effect objects of a GEfc Fltr: the object itself (one effect inline) or the
// objects of its GEfs list. Empty when the shape is anything else.
std::vector<const DescriptorObject*> effect_objects(const DescriptorObject& filter, bool& listed) {
  std::vector<const DescriptorObject*> effects;
  const auto* list = descriptor_value(filter, "GEfs");
  listed = list != nullptr;
  if (!listed) {
    effects.push_back(&filter);
    return effects;
  }
  if (list->type != DescriptorValue::Type::List || filter.values.size() != 1U || list->list_value.size() < 2U) {
    return {};
  }
  for (const auto& item : list->list_value) {
    if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr ||
        item.object_value->class_id != "GEfc") {
      return {};
    }
    effects.push_back(item.object_value.get());
  }
  return effects;
}

std::optional<FilterGallerySmartFilter> read_gallery(const DescriptorObject& filter) {
  FilterGallerySmartFilter gallery;
  const auto objects = effect_objects(filter, gallery.listed);
  if (objects.empty()) {
    return std::nullopt;
  }
  for (const auto* object : objects) {
    auto effect = read_effect(*object);
    if (!effect.has_value()) {
      return std::nullopt;
    }
    gallery.effects.push_back(std::move(*effect));
  }
  return gallery;
}

}  // namespace

bool parse_filter_gallery_smart_filter(const DescriptorObject* filter, SmartFilterEntry& entry,
                                       bool& entry_supported) {
  if (filter == nullptr || filter->class_id != "GEfc") {
    return false;
  }
  auto gallery = entry.native_filter_id == kFilterGalleryFilterId ? read_gallery(*filter) : std::nullopt;
  if (!gallery.has_value()) {
    entry_supported = false;
    return true;
  }
  entry.kind = SmartFilterKind::FilterGallery;
  entry.parameters = std::move(*gallery);
  return true;
}

bool write_filter_gallery_smart_filter(DescriptorObject& filter, const FilterGallerySmartFilter& gallery) {
  if (filter.class_id != "GEfc") {
    return false;
  }
  const auto stored = read_gallery(filter);
  if (!stored.has_value() || stored->listed != gallery.listed ||
      stored->effects.size() != gallery.effects.size()) {
    return false;
  }
  // Patchy edits settings only, never which effects run: patch the integer leaves in place
  // so every key, id form and order stays Photoshop's.
  auto* list = gallery.listed ? &filter.values.at("GEfs") : nullptr;
  for (std::size_t index = 0; index < gallery.effects.size(); ++index) {
    const auto& effect = gallery.effects[index];
    if (effect.effect_id != stored->effects[index].effect_id) {
      return false;
    }
    auto& object = list != nullptr ? *list->list_value[index].object_value : filter;
    const auto* spec = effect_spec(effect.effect_id);
    const auto* plastic = std::get_if<PlasticWrapSmartFilter>(&effect.settings);
    if (spec == nullptr || plastic == nullptr) {
      return false;
    }
    const std::array<int, 3> values{plastic->highlight_strength, plastic->detail, plastic->smoothness};
    for (std::size_t field = 0; field < spec->fields.size(); ++field) {
      const auto& range = spec->fields[field];
      if (values[field] < range.minimum || values[field] > range.maximum) {
        return false;
      }
      object.values.at(range.key).integer_value = values[field];
    }
  }
  return true;
}

}  // namespace patchy::psd
