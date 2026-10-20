#include "psd/psd_smart_filter_adjustments.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace patchy::psd {

namespace {

// --- reading ---------------------------------------------------------------

// Reads the values of one Fltr object (or one of its list items) and records whether
// everything it saw is in the modeled subset. Absent keys read as Photoshop's
// defaults: its action forms store only the values a dialog changed (Photoshop's own
// bundled actions record LvlA with just Gmm, for example).
struct FieldReader {
  const DescriptorObject& object;
  bool ok{true};

  bool only(std::initializer_list<std::string_view> keys) {
    for (const auto& [key, value] : object.values) {
      static_cast<void>(value);
      if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
        ok = false;
      }
    }
    return ok;
  }

  const DescriptorValue* find(std::string_view key) const { return descriptor_value(object, key); }

  int integer(std::string_view key, int fallback, int minimum, int maximum) {
    const auto* value = find(key);
    if (value == nullptr) {
      return fallback;
    }
    if (value->type != DescriptorValue::Type::Integer || value->integer_value < minimum ||
        value->integer_value > maximum) {
      ok = false;
      return fallback;
    }
    return value->integer_value;
  }

  double number(std::string_view key, double fallback, double minimum, double maximum) {
    const auto* value = find(key);
    if (value == nullptr) {
      return fallback;
    }
    double number = 0.0;
    if (value->type == DescriptorValue::Type::Double) {
      number = value->double_value;
    } else if (value->type == DescriptorValue::Type::Integer) {
      number = value->integer_value;
    } else {
      ok = false;
      return fallback;
    }
    if (!std::isfinite(number) || number < minimum || number > maximum) {
      ok = false;
      return fallback;
    }
    return number;
  }

  bool boolean(std::string_view key, bool fallback) {
    const auto* value = find(key);
    if (value == nullptr) {
      return fallback;
    }
    if (value->type != DescriptorValue::Type::Bool) {
      ok = false;
      return fallback;
    }
    return value->bool_value;
  }

  // A list of exactly `count` integers in range.
  bool integers(std::string_view key, std::size_t count, int minimum, int maximum, int* out) {
    const auto* value = find(key);
    if (value == nullptr) {
      return false;
    }
    if (value->type != DescriptorValue::Type::List || value->list_value.size() != count) {
      ok = false;
      return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
      const auto& item = value->list_value[index];
      if (item.type != DescriptorValue::Type::Integer || item.integer_value < minimum ||
          item.integer_value > maximum) {
        ok = false;
        return false;
      }
      out[index] = item.integer_value;
    }
    return true;
  }

  // The objects of a list of `class_id` objects; null when the key is absent.
  const std::vector<DescriptorValue>* objects(std::string_view key, std::string_view class_id) {
    const auto* value = find(key);
    if (value == nullptr) {
      return nullptr;
    }
    if (value->type != DescriptorValue::Type::List) {
      ok = false;
      return nullptr;
    }
    for (const auto& item : value->list_value) {
      if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr ||
          item.object_value->class_id != class_id) {
        ok = false;
        return nullptr;
      }
    }
    return &value->list_value;
  }

  // An object value of `class_id`; null when absent.
  const DescriptorObject* object_of(std::string_view key, std::string_view class_id) {
    const auto* value = find(key);
    if (value == nullptr) {
      return nullptr;
    }
    if (value->type != DescriptorValue::Type::Object || value->object_value == nullptr ||
        value->object_value->class_id != class_id) {
      ok = false;
      return nullptr;
    }
    return value->object_value.get();
  }

  // presetKind is the dialog's preset menu state; the explicit values beside it are
  // what Photoshop renders.
  void preset_kind() {
    const auto* value = find("presetKind");
    if (value != nullptr &&
        (value->type != DescriptorValue::Type::Enum || value->enum_type != "presetKindType")) {
      ok = false;
    }
  }
};

enum class ChannelId { Composite, Red, Green, Blue };

// The 'Chnl' reference of a LvlA or CrvA record: Enmr Chnl/Chnl with Cmps, Rd, Grn or Bl.
std::optional<ChannelId> record_channel(const DescriptorObject& record) {
  const auto* value = descriptor_value(record, "Chnl");
  if (value == nullptr || value->type != DescriptorValue::Type::Reference ||
      value->reference_items.size() != 1U) {
    return std::nullopt;
  }
  const auto& item = value->reference_items.front();
  if (item.form != "Enmr" || item.class_id != "Chnl" || item.id_a != "Chnl") {
    return std::nullopt;
  }
  if (item.id_b == "Cmps") {
    return ChannelId::Composite;
  }
  if (item.id_b == "Rd  ") {
    return ChannelId::Red;
  }
  if (item.id_b == "Grn ") {
    return ChannelId::Green;
  }
  if (item.id_b == "Bl  ") {
    return ChannelId::Blue;
  }
  return std::nullopt;
}

bool read_levels(const DescriptorObject& filter, LevelsAdjustment& levels) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Adjs"});
  reader.preset_kind();
  const auto* records = reader.objects("Adjs", "LvlA");
  if (!reader.ok) {
    return false;
  }
  std::array<bool, 4> seen{};
  if (records != nullptr) {
    for (const auto& item : *records) {
      FieldReader record{*item.object_value};
      record.only({"Chnl", "Inpt", "Gmm ", "Otpt"});
      const auto channel = record_channel(*item.object_value);
      if (!record.ok || !channel.has_value() || seen[static_cast<std::size_t>(*channel)]) {
        return false;
      }
      seen[static_cast<std::size_t>(*channel)] = true;
      LevelsRecord parsed;
      int input[2]{0, 255};
      int output[2]{0, 255};
      record.integers("Inpt", 2, 0, 255, input);
      record.integers("Otpt", 2, 0, 255, output);
      const auto gamma = record.number("Gmm ", 1.0, 0.1, 9.99);
      if (!record.ok || input[0] >= input[1] || output[0] > output[1]) {
        return false;
      }
      parsed.black_input = input[0];
      parsed.white_input = input[1];
      parsed.gamma_percent = static_cast<int>(std::lround(gamma * 100.0));
      parsed.black_output = output[0];
      parsed.white_output = output[1];
      if (clamp_levels_record(parsed) != parsed) {
        return false;
      }
      switch (*channel) {
        case ChannelId::Composite:
          set_levels_master_record(levels, parsed);
          break;
        case ChannelId::Red:
          levels.red = parsed;
          break;
        case ChannelId::Green:
          levels.green = parsed;
          break;
        case ChannelId::Blue:
          levels.blue = parsed;
          break;
      }
    }
  }
  return true;
}

bool read_curve_points(const DescriptorObject& record, CurveControlPoints& points) {
  FieldReader reader{record};
  const auto* items = reader.objects("Crv ", "Pnt ");
  if (!reader.ok || items == nullptr || items->size() < 2U || items->size() > 19U) {
    return false;
  }
  points.clear();
  for (const auto& item : *items) {
    FieldReader point{*item.object_value};
    point.only({"Hrzn", "Vrtc"});
    const auto input = point.number("Hrzn", -1.0, 0.0, 255.0);
    const auto output = point.number("Vrtc", -1.0, 0.0, 255.0);
    if (!point.ok || point.find("Hrzn") == nullptr || point.find("Vrtc") == nullptr ||
        input != std::round(input) || output != std::round(output)) {
      return false;
    }
    const CurveControlPoint parsed{static_cast<int>(input), static_cast<int>(output)};
    if (!points.empty() && parsed.input <= points.back().input) {
      return false;
    }
    points.push_back(parsed);
  }
  return true;
}

bool read_curves(const DescriptorObject& filter, CurvesAdjustment& curves) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Adjs"});
  reader.preset_kind();
  const auto* records = reader.objects("Adjs", "CrvA");
  if (!reader.ok) {
    return false;
  }
  std::array<bool, 4> seen{};
  if (records != nullptr) {
    for (const auto& item : *records) {
      FieldReader record{*item.object_value};
      record.only({"Chnl", "Crv "});
      const auto channel = record_channel(*item.object_value);
      if (!record.ok || !channel.has_value() || seen[static_cast<std::size_t>(*channel)]) {
        return false;
      }
      seen[static_cast<std::size_t>(*channel)] = true;
      CurveControlPoints points;
      if (!read_curve_points(*item.object_value, points)) {
        return false;
      }
      switch (*channel) {
        case ChannelId::Composite:
          curves.rgb = std::move(points);
          break;
        case ChannelId::Red:
          curves.red = std::move(points);
          break;
        case ChannelId::Green:
          curves.green = std::move(points);
          break;
        case ChannelId::Blue:
          curves.blue = std::move(points);
          break;
      }
    }
  }
  return true;
}

// HStr: Clrz and one Hst2 triple, the master sliders or (Clrz) the colorize sliders.
// Per-hue-range bands are further Hst2 records with keys Patchy has not captured, so
// they stay unmodeled.
bool read_hue_saturation(const DescriptorObject& filter, HueSaturationAdjustment& settings) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Clrz", "Adjs"});
  reader.preset_kind();
  settings.colorize = reader.boolean("Clrz", false);
  const auto* records = reader.objects("Adjs", "Hst2");
  if (!reader.ok || (records != nullptr && records->size() > 1U) ||
      (settings.colorize && (records == nullptr || records->empty()))) {
    return false;
  }
  if (records == nullptr || records->empty()) {
    return true;
  }
  FieldReader triple{*records->front().object_value};
  triple.only({"H   ", "Strt", "Lght"});
  if (settings.colorize) {
    settings.colorize_hue = triple.integer("H   ", 0, 0, 360);
    settings.colorize_saturation = triple.integer("Strt", 25, 0, 100);
    settings.colorize_lightness = triple.integer("Lght", 0, -100, 100);
  } else {
    settings.hue_shift = triple.integer("H   ", 0, -180, 180);
    settings.saturation_delta = triple.integer("Strt", 0, -100, 100);
    settings.lightness_delta = triple.integer("Lght", 0, -100, 100);
  }
  return triple.ok;
}

bool read_brightness_contrast(const DescriptorObject& filter, BrightnessContrastAdjustment& settings) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Brgh", "Cntr", "useLegacy"});
  reader.preset_kind();
  // An action without useLegacy (Photoshop's older recorded form) renders with the legacy
  // algorithm in Photoshop 2026 (the "no legacy key" capture).
  settings.use_legacy = reader.boolean("useLegacy", true);
  const auto range = settings.use_legacy ? kBrightnessContrastLegacyRange : kModernBrightnessRange;
  settings.brightness = reader.integer("Brgh", 0, -range, range);
  settings.contrast = settings.use_legacy
                          ? reader.integer("Cntr", 0, -kBrightnessContrastLegacyRange,
                                           kBrightnessContrastLegacyRange)
                          : reader.integer("Cntr", 0, kModernContrastMin, kModernContrastMax);
  return reader.ok;
}

bool read_color_balance(const DescriptorObject& filter, ColorBalanceAdjustment& settings) {
  FieldReader reader{filter};
  reader.only({"presetKind", "ShdL", "MdtL", "HghL", "PrsL"});
  reader.preset_kind();
  int shadows[3]{0, 0, 0};
  int midtones[3]{0, 0, 0};
  int highlights[3]{0, 0, 0};
  reader.integers("ShdL", 3, -100, 100, shadows);
  reader.integers("MdtL", 3, -100, 100, midtones);
  reader.integers("HghL", 3, -100, 100, highlights);
  if (reader.find("PrsL") == nullptr) {
    return false;  // every recorded form carries it; its default is not captured
  }
  settings.preserve_luminosity = reader.boolean("PrsL", true);
  settings.shadows = {shadows[0], shadows[1], shadows[2]};
  settings.cyan_red = midtones[0];
  settings.magenta_green = midtones[1];
  settings.yellow_blue = midtones[2];
  settings.highlights = {highlights[0], highlights[1], highlights[2]};
  return reader.ok;
}

bool read_mixer_row(const DescriptorObject* row, ChannelMixerRow& parsed) {
  if (row == nullptr) {
    return true;
  }
  FieldReader reader{*row};
  reader.only({"Rd  ", "Grn ", "Bl  ", "Cnst"});
  parsed.red = reader.integer("Rd  ", 0, -200, 200);
  parsed.green = reader.integer("Grn ", 0, -200, 200);
  parsed.blue = reader.integer("Bl  ", 0, -200, 200);
  parsed.constant = reader.integer("Cnst", 0, -200, 200);
  return reader.ok;
}

bool read_channel_mixer(const DescriptorObject& filter, ChannelMixerAdjustment& settings) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Mnch", "Rd  ", "Grn ", "Bl  ", "Gry "});
  reader.preset_kind();
  settings.monochrome = reader.boolean("Mnch", false);
  const auto* red = reader.object_of("Rd  ", "ChMx");
  const auto* green = reader.object_of("Grn ", "ChMx");
  const auto* blue = reader.object_of("Bl  ", "ChMx");
  const auto* gray = reader.object_of("Gry ", "ChMx");
  if (!reader.ok || (settings.monochrome && gray == nullptr)) {
    return false;
  }
  // A row the action leaves out keeps Photoshop's identity row.
  if (red != nullptr) {
    settings.red = {};
  }
  if (green != nullptr) {
    settings.green = {};
  }
  if (blue != nullptr) {
    settings.blue = {};
  }
  if (gray != nullptr) {
    settings.gray = {};
  }
  return read_mixer_row(red, settings.red) && read_mixer_row(green, settings.green) &&
         read_mixer_row(blue, settings.blue) && read_mixer_row(gray, settings.gray);
}

bool read_exposure(const DescriptorObject& filter, ExposureAdjustment& settings) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Exps", "Ofst", "gammaCorrection"});
  reader.preset_kind();
  const auto exposure = reader.number("Exps", 0.0, -20.0, 20.0);
  const auto offset = reader.number("Ofst", 0.0, -0.5, 0.5);
  const auto gamma = reader.number("gammaCorrection", 1.0, 0.01, 9.99);
  settings.exposure_hundredths = static_cast<int>(std::lround(exposure * 100.0));
  settings.offset_ten_thousandths = static_cast<int>(std::lround(offset * 10000.0));
  settings.gamma_hundredths = static_cast<int>(std::lround(gamma * 100.0));
  return reader.ok;
}

bool read_photo_filter(const DescriptorObject& filter, PhotoFilterAdjustment& settings) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Clr ", "Dnst", "PrsL"});
  reader.preset_kind();
  const auto* color = reader.object_of("Clr ", "LbCl");
  settings.density = reader.integer("Dnst", 25, 1, 100);
  settings.preserve_luminosity = reader.boolean("PrsL", true);
  if (!reader.ok || color == nullptr) {
    return false;  // Photoshop records the filter color as Lab; other spaces are unmodeled
  }
  FieldReader lab{*color};
  lab.only({"Lmnc", "A   ", "B   "});
  const auto l = lab.number("Lmnc", -1.0, 0.0, 100.0);
  const auto a = lab.number("A   ", 0.0, -128.0, 127.0);
  const auto b = lab.number("B   ", 0.0, -128.0, 127.0);
  if (!lab.ok || lab.find("Lmnc") == nullptr || lab.find("A   ") == nullptr || lab.find("B   ") == nullptr) {
    return false;
  }
  settings.lab_l = static_cast<int>(std::lround(l * 100.0));
  settings.lab_a = static_cast<int>(std::lround(a * 100.0));
  settings.lab_b = static_cast<int>(std::lround(b * 100.0));
  return clamp_photo_filter(settings) == settings;
}

bool read_black_white(const DescriptorObject& filter, BlackWhiteAdjustment& settings) {
  FieldReader reader{filter};
  reader.only({"presetKind", "Rd  ", "Yllw", "Grn ", "Cyn ", "Bl  ", "Mgnt", "useTint", "tintColor"});
  reader.preset_kind();
  settings.reds = reader.integer("Rd  ", 40, -200, 300);
  settings.yellows = reader.integer("Yllw", 60, -200, 300);
  settings.greens = reader.integer("Grn ", 40, -200, 300);
  settings.cyans = reader.integer("Cyn ", 60, -200, 300);
  settings.blues = reader.integer("Bl  ", 20, -200, 300);
  settings.magentas = reader.integer("Mgnt", 80, -200, 300);
  settings.tint = reader.boolean("useTint", false);
  if (const auto* tint = reader.object_of("tintColor", "RGBC"); tint != nullptr) {
    FieldReader color{*tint};
    color.only({"Rd  ", "Grn ", "Bl  "});
    const auto channel = [&color](std::string_view key) {
      return static_cast<std::uint8_t>(std::lround(color.number(key, 0.0, 0.0, 255.0)));
    };
    settings.tint_color = RgbColor{channel("Rd  "), channel("Grn "), channel("Bl  ")};
    if (!color.ok) {
      return false;
    }
  } else if (settings.tint) {
    return false;
  }
  return reader.ok;
}

struct KindIds {
  AdjustmentKind kind;
  AdjustmentSmartFilterIds ids;
};

constexpr std::uint32_t char_id(const char (&text)[5]) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(text[0])) << 24U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(text[1])) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(text[2])) << 8U) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(text[3]));
}

// Entry names, Fltr names and ids as Photoshop 2026 writes them (October 2026 capture).
constexpr std::array<KindIds, 11> kKinds{{
    {AdjustmentKind::Levels, {"Lvls", false, "Levels", "Levels...", char_id("Lvls")}},
    {AdjustmentKind::Curves, {"Crvs", false, "Curves", "Curves...", char_id("Crvs")}},
    {AdjustmentKind::HueSaturation, {"HStr", false, "Hue/Saturation", "Hue/Saturation...", char_id("HStr")}},
    {AdjustmentKind::BrightnessContrast,
     {"BrgC", false, "Brightness/Contrast", "Brightness/&Contrast...", char_id("BrgC")}},
    {AdjustmentKind::ColorBalance, {"ClrB", false, "Color Balance", "Color Balance...", char_id("ClrB")}},
    {AdjustmentKind::ChannelMixer, {"ChnM", false, "Channel Mixer", "Channel Mixer...", char_id("ChnM")}},
    {AdjustmentKind::Exposure, {"Exps", false, "Exposure", "Exposure...", char_id("Exps")}},
    {AdjustmentKind::Posterize, {"Pstr", false, "Posterize", "Posterize...", char_id("Pstr")}},
    {AdjustmentKind::Threshold, {"Thrs", false, "Threshold", "Threshold...", char_id("Thrs")}},
    {AdjustmentKind::PhotoFilter, {"photoFilter", true, "Photo Filter", "Photo Filter...", 0U}},
    {AdjustmentKind::BlackWhite, {"BanW", false, "Black & White", "Black & White...", char_id("BanW")}},
}};

std::optional<AdjustmentKind> kind_for_class(std::string_view class_id) {
  for (const auto& entry : kKinds) {
    if (class_id == entry.ids.class_id) {
      return entry.kind;
    }
  }
  return std::nullopt;
}

// Reads `filter` as the action form of `kind`; nullopt when it is outside the model.
std::optional<AdjustmentSettings> read_adjustment(const DescriptorObject& filter, AdjustmentKind kind) {
  AdjustmentSettings settings;
  settings.kind = kind;
  bool ok = false;
  switch (kind) {
    case AdjustmentKind::Levels:
      ok = read_levels(filter, settings.levels);
      break;
    case AdjustmentKind::Curves:
      ok = read_curves(filter, settings.curves);
      break;
    case AdjustmentKind::HueSaturation:
      ok = read_hue_saturation(filter, settings.hue_saturation);
      break;
    case AdjustmentKind::BrightnessContrast:
      ok = read_brightness_contrast(filter, settings.brightness_contrast);
      break;
    case AdjustmentKind::ColorBalance:
      ok = read_color_balance(filter, settings.color_balance);
      break;
    case AdjustmentKind::ChannelMixer:
      ok = read_channel_mixer(filter, settings.channel_mixer);
      break;
    case AdjustmentKind::Exposure:
      ok = read_exposure(filter, settings.exposure);
      break;
    case AdjustmentKind::Posterize: {
      FieldReader reader{filter};
      reader.only({"presetKind", "Lvls"});
      reader.preset_kind();
      settings.posterize.levels = reader.integer("Lvls", 4, 2, 255);
      ok = reader.ok;
      break;
    }
    case AdjustmentKind::Threshold: {
      FieldReader reader{filter};
      reader.only({"presetKind", "Lvl "});
      reader.preset_kind();
      settings.threshold.level = reader.integer("Lvl ", 128, 1, 255);
      ok = reader.ok;
      break;
    }
    case AdjustmentKind::PhotoFilter:
      ok = read_photo_filter(filter, settings.photo_filter);
      break;
    case AdjustmentKind::BlackWhite:
      ok = read_black_white(filter, settings.black_white);
      break;
    case AdjustmentKind::Invert:
    case AdjustmentKind::GradientMap:
    case AdjustmentKind::ColorLookup:
    case AdjustmentKind::SelectiveColor:
    case AdjustmentKind::Vibrance:
      ok = false;
      break;
  }
  if (!ok) {
    return std::nullopt;
  }
  return settings;
}

// --- writing ---------------------------------------------------------------

DescriptorValue integer_value(int value) {
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

DescriptorValue integer_list(std::initializer_list<int> values) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::List;
  for (const auto value : values) {
    result.list_value.push_back(integer_value(value));
  }
  return result;
}

DescriptorValue object_value(std::string class_id) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Object;
  result.object_value = std::make_shared<DescriptorObject>();
  result.object_value->class_id = std::move(class_id);
  return result;
}

void put(DescriptorObject& object, std::string key, DescriptorValue value, bool long_form = false) {
  object.key_order.push_back({key, long_form});
  object.values.insert_or_assign(std::move(key), std::move(value));
}

DescriptorValue channel_reference(ChannelId channel) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Reference;
  DescriptorReferenceItem item;
  item.form = "Enmr";
  item.class_id = "Chnl";
  item.id_a = "Chnl";
  item.id_b = channel == ChannelId::Composite ? "Cmps"
              : channel == ChannelId::Red     ? "Rd  "
              : channel == ChannelId::Green   ? "Grn "
                                              : "Bl  ";
  result.reference_items.push_back(std::move(item));
  return result;
}

DescriptorValue levels_record(ChannelId channel, const LevelsRecord& record) {
  auto value = object_value("LvlA");
  auto& object = *value.object_value;
  put(object, "Chnl", channel_reference(channel));
  if (record.black_input != 0 || record.white_input != 255) {
    put(object, "Inpt", integer_list({record.black_input, record.white_input}));
  }
  if (record.gamma_percent != 100) {
    put(object, "Gmm ", double_value(record.gamma_percent / 100.0));
  }
  if (record.black_output != 0 || record.white_output != 255) {
    put(object, "Otpt", integer_list({record.black_output, record.white_output}));
  }
  return value;
}

DescriptorValue curves_record(ChannelId channel, const CurveControlPoints& points) {
  auto value = object_value("CrvA");
  auto& object = *value.object_value;
  put(object, "Chnl", channel_reference(channel));
  DescriptorValue list;
  list.type = DescriptorValue::Type::List;
  for (const auto& point : points) {
    auto item = object_value("Pnt ");
    put(*item.object_value, "Hrzn", double_value(point.input));
    put(*item.object_value, "Vrtc", double_value(point.output));
    list.list_value.push_back(std::move(item));
  }
  put(object, "Crv ", std::move(list));
  return value;
}

DescriptorValue mixer_row(const ChannelMixerRow& row, bool constant) {
  auto value = object_value("ChMx");
  put(*value.object_value, "Rd  ", integer_value(row.red));
  put(*value.object_value, "Grn ", integer_value(row.green));
  put(*value.object_value, "Bl  ", integer_value(row.blue));
  if (constant) {
    put(*value.object_value, "Cnst", integer_value(row.constant));
  }
  return value;
}

bool hue_saturation_bands_default(const HueSaturationAdjustment& settings) {
  return settings.bands == default_hue_saturation_bands();
}

// Appends `settings` in the form Photoshop's dialog records (October 2026 capture and
// Photoshop's bundled actions) to an emptied Fltr object.
bool append_adjustment_fields(DescriptorObject& object, const AdjustmentSettings& settings) {
  switch (settings.kind) {
    case AdjustmentKind::Levels: {
      const auto& levels = settings.levels;
      if (levels.black_ink != LevelsRecord{}) {
        return false;
      }
      DescriptorValue list;
      list.type = DescriptorValue::Type::List;
      const std::array<std::pair<ChannelId, const LevelsRecord*>, 3> channels{
          {{ChannelId::Red, &levels.red}, {ChannelId::Green, &levels.green}, {ChannelId::Blue, &levels.blue}}};
      for (const auto& [channel, record] : channels) {
        if (*record != LevelsRecord{}) {
          list.list_value.push_back(levels_record(channel, *record));
        }
      }
      list.list_value.push_back(levels_record(ChannelId::Composite, levels_master_record(levels)));
      put(object, "Adjs", std::move(list));
      return true;
    }
    case AdjustmentKind::Curves: {
      const auto& curves = settings.curves;
      const CurveControlPoints identity{{0, 0}, {255, 255}};
      if (curves.black_ink != identity) {
        return false;
      }
      DescriptorValue list;
      list.type = DescriptorValue::Type::List;
      const std::array<std::pair<ChannelId, const CurveControlPoints*>, 3> channels{
          {{ChannelId::Red, &curves.red}, {ChannelId::Green, &curves.green}, {ChannelId::Blue, &curves.blue}}};
      for (const auto& [channel, points] : channels) {
        if (*points != identity) {
          list.list_value.push_back(curves_record(channel, *points));
        }
      }
      list.list_value.push_back(curves_record(ChannelId::Composite, curves.rgb));
      put(object, "Adjs", std::move(list));
      return true;
    }
    case AdjustmentKind::HueSaturation: {
      const auto& hue = settings.hue_saturation;
      if (!hue_saturation_bands_default(hue)) {
        return false;  // band records are not captured yet
      }
      put(object, "Clrz", bool_value(hue.colorize));
      auto triple = object_value("Hst2");
      put(*triple.object_value, "H   ", integer_value(hue.colorize ? hue.colorize_hue : hue.hue_shift));
      put(*triple.object_value, "Strt",
          integer_value(hue.colorize ? hue.colorize_saturation : hue.saturation_delta));
      put(*triple.object_value, "Lght",
          integer_value(hue.colorize ? hue.colorize_lightness : hue.lightness_delta));
      DescriptorValue list;
      list.type = DescriptorValue::Type::List;
      list.list_value.push_back(std::move(triple));
      put(object, "Adjs", std::move(list));
      return true;
    }
    case AdjustmentKind::BrightnessContrast:
      put(object, "Brgh", integer_value(settings.brightness_contrast.brightness));
      put(object, "Cntr", integer_value(settings.brightness_contrast.contrast));
      put(object, "useLegacy", bool_value(settings.brightness_contrast.use_legacy), true);
      return true;
    case AdjustmentKind::ColorBalance: {
      const auto& balance = settings.color_balance;
      put(object, "ShdL",
          integer_list({balance.shadows.cyan_red, balance.shadows.magenta_green, balance.shadows.yellow_blue}));
      put(object, "MdtL", integer_list({balance.cyan_red, balance.magenta_green, balance.yellow_blue}));
      put(object, "HghL",
          integer_list({balance.highlights.cyan_red, balance.highlights.magenta_green,
                        balance.highlights.yellow_blue}));
      put(object, "PrsL", bool_value(balance.preserve_luminosity));
      return true;
    }
    case AdjustmentKind::ChannelMixer: {
      const auto& mixer = settings.channel_mixer;
      if (mixer.monochrome) {
        put(object, "Mnch", bool_value(true));
        put(object, "Gry ", mixer_row(mixer.gray, true));
      } else {
        put(object, "Rd  ", mixer_row(mixer.red, true));
        put(object, "Grn ", mixer_row(mixer.green, true));
        put(object, "Bl  ", mixer_row(mixer.blue, true));
      }
      return true;
    }
    case AdjustmentKind::Exposure:
      put(object, "Exps", double_value(settings.exposure.exposure_hundredths / 100.0));
      put(object, "Ofst", double_value(settings.exposure.offset_ten_thousandths / 10000.0));
      put(object, "gammaCorrection", double_value(settings.exposure.gamma_hundredths / 100.0), true);
      return true;
    case AdjustmentKind::Posterize:
      put(object, "Lvls", integer_value(settings.posterize.levels));
      return true;
    case AdjustmentKind::Threshold:
      put(object, "Lvl ", integer_value(settings.threshold.level));
      return true;
    case AdjustmentKind::PhotoFilter: {
      const auto& photo = settings.photo_filter;
      auto lab = object_value("LbCl");
      put(*lab.object_value, "Lmnc", double_value(photo.lab_l / 100.0));
      put(*lab.object_value, "A   ", double_value(photo.lab_a / 100.0));
      put(*lab.object_value, "B   ", double_value(photo.lab_b / 100.0));
      put(object, "Clr ", std::move(lab));
      put(object, "Dnst", integer_value(photo.density));
      put(object, "PrsL", bool_value(photo.preserve_luminosity));
      return true;
    }
    case AdjustmentKind::BlackWhite: {
      const auto& gray = settings.black_white;
      put(object, "Rd  ", integer_value(gray.reds));
      put(object, "Yllw", integer_value(gray.yellows));
      put(object, "Grn ", integer_value(gray.greens));
      put(object, "Cyn ", integer_value(gray.cyans));
      put(object, "Bl  ", integer_value(gray.blues));
      put(object, "Mgnt", integer_value(gray.magentas));
      put(object, "useTint", bool_value(gray.tint), true);
      auto tint = object_value("RGBC");
      put(*tint.object_value, "Rd  ", double_value(gray.tint_color.red));
      put(*tint.object_value, "Grn ", double_value(gray.tint_color.green));
      put(*tint.object_value, "Bl  ", double_value(gray.tint_color.blue));
      put(object, "tintColor", std::move(tint), true);
      return true;
    }
    case AdjustmentKind::Invert:
    case AdjustmentKind::GradientMap:
    case AdjustmentKind::ColorLookup:
    case AdjustmentKind::SelectiveColor:
    case AdjustmentKind::Vibrance:
      return false;
  }
  return false;
}

}  // namespace

std::optional<AdjustmentSmartFilterIds> adjustment_smart_filter_ids(AdjustmentKind kind) noexcept {
  for (const auto& entry : kKinds) {
    if (entry.kind == kind) {
      return entry.ids;
    }
  }
  return std::nullopt;
}

bool parse_adjustment_smart_filter(const DescriptorObject* filter, SmartFilterEntry& entry,
                                   bool& entry_supported) {
  if (filter == nullptr) {
    return false;
  }
  const auto kind = kind_for_class(filter->class_id);
  if (!kind.has_value()) {
    return false;
  }
  const auto ids = adjustment_smart_filter_ids(*kind);
  // A charID class must carry its own filterID; a stringID class (Photo Filter) gets a
  // run-time number, which Patchy keeps as stored.
  if (ids->filter_id != 0U && entry.native_filter_id != ids->filter_id) {
    entry_supported = false;
    return true;
  }
  auto settings = read_adjustment(*filter, *kind);
  if (!settings.has_value()) {
    entry_supported = false;
    return true;
  }
  entry.kind = SmartFilterKind::Adjustment;
  entry.parameters = AdjustmentSmartFilter{std::move(*settings)};
  return true;
}

bool write_adjustment_smart_filter(DescriptorObject& filter, const AdjustmentSettings& settings) {
  const auto kind = kind_for_class(filter.class_id);
  if (!kind.has_value() || *kind != settings.kind) {
    return false;
  }
  if (const auto stored = read_adjustment(filter, *kind);
      stored.has_value() && AdjustmentSmartFilter{*stored} == AdjustmentSmartFilter{settings}) {
    return true;
  }
  // Keep the dialog's preset state; every other key is rewritten from the model.
  std::optional<DescriptorValue> preset;
  bool preset_long_form = true;
  if (const auto found = filter.values.find("presetKind"); found != filter.values.end()) {
    preset = found->second;
    for (const auto& key : filter.key_order) {
      if (key.key == "presetKind") {
        preset_long_form = key.long_form;
      }
    }
  }
  DescriptorObject rewritten;
  rewritten.name = filter.name;
  rewritten.class_id = filter.class_id;
  rewritten.class_id_long_form = filter.class_id_long_form;
  if (preset.has_value()) {
    put(rewritten, "presetKind", std::move(*preset), preset_long_form);
  }
  if (!append_adjustment_fields(rewritten, settings)) {
    return false;
  }
  filter = std::move(rewritten);
  return true;
}

}  // namespace patchy::psd
