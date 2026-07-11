#include "core/adjustment_layer.hpp"

#include "core/blend_math.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>

namespace patchy {

namespace {

struct HslColor {
  double hue{0.0};
  double saturation{0.0};
  double lightness{0.0};
};

std::optional<int> parse_int(std::string_view value) {
  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

int metadata_int_or(const Layer& layer, const char* key, int fallback) {
  const auto found = layer.metadata().find(key);
  if (found == layer.metadata().end()) {
    return fallback;
  }
  return parse_int(found->second).value_or(fallback);
}

std::string_view metadata_string_or(const Layer& layer, const char* key, std::string_view fallback) {
  const auto found = layer.metadata().find(key);
  return found == layer.metadata().end() ? fallback : std::string_view(found->second);
}

void set_metadata_int(Layer& layer, const char* key, int value) {
  layer.metadata()[key] = std::to_string(value);
}

void set_metadata_string(Layer& layer, const char* key, std::string value) {
  layer.metadata()[key] = std::move(value);
}

constexpr std::size_t kMinimumCurveControlPoints = 2U;
constexpr std::size_t kMaximumCurveControlPoints = 19U;

std::optional<CurveControlPoints> parse_curve_control_points(std::string_view encoded) {
  CurveControlPoints points;
  while (!encoded.empty()) {
    const auto separator = encoded.find(';');
    const auto token = encoded.substr(0, separator);
    const auto coordinate_separator = token.find(':');
    if (token.empty() || coordinate_separator == std::string_view::npos ||
        token.find(':', coordinate_separator + 1U) != std::string_view::npos) {
      return std::nullopt;
    }
    const auto input = parse_int(token.substr(0, coordinate_separator));
    const auto output = parse_int(token.substr(coordinate_separator + 1U));
    if (!input.has_value() || !output.has_value() || *input < 0 || *input > 255 || *output < 0 ||
        *output > 255) {
      return std::nullopt;
    }
    points.push_back(CurveControlPoint{*input, *output});
    if (points.size() > kMaximumCurveControlPoints) {
      return std::nullopt;
    }
    if (separator == std::string_view::npos) {
      encoded = {};
    } else {
      encoded.remove_prefix(separator + 1U);
      if (encoded.empty()) {
        return std::nullopt;
      }
    }
  }
  if (points.size() < kMinimumCurveControlPoints) {
    return std::nullopt;
  }
  for (std::size_t index = 1; index < points.size(); ++index) {
    if (points[index - 1U].input >= points[index].input) {
      return std::nullopt;
    }
  }
  return points;
}

std::string serialize_curve_control_points(const CurveControlPoints& source) {
  const auto points = normalized_curve_control_points(source);
  std::string encoded;
  for (std::size_t index = 0; index < points.size(); ++index) {
    if (index != 0U) {
      encoded.push_back(';');
    }
    encoded += std::to_string(points[index].input);
    encoded.push_back(':');
    encoded += std::to_string(points[index].output);
  }
  return encoded;
}

std::optional<CurvesAdjustment> metadata_curves_adjustment(const Layer& layer) {
  const auto rgb = parse_curve_control_points(metadata_string_or(layer, kLayerMetadataAdjustmentCurvesRgbPoints, {}));
  const auto red = parse_curve_control_points(metadata_string_or(layer, kLayerMetadataAdjustmentCurvesRedPoints, {}));
  const auto green =
      parse_curve_control_points(metadata_string_or(layer, kLayerMetadataAdjustmentCurvesGreenPoints, {}));
  const auto blue = parse_curve_control_points(metadata_string_or(layer, kLayerMetadataAdjustmentCurvesBluePoints, {}));
  if (!rgb.has_value() || !red.has_value() || !green.has_value() || !blue.has_value()) {
    return std::nullopt;
  }
  return CurvesAdjustment{*rgb, *red, *green, *blue};
}

LevelsRecord clamp_levels_record(LevelsRecord record);

LevelsRecord metadata_levels_record_or(const Layer& layer, const char* black_input_key, const char* white_input_key,
                                       const char* gamma_percent_key, const char* black_output_key,
                                       const char* white_output_key) {
  return clamp_levels_record(LevelsRecord{metadata_int_or(layer, black_input_key, 0),
                                          metadata_int_or(layer, white_input_key, 255),
                                          metadata_int_or(layer, gamma_percent_key, 100),
                                          metadata_int_or(layer, black_output_key, 0),
                                          metadata_int_or(layer, white_output_key, 255)});
}

void set_metadata_levels_record(Layer& layer, LevelsRecord record, const char* black_input_key,
                                const char* white_input_key, const char* gamma_percent_key,
                                const char* black_output_key, const char* white_output_key) {
  record = clamp_levels_record(record);
  set_metadata_int(layer, black_input_key, record.black_input);
  set_metadata_int(layer, white_input_key, record.white_input);
  set_metadata_int(layer, gamma_percent_key, record.gamma_percent);
  set_metadata_int(layer, black_output_key, record.black_output);
  set_metadata_int(layer, white_output_key, record.white_output);
}

std::string levels_channel_key(LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return "red";
    case LevelsChannel::Green:
      return "green";
    case LevelsChannel::Blue:
      return "blue";
    case LevelsChannel::Rgb:
      return "rgb";
  }
  return "rgb";
}

LevelsChannel levels_channel_from_key(std::string_view key) {
  if (key == "red") {
    return LevelsChannel::Red;
  }
  if (key == "green") {
    return LevelsChannel::Green;
  }
  if (key == "blue") {
    return LevelsChannel::Blue;
  }
  return LevelsChannel::Rgb;
}

LevelsRecord clamp_levels_record(LevelsRecord record) {
  record.black_input = std::clamp(record.black_input, 0, 254);
  record.white_input = std::clamp(record.white_input, record.black_input + 1, 255);
  record.gamma_percent = std::clamp(record.gamma_percent, 10, 999);
  record.black_output = std::clamp(record.black_output, 0, 255);
  record.white_output = std::clamp(record.white_output, record.black_output, 255);
  return record;
}

LevelsRecord levels_master_record(LevelsAdjustment settings) {
  return clamp_levels_record(LevelsRecord{settings.black_input, settings.white_input, settings.gamma_percent,
                                          settings.black_output, settings.white_output});
}

bool levels_record_has_effect(LevelsRecord record) {
  record = clamp_levels_record(record);
  return record.black_input != 0 || record.white_input != 255 || record.gamma_percent != 100 ||
         record.black_output != 0 || record.white_output != 255;
}

HslColor rgb_to_hsl(RgbColor color) {
  const auto red = static_cast<double>(color.red) / 255.0;
  const auto green = static_cast<double>(color.green) / 255.0;
  const auto blue = static_cast<double>(color.blue) / 255.0;
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto delta = maximum - minimum;

  HslColor hsl;
  hsl.lightness = (maximum + minimum) * 0.5;
  if (delta <= 0.0) {
    return hsl;
  }

  hsl.saturation = hsl.lightness > 0.5 ? delta / (2.0 - maximum - minimum) : delta / (maximum + minimum);
  if (maximum == red) {
    hsl.hue = (green - blue) / delta + (green < blue ? 6.0 : 0.0);
  } else if (maximum == green) {
    hsl.hue = (blue - red) / delta + 2.0;
  } else {
    hsl.hue = (red - green) / delta + 4.0;
  }
  hsl.hue /= 6.0;
  return hsl;
}

double hue_to_rgb(double p, double q, double t) {
  if (t < 0.0) {
    t += 1.0;
  }
  if (t > 1.0) {
    t -= 1.0;
  }
  if (t < 1.0 / 6.0) {
    return p + (q - p) * 6.0 * t;
  }
  if (t < 0.5) {
    return q;
  }
  if (t < 2.0 / 3.0) {
    return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  }
  return p;
}

RgbColor hsl_to_rgb(HslColor hsl) {
  hsl.hue = hsl.hue - std::floor(hsl.hue);
  hsl.saturation = std::clamp(hsl.saturation, 0.0, 1.0);
  hsl.lightness = std::clamp(hsl.lightness, 0.0, 1.0);
  if (hsl.saturation <= 0.0) {
    const auto gray = clamp_byte(static_cast<float>(hsl.lightness * 255.0));
    return RgbColor{gray, gray, gray};
  }

  const auto q = hsl.lightness < 0.5 ? hsl.lightness * (1.0 + hsl.saturation)
                                     : hsl.lightness + hsl.saturation - hsl.lightness * hsl.saturation;
  const auto p = 2.0 * hsl.lightness - q;
  return RgbColor{clamp_byte(static_cast<float>(hue_to_rgb(p, q, hsl.hue + 1.0 / 3.0) * 255.0)),
                  clamp_byte(static_cast<float>(hue_to_rgb(p, q, hsl.hue) * 255.0)),
                  clamp_byte(static_cast<float>(hue_to_rgb(p, q, hsl.hue - 1.0 / 3.0) * 255.0))};
}

std::uint8_t levels_channel(std::uint8_t value, LevelsRecord record) {
  record = clamp_levels_record(record);
  const auto input_range = static_cast<double>(record.white_input - record.black_input);
  const auto gamma = static_cast<double>(record.gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;
  const auto normalized =
      std::clamp((static_cast<double>(value) - static_cast<double>(record.black_input)) / input_range, 0.0, 1.0);
  const auto leveled = std::pow(normalized, inverse_gamma);
  const auto output =
      static_cast<double>(record.black_output) + leveled * static_cast<double>(record.white_output - record.black_output);
  return clamp_byte(static_cast<float>(output));
}

RgbColor apply_levels(RgbColor color, LevelsAdjustment settings) {
  const auto master = levels_master_record(settings);
  RgbColor adjusted{levels_channel(color.red, master), levels_channel(color.green, master),
                    levels_channel(color.blue, master)};
  adjusted.red = levels_channel(adjusted.red, settings.red);
  adjusted.green = levels_channel(adjusted.green, settings.green);
  adjusted.blue = levels_channel(adjusted.blue, settings.blue);
  return adjusted;
}

RgbColor apply_curves(RgbColor color, const CurvesAdjustment& settings) {
  // Some compositor/export targets expose only the single-color adjustment
  // hook. Keep the last exact settings per render thread so those paths do not
  // normalize, sort, and allocate four 256-entry LUTs for every pixel.
  struct CachedCurvesLut {
    CurvesAdjustment settings{};
    AdjustmentLut lut{};
    bool valid{false};
  };
  thread_local CachedCurvesLut cache;
  if (!cache.valid || cache.settings != settings) {
    cache.settings = settings;
    cache.lut = build_curves_lut(settings);
    cache.valid = true;
  }
  return RgbColor{cache.lut.red[color.red], cache.lut.green[color.green], cache.lut.blue[color.blue]};
}

// Photoshop 2026 colorize, calibrated pixel-for-pixel against COM-rendered
// probe files (docs/ps-compat.md "Hue/Saturation colorize"): lightness is the
// integer (max+min)/2, the lightness slider blends toward white/black and
// rounds, saturation applies through Photoshop's slightly-nonlinear percent
// table with asymmetric rounding (round toward the max channel, truncate
// toward the min), and the hue's sector interpolant comes from a per-degree
// table measured off Photoshop's wheel (it is not the ideal hexagon ramp).
// 99.6% of probe pixels match exactly; the rest are within 2/255.

// Per-degree hue interpolant (x/255) within the 60-degree sector
// (sector = hue / 60): mid = p + f * (q - p).
constexpr std::array<std::uint8_t, 360> kColorizeHueInterp = {
      0,   0,   7,  14,  14,  21,  28,  28,  34,  40,  47,  47,  53,  59,  59,
     65,  71,  76,  76,  82,  88,  88,  93,  99, 104, 104, 110, 115, 115, 121,
    126, 132, 132, 137, 142, 142, 148, 153, 159, 159, 165, 170, 170, 176, 182,
    187, 187, 193, 199, 199, 205, 211, 211, 218, 224, 231, 231, 237, 244, 244,
    255, 255, 244, 244, 237, 231, 231, 224, 218, 211, 211, 205, 199, 199, 193,
    187, 182, 182, 176, 170, 170, 165, 159, 153, 153, 148, 142, 142, 137, 132,
    126, 126, 121, 115, 115, 110, 104, 104,  99,  93,  88,  88,  82,  76,  76,
     71,  65,  59,  59,  53,  47,  47,  40,  34,  28,  28,  21,  14,  14,   7,
      0,   0,   0,   7,  14,  14,  21,  28,  34,  34,  40,  47,  47,  53,  59,
     65,  65,  71,  76,  76,  82,  88,  88,  93,  99, 104, 104, 110, 115, 115,
    121, 126, 132, 132, 137, 142, 142, 148, 153, 159, 159, 165, 170, 170, 176,
    182, 187, 187, 193, 199, 199, 205, 211, 218, 218, 224, 231, 231, 237, 244,
    255, 255, 244, 237, 237, 231, 224, 224, 218, 211, 205, 205, 199, 193, 193,
    187, 182, 176, 176, 170, 165, 165, 159, 153, 148, 148, 142, 137, 137, 132,
    126, 121, 121, 115, 110, 110, 104,  99,  93,  93,  88,  82,  82,  76,  71,
     65,  65,  59,  53,  53,  47,  40,  40,  34,  28,  21,  21,  14,   7,   7,
      0,   0,   7,   7,  14,  21,  21,  28,  34,  40,  40,  47,  53,  53,  59,
     65,  71,  71,  76,  82,  82,  88,  93,  99,  99, 104, 110, 110, 115, 121,
    126, 126, 132, 137, 137, 142, 148, 148, 153, 159, 165, 165, 170, 176, 176,
    182, 187, 193, 193, 199, 205, 205, 211, 218, 224, 224, 231, 237, 237, 244,
    255, 255, 255, 244, 237, 237, 231, 224, 218, 218, 211, 205, 205, 199, 193,
    187, 187, 182, 176, 176, 170, 165, 165, 159, 153, 148, 148, 142, 137, 137,
    132, 126, 121, 121, 115, 110, 110, 104,  99,  93,  93,  88,  82,  82,  76,
     71,  65,  65,  59,  53,  53,  47,  40,  34,  34,  28,  21,  21,  14,   7,
};

// Photoshop's effective saturation ratio per percent; its internal percent
// conversion sits slightly below s/100 (interval midpoints, exact for every
// lightness probed).
constexpr std::array<double, 101> kColorizeSaturationScale = {
    0.000000000, 0.007905262, 0.019710941, 0.027668416, 0.039421881,
    0.047270696, 0.059073014, 0.066946710, 0.078843763, 0.086640420,
    0.098455023, 0.110265169, 0.118146027, 0.129960630, 0.137863155,
    0.149803150, 0.157507281, 0.169323089, 0.177190272, 0.189000384,
    0.200803537, 0.208678535, 0.220530338, 0.228370759, 0.240176779,
    0.249015748, 0.259921260, 0.267786839, 0.279548726, 0.287450787,
    0.299606299, 0.311067367, 0.318931578, 0.330738946, 0.338646177,
    0.350410526, 0.358300525, 0.370104305, 0.378000768, 0.389797144,
    0.401607074, 0.409465789, 0.421278069, 0.429150262, 0.441060676,
    0.448841267, 0.460652039, 0.468626969, 0.480353559, 0.488212135,
    0.501968504, 0.511857893, 0.519710941, 0.531513797, 0.539421881,
    0.551231577, 0.559073014, 0.571147357, 0.578843763, 0.590730136,
    0.602385922, 0.610265169, 0.622070134, 0.629960630, 0.641761664,
    0.649803150, 0.661437828, 0.669323089, 0.681130891, 0.689000384,
    0.700803537, 0.712621052, 0.720530338, 0.732303348, 0.740176779,
    0.751984252, 0.759921260, 0.771696337, 0.779548726, 0.791502625,
    0.803170548, 0.811067367, 0.822875656, 0.830738946, 0.842556139,
    0.850410526, 0.862224811, 0.870104305, 0.881917104, 0.889797144,
    0.901607074, 0.913423683, 0.921278069, 0.933202100, 0.941060676,
    0.952793047, 0.960652039, 0.972459005, 0.980353559, 0.992156742,
    1.003952500,
};

RgbColor apply_colorize(RgbColor color, const HueSaturationAdjustment& settings) {
  const int hue = ((settings.colorize_hue % 360) + 360) % 360;
  const auto saturation = std::clamp(settings.colorize_saturation, 0, 100);
  const auto lightness = std::clamp(settings.colorize_lightness, -100, 100);

  const int maximum = std::max({color.red, color.green, color.blue});
  const int minimum = std::min({color.red, color.green, color.blue});
  int light = (maximum + minimum) >> 1;
  if (lightness > 0) {
    light = static_cast<int>(light + (255.0 - light) * lightness / 100.0 + 0.5);
  } else if (lightness < 0) {
    light = static_cast<int>(light * (100.0 + lightness) / 100.0 + 0.5);
  }
  const int band = std::min(light, 255 - light);
  const auto delta = band * kColorizeSaturationScale[static_cast<std::size_t>(saturation)];
  const auto q = std::min(255, light + static_cast<int>(delta + 0.5));
  const auto p = std::max(0, light - static_cast<int>(delta));
  const auto interp = static_cast<double>(kColorizeHueInterp[static_cast<std::size_t>(hue)]);
  const auto mid = p + static_cast<int>((q - p) * interp / 255.0 + 0.5);

  const auto q8 = static_cast<std::uint8_t>(q);
  const auto p8 = static_cast<std::uint8_t>(p);
  const auto m8 = static_cast<std::uint8_t>(mid);
  switch (hue / 60) {
    case 0:
      return RgbColor{q8, m8, p8};  // red -> yellow
    case 1:
      return RgbColor{m8, q8, p8};  // yellow -> green
    case 2:
      return RgbColor{p8, q8, m8};  // green -> cyan
    case 3:
      return RgbColor{p8, m8, q8};  // cyan -> blue
    case 4:
      return RgbColor{m8, p8, q8};  // blue -> magenta
    default:
      return RgbColor{q8, p8, m8};  // magenta -> red
  }
}

RgbColor apply_hue_saturation(RgbColor color, HueSaturationAdjustment settings) {
  if (settings.colorize) {
    return apply_colorize(color, settings);
  }
  settings.hue_shift = std::clamp(settings.hue_shift, -180, 180);
  settings.saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  settings.lightness_delta = std::clamp(settings.lightness_delta, -100, 100);

  auto hsl = rgb_to_hsl(color);
  hsl.hue += static_cast<double>(settings.hue_shift) / 360.0;
  hsl.saturation += static_cast<double>(settings.saturation_delta) / 100.0;
  hsl.lightness += static_cast<double>(settings.lightness_delta) / 100.0;
  return hsl_to_rgb(hsl);
}

RgbColor apply_color_balance(RgbColor color, ColorBalanceAdjustment settings) {
  settings.cyan_red = std::clamp(settings.cyan_red, -100, 100);
  settings.magenta_green = std::clamp(settings.magenta_green, -100, 100);
  settings.yellow_blue = std::clamp(settings.yellow_blue, -100, 100);
  const auto red_delta = static_cast<int>(std::round(static_cast<double>(settings.cyan_red) * 255.0 / 100.0));
  const auto green_delta = static_cast<int>(std::round(static_cast<double>(settings.magenta_green) * 255.0 / 100.0));
  const auto blue_delta = static_cast<int>(std::round(static_cast<double>(settings.yellow_blue) * 255.0 / 100.0));
  return RgbColor{clamp_byte(static_cast<float>(static_cast<int>(color.red) + red_delta)),
                  clamp_byte(static_cast<float>(static_cast<int>(color.green) + green_delta)),
                  clamp_byte(static_cast<float>(static_cast<int>(color.blue) + blue_delta))};
}

}  // namespace

CurveControlPoints normalized_curve_control_points(CurveControlPoints points) {
  for (auto& point : points) {
    point.input = std::clamp(point.input, 0, 255);
    point.output = std::clamp(point.output, 0, 255);
  }
  std::stable_sort(points.begin(), points.end(),
                   [](const CurveControlPoint& left, const CurveControlPoint& right) {
                     return left.input < right.input;
                   });

  CurveControlPoints unique;
  unique.reserve(points.size() + 2U);
  for (const auto point : points) {
    if (!unique.empty() && unique.back().input == point.input) {
      unique.back() = point;
    } else {
      unique.push_back(point);
    }
  }
  if (unique.empty()) {
    return {{0, 0}, {255, 255}};
  }
  if (unique.size() == 1U) {
    if (unique.front().input < 255) {
      unique.push_back(CurveControlPoint{255, 255});
    } else {
      unique.insert(unique.begin(), CurveControlPoint{0, 0});
    }
  }

  if (unique.size() <= kMaximumCurveControlPoints) {
    return unique;
  }

  // Keep the endpoints and an evenly distributed deterministic subset of the
  // interior. Normal editor and file-format paths enforce the limit before this
  // point; this is a defensive bound for callers constructing the public model.
  CurveControlPoints bounded;
  bounded.reserve(kMaximumCurveControlPoints);
  bounded.push_back(unique.front());
  constexpr std::size_t kInteriorSlots = kMaximumCurveControlPoints - 2U;
  const auto last_index = unique.size() - 1U;
  for (std::size_t slot = 1U; slot <= kInteriorSlots; ++slot) {
    const auto index = (slot * last_index + (kMaximumCurveControlPoints - 1U) / 2U) /
                       (kMaximumCurveControlPoints - 1U);
    bounded.push_back(unique[index]);
  }
  bounded.push_back(unique.back());
  return bounded;
}

const CurveControlPoints& curve_points_for_channel(const CurvesAdjustment& curves, CurvesChannel channel) noexcept {
  switch (channel) {
    case CurvesChannel::Red:
      return curves.red;
    case CurvesChannel::Green:
      return curves.green;
    case CurvesChannel::Blue:
      return curves.blue;
    case CurvesChannel::Rgb:
      return curves.rgb;
  }
  return curves.rgb;
}

void set_curve_points_for_channel(CurvesAdjustment& curves, CurvesChannel channel, CurveControlPoints points) {
  auto normalized = normalized_curve_control_points(std::move(points));
  switch (channel) {
    case CurvesChannel::Red:
      curves.red = std::move(normalized);
      return;
    case CurvesChannel::Green:
      curves.green = std::move(normalized);
      return;
    case CurvesChannel::Blue:
      curves.blue = std::move(normalized);
      return;
    case CurvesChannel::Rgb:
      curves.rgb = std::move(normalized);
      return;
  }
}

CurvesAdjustment curves_adjustment_from_legacy_outputs(int shadow_output, int midtone_output,
                                                       int highlight_output) {
  CurvesAdjustment curves;
  curves.rgb = normalized_curve_control_points({{0, std::clamp(shadow_output, 0, 255)},
                                                 {128, std::clamp(midtone_output, 0, 255)},
                                                 {255, std::clamp(highlight_output, 0, 255)}});
  return curves;
}

std::array<std::uint8_t, 256> build_curve_lut(const CurveControlPoints& source) {
  const auto points = normalized_curve_control_points(source);
  const auto count = points.size();
  std::vector<double> second_derivatives(count, 0.0);
  std::vector<double> workspace(count, 0.0);

  // Photoshop 2026 calibration over full 256-value ramps: Curves uses a natural
  // cubic through the control points, zero second derivative at both endpoints,
  // clamps outside movable endpoints, and rounds the result to the nearest byte.
  // All render paths intentionally funnel through this one calibrated builder.
  for (std::size_t index = 1U; index + 1U < count; ++index) {
    const auto previous_span = static_cast<double>(points[index].input - points[index - 1U].input);
    const auto next_span = static_cast<double>(points[index + 1U].input - points[index].input);
    const auto combined_span = previous_span + next_span;
    const auto sigma = previous_span / combined_span;
    const auto pivot = sigma * second_derivatives[index - 1U] + 2.0;
    second_derivatives[index] = (sigma - 1.0) / pivot;
    const auto previous_slope =
        static_cast<double>(points[index].output - points[index - 1U].output) / previous_span;
    const auto next_slope = static_cast<double>(points[index + 1U].output - points[index].output) / next_span;
    workspace[index] =
        (6.0 * (next_slope - previous_slope) / combined_span - sigma * workspace[index - 1U]) / pivot;
  }
  for (std::size_t upper = count - 1U; upper > 0U; --upper) {
    const auto index = upper - 1U;
    second_derivatives[index] = second_derivatives[index] * second_derivatives[upper] + workspace[index];
  }

  std::array<std::uint8_t, 256> lut{};
  std::size_t upper = 1U;
  for (int input = 0; input < 256; ++input) {
    if (input <= points.front().input) {
      lut[static_cast<std::size_t>(input)] = static_cast<std::uint8_t>(points.front().output);
      continue;
    }
    if (input >= points.back().input) {
      lut[static_cast<std::size_t>(input)] = static_cast<std::uint8_t>(points.back().output);
      continue;
    }
    while (upper + 1U < count && input > points[upper].input) {
      ++upper;
    }
    if (input == points[upper - 1U].input) {
      lut[static_cast<std::size_t>(input)] = static_cast<std::uint8_t>(points[upper - 1U].output);
      continue;
    }
    if (input == points[upper].input) {
      lut[static_cast<std::size_t>(input)] = static_cast<std::uint8_t>(points[upper].output);
      continue;
    }
    const auto span = static_cast<double>(points[upper].input - points[upper - 1U].input);
    const auto left_weight = (static_cast<double>(points[upper].input) - input) / span;
    const auto right_weight = (input - static_cast<double>(points[upper - 1U].input)) / span;
    const auto output = left_weight * points[upper - 1U].output + right_weight * points[upper].output +
                        ((left_weight * left_weight * left_weight - left_weight) *
                             second_derivatives[upper - 1U] +
                         (right_weight * right_weight * right_weight - right_weight) *
                             second_derivatives[upper]) *
                            span * span / 6.0;
    lut[static_cast<std::size_t>(input)] =
        static_cast<std::uint8_t>(std::clamp(std::lround(output), 0L, 255L));
  }
  return lut;
}

AdjustmentLut build_curves_lut(const CurvesAdjustment& curves) {
  const auto composite = build_curve_lut(curves.rgb);
  const auto red = build_curve_lut(curves.red);
  const auto green = build_curve_lut(curves.green);
  const auto blue = build_curve_lut(curves.blue);
  AdjustmentLut lut;
  for (std::size_t input = 0; input < 256U; ++input) {
    // Photoshop applies the component channel first, then Composite RGB.
    lut.red[input] = composite[red[input]];
    lut.green[input] = composite[green[input]];
    lut.blue[input] = composite[blue[input]];
  }
  return lut;
}

bool layer_is_adjustment(const Layer& layer) {
  return layer.kind() == LayerKind::Adjustment && adjustment_settings_from_layer(layer).has_value();
}

std::string adjustment_kind_key(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
      return "levels";
    case AdjustmentKind::Curves:
      return "curves";
    case AdjustmentKind::HueSaturation:
      return "hue_saturation";
    case AdjustmentKind::ColorBalance:
      return "color_balance";
  }
  return "levels";
}

std::string adjustment_display_name(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
      return "Levels";
    case AdjustmentKind::Curves:
      return "Curves";
    case AdjustmentKind::HueSaturation:
      return "Hue/Saturation";
    case AdjustmentKind::ColorBalance:
      return "Color Balance";
  }
  return "Adjustment";
}

std::optional<AdjustmentKind> adjustment_kind_from_key(std::string_view key) {
  if (key == "levels") {
    return AdjustmentKind::Levels;
  }
  if (key == "curves") {
    return AdjustmentKind::Curves;
  }
  if (key == "hue_saturation") {
    return AdjustmentKind::HueSaturation;
  }
  if (key == "color_balance") {
    return AdjustmentKind::ColorBalance;
  }
  return std::nullopt;
}

std::optional<AdjustmentSettings> adjustment_settings_from_layer(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataAdjustmentType);
  if (found == layer.metadata().end()) {
    return std::nullopt;
  }
  const auto kind = adjustment_kind_from_key(found->second);
  if (!kind.has_value()) {
    return std::nullopt;
  }

  AdjustmentSettings settings;
  settings.kind = *kind;
  settings.levels.black_input = metadata_int_or(layer, kLayerMetadataAdjustmentLevelsBlackInput, 0);
  settings.levels.white_input = metadata_int_or(layer, kLayerMetadataAdjustmentLevelsWhiteInput, 255);
  settings.levels.gamma_percent = metadata_int_or(layer, kLayerMetadataAdjustmentLevelsGammaPercent, 100);
  settings.levels.black_output = metadata_int_or(layer, kLayerMetadataAdjustmentLevelsBlackOutput, 0);
  settings.levels.white_output = metadata_int_or(layer, kLayerMetadataAdjustmentLevelsWhiteOutput, 255);
  settings.levels.channel =
      levels_channel_from_key(metadata_string_or(layer, kLayerMetadataAdjustmentLevelsChannel, "rgb"));
  settings.levels.red =
      metadata_levels_record_or(layer, kLayerMetadataAdjustmentLevelsRedBlackInput,
                                kLayerMetadataAdjustmentLevelsRedWhiteInput,
                                kLayerMetadataAdjustmentLevelsRedGammaPercent,
                                kLayerMetadataAdjustmentLevelsRedBlackOutput,
                                kLayerMetadataAdjustmentLevelsRedWhiteOutput);
  settings.levels.green =
      metadata_levels_record_or(layer, kLayerMetadataAdjustmentLevelsGreenBlackInput,
                                kLayerMetadataAdjustmentLevelsGreenWhiteInput,
                                kLayerMetadataAdjustmentLevelsGreenGammaPercent,
                                kLayerMetadataAdjustmentLevelsGreenBlackOutput,
                                kLayerMetadataAdjustmentLevelsGreenWhiteOutput);
  settings.levels.blue =
      metadata_levels_record_or(layer, kLayerMetadataAdjustmentLevelsBlueBlackInput,
                                kLayerMetadataAdjustmentLevelsBlueWhiteInput,
                                kLayerMetadataAdjustmentLevelsBlueGammaPercent,
                                kLayerMetadataAdjustmentLevelsBlueBlackOutput,
                                kLayerMetadataAdjustmentLevelsBlueWhiteOutput);
  settings.curves = curves_adjustment_from_legacy_outputs(
      metadata_int_or(layer, kLayerMetadataAdjustmentCurvesShadowOutput, 0),
      metadata_int_or(layer, kLayerMetadataAdjustmentCurvesMidtoneOutput, 128),
      metadata_int_or(layer, kLayerMetadataAdjustmentCurvesHighlightOutput, 255));
  if (const auto rich_curves = metadata_curves_adjustment(layer); rich_curves.has_value()) {
    settings.curves = *rich_curves;
  }
  settings.hue_saturation.hue_shift = metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationHueShift, 0);
  settings.hue_saturation.saturation_delta =
      metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationSaturationDelta, 0);
  settings.hue_saturation.lightness_delta =
      metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationLightnessDelta, 0);
  settings.hue_saturation.colorize = metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationColorize, 0) != 0;
  settings.hue_saturation.colorize_hue =
      metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationColorizeHue, 0);
  settings.hue_saturation.colorize_saturation =
      metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationColorizeSaturation, 25);
  settings.hue_saturation.colorize_lightness =
      metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationColorizeLightness, 0);
  settings.color_balance.cyan_red = metadata_int_or(layer, kLayerMetadataAdjustmentColorBalanceCyanRed, 0);
  settings.color_balance.magenta_green = metadata_int_or(layer, kLayerMetadataAdjustmentColorBalanceMagentaGreen, 0);
  settings.color_balance.yellow_blue = metadata_int_or(layer, kLayerMetadataAdjustmentColorBalanceYellowBlue, 0);
  return settings;
}

void configure_adjustment_layer(Layer& layer, const AdjustmentSettings& settings) {
  layer.metadata()[kLayerMetadataAdjustmentType] = adjustment_kind_key(settings.kind);
  set_metadata_int(layer, kLayerMetadataAdjustmentLevelsBlackInput, std::clamp(settings.levels.black_input, 0, 254));
  set_metadata_int(layer, kLayerMetadataAdjustmentLevelsWhiteInput,
                   std::clamp(settings.levels.white_input, std::clamp(settings.levels.black_input, 0, 254) + 1, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentLevelsGammaPercent,
                   std::clamp(settings.levels.gamma_percent, 10, 999));
  set_metadata_int(layer, kLayerMetadataAdjustmentLevelsBlackOutput,
                   std::clamp(settings.levels.black_output, 0, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentLevelsWhiteOutput,
                   std::clamp(settings.levels.white_output,
                              std::clamp(settings.levels.black_output, 0, 255), 255));
  set_metadata_string(layer, kLayerMetadataAdjustmentLevelsChannel, levels_channel_key(settings.levels.channel));
  set_metadata_levels_record(layer, settings.levels.red, kLayerMetadataAdjustmentLevelsRedBlackInput,
                             kLayerMetadataAdjustmentLevelsRedWhiteInput,
                             kLayerMetadataAdjustmentLevelsRedGammaPercent,
                             kLayerMetadataAdjustmentLevelsRedBlackOutput,
                             kLayerMetadataAdjustmentLevelsRedWhiteOutput);
  set_metadata_levels_record(layer, settings.levels.green, kLayerMetadataAdjustmentLevelsGreenBlackInput,
                             kLayerMetadataAdjustmentLevelsGreenWhiteInput,
                             kLayerMetadataAdjustmentLevelsGreenGammaPercent,
                             kLayerMetadataAdjustmentLevelsGreenBlackOutput,
                             kLayerMetadataAdjustmentLevelsGreenWhiteOutput);
  set_metadata_levels_record(layer, settings.levels.blue, kLayerMetadataAdjustmentLevelsBlueBlackInput,
                             kLayerMetadataAdjustmentLevelsBlueWhiteInput,
                             kLayerMetadataAdjustmentLevelsBlueGammaPercent,
                             kLayerMetadataAdjustmentLevelsBlueBlackOutput,
                             kLayerMetadataAdjustmentLevelsBlueWhiteOutput);
  const auto composite_curve_lut = build_curve_lut(settings.curves.rgb);
  set_metadata_int(layer, kLayerMetadataAdjustmentCurvesShadowOutput, composite_curve_lut[0]);
  set_metadata_int(layer, kLayerMetadataAdjustmentCurvesMidtoneOutput, composite_curve_lut[128]);
  set_metadata_int(layer, kLayerMetadataAdjustmentCurvesHighlightOutput, composite_curve_lut[255]);
  auto& metadata = layer.metadata();
  if (settings.kind == AdjustmentKind::Curves) {
    metadata[kLayerMetadataAdjustmentCurvesRgbPoints] = serialize_curve_control_points(settings.curves.rgb);
    metadata[kLayerMetadataAdjustmentCurvesRedPoints] = serialize_curve_control_points(settings.curves.red);
    metadata[kLayerMetadataAdjustmentCurvesGreenPoints] = serialize_curve_control_points(settings.curves.green);
    metadata[kLayerMetadataAdjustmentCurvesBluePoints] = serialize_curve_control_points(settings.curves.blue);
  } else {
    metadata.erase(kLayerMetadataAdjustmentCurvesRgbPoints);
    metadata.erase(kLayerMetadataAdjustmentCurvesRedPoints);
    metadata.erase(kLayerMetadataAdjustmentCurvesGreenPoints);
    metadata.erase(kLayerMetadataAdjustmentCurvesBluePoints);
  }
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationHueShift,
                   std::clamp(settings.hue_saturation.hue_shift, -180, 180));
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationSaturationDelta,
                   std::clamp(settings.hue_saturation.saturation_delta, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationLightnessDelta,
                   std::clamp(settings.hue_saturation.lightness_delta, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationColorize, settings.hue_saturation.colorize ? 1 : 0);
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationColorizeHue,
                   std::clamp(settings.hue_saturation.colorize_hue, 0, 360) % 360);
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationColorizeSaturation,
                   std::clamp(settings.hue_saturation.colorize_saturation, 0, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationColorizeLightness,
                   std::clamp(settings.hue_saturation.colorize_lightness, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentColorBalanceCyanRed,
                   std::clamp(settings.color_balance.cyan_red, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentColorBalanceMagentaGreen,
                   std::clamp(settings.color_balance.magenta_green, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentColorBalanceYellowBlue,
                   std::clamp(settings.color_balance.yellow_blue, -100, 100));
}

RgbColor apply_adjustment_to_color(RgbColor color, const AdjustmentSettings& settings) {
  switch (settings.kind) {
    case AdjustmentKind::Levels:
      return apply_levels(color, settings.levels);
    case AdjustmentKind::Curves:
      return apply_curves(color, settings.curves);
    case AdjustmentKind::HueSaturation:
      return apply_hue_saturation(color, settings.hue_saturation);
    case AdjustmentKind::ColorBalance:
      return apply_color_balance(color, settings.color_balance);
  }
  return color;
}

void apply_adjustment_to_pixels(PixelBuffer& pixels, const AdjustmentSettings& settings) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return;
  }

  const auto lut = build_adjustment_lut(settings);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      if (lut.has_value()) {
        px[0] = lut->red[px[0]];
        px[1] = lut->green[px[1]];
        px[2] = lut->blue[px[2]];
      } else {
        const auto adjusted = apply_adjustment_to_color(RgbColor{px[0], px[1], px[2]}, settings);
        px[0] = adjusted.red;
        px[1] = adjusted.green;
        px[2] = adjusted.blue;
      }
    }
  }
}

std::optional<AdjustmentLut> build_adjustment_lut(const AdjustmentSettings& settings) {
  if (settings.kind == AdjustmentKind::HueSaturation) {
    return std::nullopt;
  }
  if (settings.kind == AdjustmentKind::Curves) {
    return build_curves_lut(settings.curves);
  }
  AdjustmentLut lut;
  for (int value = 0; value < 256; ++value) {
    const auto probe = static_cast<std::uint8_t>(value);
    // Levels, Curves and Color Balance are per-channel maps, so a gray probe
    // reads off each channel's transfer curve exactly.
    const auto adjusted = apply_adjustment_to_color(RgbColor{probe, probe, probe}, settings);
    lut.red[static_cast<std::size_t>(value)] = adjusted.red;
    lut.green[static_cast<std::size_t>(value)] = adjusted.green;
    lut.blue[static_cast<std::size_t>(value)] = adjusted.blue;
  }
  return lut;
}

bool adjustment_has_effect(const AdjustmentSettings& settings) {
  switch (settings.kind) {
    case AdjustmentKind::Levels:
      return levels_record_has_effect(levels_master_record(settings.levels)) ||
             levels_record_has_effect(settings.levels.red) || levels_record_has_effect(settings.levels.green) ||
             levels_record_has_effect(settings.levels.blue);
    case AdjustmentKind::Curves:
      {
        const auto lut = build_curves_lut(settings.curves);
        for (std::size_t value = 0; value < 256U; ++value) {
          if (lut.red[value] != value || lut.green[value] != value || lut.blue[value] != value) {
            return true;
          }
        }
        return false;
      }
    case AdjustmentKind::HueSaturation:
      return settings.hue_saturation.colorize || settings.hue_saturation.hue_shift != 0 ||
             settings.hue_saturation.saturation_delta != 0 || settings.hue_saturation.lightness_delta != 0;
    case AdjustmentKind::ColorBalance:
      return settings.color_balance.cyan_red != 0 || settings.color_balance.magenta_green != 0 ||
             settings.color_balance.yellow_blue != 0;
  }
  return false;
}

}  // namespace patchy
