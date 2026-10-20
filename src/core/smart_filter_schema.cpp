#include "core/smart_filter_schema.hpp"

#include "support/translate_noop.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace patchy {
namespace {

// Keys, key order, types and ranges come from Photoshop 2026 COM captures and
// its filter dialogs (docs/smart-filters-distort-stylize.md). Never reorder
// fields: SchemaSmartFilter::values follows this order.

constexpr SmartFilterFieldOption kZigZagStyles[]{
    {"ArnC", "around_center", PATCHY_TRANSLATE_NOOP("QObject", "Around Center")},
    {"OtFC", "out_from_center", PATCHY_TRANSLATE_NOOP("QObject", "Out From Center")},
    {"PndR", "pond_ripples", PATCHY_TRANSLATE_NOOP("QObject", "Pond Ripples")},
};

// Photoshop's recorded ZigZag action: Amnt, NmbR, ZZTy (shipped Text Effects set).
constexpr SmartFilterField kZigZagFields[]{
    {"Amnt", "amount", PATCHY_TRANSLATE_NOOP("QObject", "Amount"), SmartFilterFieldType::Integer,
     nullptr, {}, -100, 100, 10, -100, 100, nullptr},
    {"NmbR", "ridges", PATCHY_TRANSLATE_NOOP("QObject", "Ridges"), SmartFilterFieldType::Integer,
     nullptr, {}, 0, 20, 5, 1, 20, nullptr},
    {"ZZTy", "style", PATCHY_TRANSLATE_NOOP("QObject", "Style"), SmartFilterFieldType::Enum, "ZZTy",
     kZigZagStyles, 0, 0, 2},
};

constexpr SmartFilterFieldOption kWaveTypes[]{
    {"WvSn", "sine", PATCHY_TRANSLATE_NOOP("QObject", "Sine")},
    {"WvTr", "triangle", PATCHY_TRANSLATE_NOOP("QObject", "Triangle")},
    {"WvSq", "square", PATCHY_TRANSLATE_NOOP("QObject", "Square")},
};

constexpr SmartFilterFieldOption kUndefinedAreas[]{
    {"WrpA", "wrap_around", PATCHY_TRANSLATE_NOOP("QObject", "Wrap Around")},
    {"RptE", "repeat_edge_pixels", PATCHY_TRANSLATE_NOOP("QObject", "Repeat Edge Pixels")},
};

// Photoshop's recorded Wave action (shipped Frames, Text Effects and Textures
// sets): Wvtp, NmbG, WLMn, WLMx, AmMn, AmMx, SclH, SclV, UndA, RndS.
constexpr SmartFilterField kWaveFields[]{
    {"Wvtp", "type", PATCHY_TRANSLATE_NOOP("QObject", "Type"), SmartFilterFieldType::Enum, "Wvtp",
     kWaveTypes, 0, 0, 0},
    {"NmbG", "generators", PATCHY_TRANSLATE_NOOP("QObject", "Generators"),
     SmartFilterFieldType::Integer, nullptr, {}, 1, 999, 5, 1, 100, nullptr},
    {"WLMn", "wavelength_min", PATCHY_TRANSLATE_NOOP("QObject", "Wavelength Min"),
     SmartFilterFieldType::Integer, nullptr, {}, 1, 999, 10, 1, 998, nullptr},
    {"WLMx", "wavelength_max", PATCHY_TRANSLATE_NOOP("QObject", "Wavelength Max"),
     SmartFilterFieldType::Integer, nullptr, {}, 1, 999, 120, 2, 999, nullptr},
    {"AmMn", "amplitude_min", PATCHY_TRANSLATE_NOOP("QObject", "Amplitude Min"),
     SmartFilterFieldType::Integer, nullptr, {}, 1, 999, 5, 1, 998, nullptr},
    {"AmMx", "amplitude_max", PATCHY_TRANSLATE_NOOP("QObject", "Amplitude Max"),
     SmartFilterFieldType::Integer, nullptr, {}, 1, 999, 35, 2, 999, nullptr},
    {"SclH", "scale_horizontal", PATCHY_TRANSLATE_NOOP("QObject", "Horizontal Scale"),
     SmartFilterFieldType::Integer, nullptr, {}, 0, 100, 100, 1, 100, PATCHY_TRANSLATE_NOOP("QObject", "%")},
    {"SclV", "scale_vertical", PATCHY_TRANSLATE_NOOP("QObject", "Vertical Scale"),
     SmartFilterFieldType::Integer, nullptr, {}, 0, 100, 100, 1, 100, PATCHY_TRANSLATE_NOOP("QObject", "%")},
    {"UndA", "undefined_areas", PATCHY_TRANSLATE_NOOP("QObject", "Undefined Areas"),
     SmartFilterFieldType::Enum, "UndA", kUndefinedAreas, 0, 0, 1},
    {"RndS", "seed", PATCHY_TRANSLATE_NOOP("QObject", "Seed"), SmartFilterFieldType::Integer, nullptr,
     {}, INT32_MIN, INT32_MAX, 1, 0, 1, nullptr, false, true},
};

constexpr SmartFilterFieldOption kRippleSizes[]{
    {"Sml ", "small", PATCHY_TRANSLATE_NOOP("QObject", "Small")},
    {"Mdm ", "medium", PATCHY_TRANSLATE_NOOP("QObject", "Medium")},
    {"Lrg ", "large", PATCHY_TRANSLATE_NOOP("QObject", "Large")},
};

// Photoshop's recorded Ripple action (shipped Frames and Text Effects sets): Amnt, RplS.
constexpr SmartFilterField kRippleFields[]{
    {"Amnt", "amount", PATCHY_TRANSLATE_NOOP("QObject", "Amount"), SmartFilterFieldType::Integer,
     nullptr, {}, -999, 999, 100, -999, 999, PATCHY_TRANSLATE_NOOP("QObject", "%")},
    {"RplS", "size", PATCHY_TRANSLATE_NOOP("QObject", "Size"), SmartFilterFieldType::Enum, "RplS",
     kRippleSizes, 0, 0, 1},
};

// Photoshop's recorded Shear action (shipped Text Effects and Textures sets):
// ShrP (the curve), UndA, ShrS, ShrE. Patchy has no curve editor, so the curve
// and the two integers Photoshop stores beside it stay hidden and verbatim.
constexpr SmartFilterField kShearFields[]{
    {"ShrP", "curve", PATCHY_TRANSLATE_NOOP("QObject", "Curve"), SmartFilterFieldType::PointList,
     nullptr, {}, 2, 1000, 2, 2, 3, nullptr, false, true},
    {"UndA", "undefined_areas", PATCHY_TRANSLATE_NOOP("QObject", "Undefined Areas"),
     SmartFilterFieldType::Enum, "UndA", kUndefinedAreas, 0, 0, 0},
    {"ShrS", "curve_start", PATCHY_TRANSLATE_NOOP("QObject", "Curve Start"),
     SmartFilterFieldType::Integer, nullptr, {}, INT32_MIN, INT32_MAX, 0, 0, 1, nullptr, false, true},
    {"ShrE", "curve_end", PATCHY_TRANSLATE_NOOP("QObject", "Curve End"),
     SmartFilterFieldType::Integer, nullptr, {}, INT32_MIN, INT32_MAX, 1, 0, 1, nullptr, false, true},
};

constexpr std::array kSchemas{
    // Neither stores a Fltr object; the entry name has no ellipsis.
    SmartFilterSchema{SmartFilterKind::Solarize, "Slrz", false, 0x536c727aU, "Solarize",
                      "Solarize", PATCHY_TRANSLATE_NOOP("QObject", "Solarize"),
                      "patchy.smart_filters.solarize", {}, false},
    SmartFilterSchema{SmartFilterKind::FindEdges, "FndE", false, 0x466e6445U, "Find Edges",
                      "Find Edges", PATCHY_TRANSLATE_NOOP("QObject", "Find Edges"),
                      "patchy.smart_filters.find_edges", {}, false},
    SmartFilterSchema{SmartFilterKind::ZigZag, "ZgZg", false, 0x5a675a67U, "ZigZag", "ZigZag...",
                      PATCHY_TRANSLATE_NOOP("QObject", "ZigZag"), "patchy.smart_filters.zigzag",
                      kZigZagFields},
    SmartFilterSchema{SmartFilterKind::Wave, "Wave", false, 0x57617665U, "Wave", "Wave...",
                      PATCHY_TRANSLATE_NOOP("QObject", "Wave"), "patchy.smart_filters.wave",
                      kWaveFields},
    SmartFilterSchema{SmartFilterKind::Ripple, "Rple", false, 0x52706c65U, "Ripple", "Ripple...",
                      PATCHY_TRANSLATE_NOOP("QObject", "Ripple"), "patchy.smart_filters.ripple",
                      kRippleFields},
    SmartFilterSchema{SmartFilterKind::Shear, "Shr ", false, 0x53687220U, "Shear", "Shear...",
                      PATCHY_TRANSLATE_NOOP("QObject", "Shear"), "patchy.smart_filters.shear",
                      kShearFields},
};

}  // namespace

std::span<const SmartFilterSchema> smart_filter_schemas() noexcept { return kSchemas; }

const SmartFilterSchema* find_smart_filter_schema(SmartFilterKind kind) noexcept {
  for (const auto& schema : kSchemas) {
    if (schema.kind == kind) {
      return &schema;
    }
  }
  return nullptr;
}

const SmartFilterSchema* find_smart_filter_schema_for_class(std::string_view class_id,
                                                            std::uint32_t filter_id) noexcept {
  for (const auto& schema : kSchemas) {
    if (schema.has_filter_object && class_id == schema.class_id &&
        filter_id == schema.filter_id) {
      return &schema;
    }
  }
  return nullptr;
}

const SmartFilterSchema* find_smart_filter_schema_without_object(
    std::uint32_t filter_id) noexcept {
  for (const auto& schema : kSchemas) {
    if (!schema.has_filter_object && filter_id == schema.filter_id) {
      return &schema;
    }
  }
  return nullptr;
}

SchemaSmartFilter default_schema_settings(const SmartFilterSchema& schema) {
  SchemaSmartFilter settings;
  settings.values.reserve(schema.fields.size());
  for (const auto& field : schema.fields) {
    if (field.type == SmartFilterFieldType::PointList) {
      // A straight vertical curve: no shift anywhere.
      settings.values.push_back(2);
      settings.points = {0.0, 0.0, 0.0, 128.0};
      continue;
    }
    settings.values.push_back(field.default_value);
  }
  return settings;
}

bool schema_settings_valid(const SmartFilterSchema& schema,
                           const SchemaSmartFilter& settings) noexcept {
  if (settings.values.size() != schema.fields.size()) {
    return false;
  }
  for (std::size_t index = 0; index < schema.fields.size(); ++index) {
    const auto& field = schema.fields[index];
    const auto value = settings.values[index];
    switch (field.type) {
      case SmartFilterFieldType::Integer:
        if (value < field.minimum || value > field.maximum) {
          return false;
        }
        break;
      case SmartFilterFieldType::Enum:
        if (value < 0 || static_cast<std::size_t>(value) >= field.options.size()) {
          return false;
        }
        break;
      case SmartFilterFieldType::Boolean:
        if (value != 0 && value != 1) {
          return false;
        }
        break;
      case SmartFilterFieldType::PointList:
        if (value < field.minimum || value > field.maximum ||
            settings.points.size() != static_cast<std::size_t>(value) * 2U) {
          return false;
        }
        for (const auto coordinate : settings.points) {
          if (!std::isfinite(coordinate)) {
            return false;
          }
        }
        break;
    }
  }
  return true;
}

}  // namespace patchy
