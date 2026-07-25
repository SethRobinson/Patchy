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

// "r0;r1;r2;r3;hue;saturation;lightness". A malformed or missing value leaves
// the band at Photoshop's default hextant, which renders as no effect.
std::string serialize_hue_saturation_band(const HueSaturationBand& band) {
  const std::array<int, 7> fields{band.outer_start,   band.inner_start,      band.inner_end,
                                  band.outer_end,     band.hue_shift,        band.saturation_delta,
                                  band.lightness_delta};
  std::string encoded;
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (index != 0U) {
      encoded.push_back(';');
    }
    encoded += std::to_string(fields[index]);
  }
  return encoded;
}

std::optional<HueSaturationBand> parse_hue_saturation_band(std::string_view encoded) {
  std::array<int, 7> fields{};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (encoded.empty()) {
      return std::nullopt;
    }
    const auto separator = encoded.find(';');
    const auto token = encoded.substr(0, separator);
    const auto value = parse_int(token);
    if (!value.has_value()) {
      return std::nullopt;
    }
    fields[index] = *value;
    encoded = separator == std::string_view::npos ? std::string_view{} : encoded.substr(separator + 1U);
  }
  if (!encoded.empty()) {
    return std::nullopt;
  }
  const auto degrees = [](int value) { return ((value % 360) + 360) % 360; };
  return HueSaturationBand{degrees(fields[0]),
                           degrees(fields[1]),
                           degrees(fields[2]),
                           degrees(fields[3]),
                           std::clamp(fields[4], -180, 180),
                           std::clamp(fields[5], -100, 100),
                           std::clamp(fields[6], -100, 100)};
}

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

bool levels_record_has_effect(LevelsRecord record) {
  record = clamp_levels_record(record);
  return record.black_input != 0 || record.white_input != 255 || record.gamma_percent != 100 ||
         record.black_output != 0 || record.white_output != 255;
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

// Photoshop 2026 Hue/Saturation, calibrated pixel-for-pixel against COM-rendered
// probe files (docs/ps-compat.md "Hue/Saturation"). Colorize and the master
// sliders share three stages: the lightness slider blends a value toward
// white/black and rounds, the hue lives on a 1530-step wheel, and the result is
// rebuilt from an integer lightness plus a half-chroma spread with asymmetric
// rounding. Colorize applies lightness to the pixel's HSL lightness and
// synthesizes a hue; master applies it per channel and rotates the existing hue.

// Photoshop's lightness slider. Colorize feeds it the integer (max+min)/2;
// master feeds it each channel in turn, which is what makes the slider a blend
// toward white/black rather than an offset on HSL lightness.
//
// Photoshop quantizes the percent to a byte first: k = |lightness| * 255 / 100
// truncated, then blends by k/255. That is byte-exact over all 201 percents on
// every gray and every chroma-grid probe; the plain |lightness|/100 form drifts
// by 1 on 188 of them. The two agree exactly on multiples of 20, which is why
// the colorize calibration (probed at 0 and +-40) never saw the difference.
int photoshop_lightness_value(int value, int lightness) {
  const auto step = std::abs(lightness) * 255 / 100;
  if (lightness > 0) {
    return static_cast<int>(value + (255.0 - value) * step / 255.0 + 0.5);
  }
  if (lightness < 0) {
    return static_cast<int>(value * (255.0 - step) / 255.0 + 0.5);
  }
  return value;
}

// Photoshop's hue wheel as a 1530-step ramp: six sectors of 255 interpolant
// steps, so one degree is 4.25 steps. Master mode rotates a pixel's existing
// hue, so it needs this inverse direction as well as the forward split below.
double photoshop_wheel_position(RgbColor color) {
  const int red = color.red;
  const int green = color.green;
  const int blue = color.blue;
  const int maximum = std::max({red, green, blue});
  const int minimum = std::min({red, green, blue});
  const auto span = static_cast<double>(maximum - minimum);
  if (span <= 0.0) {
    return 0.0;
  }
  const auto ramp = [span](int middle, int low) {
    return 255.0 * static_cast<double>(middle - low) / span;
  };
  if (red == maximum && blue == minimum) {
    return ramp(green, blue);           // red -> yellow
  }
  if (green == maximum && blue == minimum) {
    return 510.0 - ramp(red, blue);     // yellow -> green
  }
  if (green == maximum && red == minimum) {
    return 510.0 + ramp(blue, red);     // green -> cyan
  }
  if (blue == maximum && red == minimum) {
    return 1020.0 - ramp(green, red);   // cyan -> blue
  }
  if (blue == maximum && green == minimum) {
    return 1020.0 + ramp(red, green);   // blue -> magenta
  }
  return 1530.0 - ramp(blue, green);    // magenta -> red
}

void photoshop_wheel_split(double position, int& sector, double& interpolant) {
  position = std::fmod(position, 1530.0);
  if (position < 0.0) {
    position += 1530.0;
  }
  sector = std::min(5, static_cast<int>(position / 255.0));
  const auto offset = position - static_cast<double>(sector) * 255.0;
  interpolant = sector % 2 == 0 ? offset : 255.0 - offset;
}

// The tail both modes share. The asymmetric rounding (round toward the max
// channel, truncate toward the min) reproduces Photoshop's alternating 2l/2l+1
// channel sums, and it is what makes an all-zero master render a byte-exact
// identity for every input in the RGB cube.
RgbColor photoshop_hsl_reconstruct(int light, double half_chroma, int sector, double interpolant) {
  const auto q = std::min(255, light + static_cast<int>(half_chroma + 0.5));
  const auto p = std::max(0, light - static_cast<int>(half_chroma));
  const auto mid = p + static_cast<int>(static_cast<double>(q - p) * interpolant / 255.0 + 0.5);
  const auto q8 = static_cast<std::uint8_t>(q);
  const auto p8 = static_cast<std::uint8_t>(p);
  const auto m8 = static_cast<std::uint8_t>(mid);
  switch (sector) {
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

// Photoshop's effective master saturation multiplier per slider percent,
// indexed delta + 100. Fitted by maximum-agreement interval overlap over the
// full 32,640-entry (lightness, chroma) probe grid at hue 0 and lightness 0
// (docs/ps-compat.md). No closed form reproduces it: -100 is exactly 0, 0 is
// exactly 1, -50 lands on 0.5 and +50 on 2.0, but +40 and +60 sit measurably
// below 1/(1 - s/100), so all 201 percents were probed. +100 is 128, NOT
// unbounded: Photoshop leaves a chroma-1 midtone at half-saturation there.
constexpr std::array<double, 201> kMasterSaturationScale = {
    0.000000000, 0.015503876, 0.027027027, 0.034482759, 0.047619048,
    0.054545455, 0.066666667, 0.074074074, 0.085714286, 0.097560976,
    0.105263158, 0.117647059, 0.125000000, 0.136363636, 0.142857143,
    0.155963303, 0.163934426, 0.176470588, 0.187500000, 0.195121951,
    0.206896552, 0.214285714, 0.226415094, 0.235294118, 0.247311828,
    0.253333333, 0.266666667, 0.277777778, 0.285714286, 0.296296296,
    0.304347826, 0.315789474, 0.324324324, 0.333333333, 0.347826087,
    0.355555556, 0.368421053, 0.375000000, 0.387096774, 0.393939394,
    0.406250000, 0.413793103, 0.425531915, 0.437500000, 0.444444444,
    0.457142857, 0.465116279, 0.476190476, 0.483870968, 0.496062992,
    0.500000000, 0.515151515, 0.527272727, 0.534883721, 0.545454545,
    0.555555556, 0.566037736, 0.574468085, 0.586206897, 0.600000000,
    0.606060606, 0.617021277, 0.625000000, 0.636363636, 0.645161290,
    0.656000000, 0.666666667, 0.675675676, 0.687500000, 0.695652174,
    0.707317073, 0.714285714, 0.727272727, 0.733333333, 0.747368421,
    0.753246753, 0.764705882, 0.777777778, 0.785714286, 0.800000000,
    0.804878049, 0.816326531, 0.823529412, 0.836363636, 0.847457627,
    0.857142857, 0.866666667, 0.875000000, 0.886792453, 0.894736842,
    0.905982906, 0.914285714, 0.925925926, 0.937500000, 0.945454545,
    0.956521739, 0.965517241, 0.976744186, 0.984126984, 1.000000000,
    1.000000000, 1.011764706, 1.023255814, 1.031250000, 1.043478261,
    1.050847458, 1.066666667, 1.074074074, 1.086956522, 1.097560976,
    1.111111111, 1.121212121, 1.137254902, 1.153846154, 1.160000000,
    1.176470588, 1.187500000, 1.205128205, 1.216216216, 1.235294118,
    1.247311828, 1.266666667, 1.277777778, 1.297872340, 1.310344828,
    1.333333333, 1.352941176, 1.368421053, 1.388888889, 1.403508772,
    1.428571429, 1.444444444, 1.470588235, 1.485714286, 1.514285714,
    1.529411765, 1.560000000, 1.575757576, 1.608695652, 1.640000000,
    1.658536585, 1.692307692, 1.716981132, 1.750000000, 1.777777778,
    1.811594203, 1.838709677, 1.882352941, 1.909090909, 1.952380952,
    2.000000000, 2.030769231, 2.076923077, 2.111111111, 2.166666667,
    2.200000000, 2.263157895, 2.307692308, 2.368421053, 2.411764706,
    2.481481481, 2.533333333, 2.609756098, 2.692307692, 2.750000000,
    2.842105263, 2.904761905, 3.000000000, 3.081081081, 3.200000000,
    3.275862069, 3.411764706, 3.500000000, 3.666666667, 3.761904762,
    3.933333333, 4.125000000, 4.263157895, 4.500000000, 4.666666667,
    4.923076923, 5.117647059, 5.444444444, 5.692307692, 6.090909091,
    6.400000000, 6.909090909, 7.333333333, 8.000000000, 8.818181818,
    9.500000000, 10.666666667, 11.666666667, 13.500000000, 15.000000000,
    18.250000000, 21.333333333, 28.400000000, 36.500000000, 64.000000000,
    128.000000000,
};

RgbColor apply_colorize(RgbColor color, const HueSaturationAdjustment& settings) {
  const int hue = ((settings.colorize_hue % 360) + 360) % 360;
  const auto saturation = std::clamp(settings.colorize_saturation, 0, 100);
  const auto lightness = std::clamp(settings.colorize_lightness, -100, 100);

  const int maximum = std::max({color.red, color.green, color.blue});
  const int minimum = std::min({color.red, color.green, color.blue});
  const int light = photoshop_lightness_value((maximum + minimum) >> 1, lightness);
  const int band = std::min(light, 255 - light);
  const auto half_chroma = band * kColorizeSaturationScale[static_cast<std::size_t>(saturation)];
  return photoshop_hsl_reconstruct(light, half_chroma, hue / 60,
                                   static_cast<double>(kColorizeHueInterp[static_cast<std::size_t>(hue)]));
}

// A band's strength at an input hue: 0 outside the outer stops, ramping in
// between outer_start and inner_start, 1 between the inner stops, ramping out
// between inner_end and outer_end. Stops run in wheel order and may wrap past
// 360. Verified against Photoshop over the whole wheel for the default
// hextants, a narrow custom range, and hard edges (outer == inner).
double hue_saturation_band_weight(double hue_degrees, const HueSaturationBand& band) {
  const auto forward = [](int from, double to) {
    auto delta = std::fmod(to - static_cast<double>(from), 360.0);
    return delta < 0.0 ? delta + 360.0 : delta;
  };
  const auto ramp_in = forward(band.outer_start, band.inner_start);
  const auto plateau = forward(band.inner_start, band.inner_end);
  const auto ramp_out = forward(band.inner_end, band.outer_end);
  if (ramp_in + plateau + ramp_out <= 0.0) {
    return 0.0;
  }
  const auto position = forward(band.outer_start, hue_degrees);
  if (position < ramp_in) {
    return position / ramp_in;
  }
  if (position <= ramp_in + plateau) {
    return 1.0;
  }
  const auto tail = position - ramp_in - plateau;
  if (tail >= ramp_out) {
    return 0.0;
  }
  return 1.0 - tail / ramp_out;
}

RgbColor apply_hue_saturation(RgbColor color, HueSaturationAdjustment settings) {
  if (settings.colorize) {
    return apply_colorize(color, settings);
  }
  const auto hue_shift = std::clamp(settings.hue_shift, -180, 180);
  const auto saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  const auto lightness_delta = std::clamp(settings.lightness_delta, -100, 100);

  // Band contributions ride on top of the master sliders. Photoshop selects the
  // band from the pixel's ORIGINAL hue (the master rotation does not move the
  // selection, verified), and the band lightness runs BEFORE the master one.
  std::array<double, 6> band_weights{};
  auto banded = color;
  if (settings.any_band_has_effect()) {
    const auto wheel = photoshop_wheel_position(color);
    const auto hue_degrees = wheel / 4.25;
    int sector = 0;
    double interpolant = 0.0;
    photoshop_wheel_split(wheel, sector, interpolant);
    auto maximum = static_cast<double>(std::max({color.red, color.green, color.blue}));
    auto minimum = static_cast<double>(std::min({color.red, color.green, color.blue}));
    double lightness = 0.0;
    for (std::size_t index = 0; index < settings.bands.size(); ++index) {
      const auto& band = settings.bands[index];
      if (!band.has_effect()) {
        continue;
      }
      band_weights[index] = hue_saturation_band_weight(hue_degrees, band);
      lightness += band_weights[index] * std::clamp(band.lightness_delta, -100, 100);
    }
    // A band's Lightness is NOT the master blend toward white/black: it
    // collapses the chroma toward the max channel (positive) or the min channel
    // (negative), so +100 flattens the range to gray(max). Overlapping bands SUM
    // their weighted percents and apply once, which is what keeps a default
    // hextant crossfade continuous (applying them in turn overshoots by 16/255).
    if (lightness > 0.0) {
      minimum += (maximum - minimum) * std::min(lightness, 100.0) / 100.0;
    } else if (lightness < 0.0) {
      maximum += (minimum - maximum) * std::min(-lightness, 100.0) / 100.0;
    }
    const auto high = std::clamp(static_cast<int>(maximum + 0.5), 0, 255);
    const auto low = std::clamp(static_cast<int>(minimum + 0.5), 0, 255);
    banded = photoshop_hsl_reconstruct((high + low) >> 1, (high - low) * 0.5, sector, interpolant);
  }
  color = banded;

  // The lightness slider runs first and per channel. Cache its 256-entry ramp
  // per thread: the slider is constant for a whole layer, so recomputing three
  // doubles per pixel is the hot cost on this path (the apply_curves precedent).
  struct CachedLightnessRamp {
    int lightness{0};
    bool valid{false};
    std::array<std::uint8_t, 256> values{};
  };
  thread_local CachedLightnessRamp ramp;
  if (!ramp.valid || ramp.lightness != lightness_delta) {
    for (int value = 0; value < 256; ++value) {
      ramp.values[static_cast<std::size_t>(value)] =
          static_cast<std::uint8_t>(photoshop_lightness_value(value, lightness_delta));
    }
    ramp.lightness = lightness_delta;
    ramp.valid = true;
  }
  const RgbColor lit{ramp.values[color.red], ramp.values[color.green], ramp.values[color.blue]};

  const int maximum = std::max({lit.red, lit.green, lit.blue});
  const int minimum = std::min({lit.red, lit.green, lit.blue});
  if (maximum == minimum) {
    return lit;  // Photoshop's master sliders never tint a neutral pixel.
  }

  const int light = (maximum + minimum) >> 1;
  const auto half = static_cast<double>(maximum - minimum) * 0.5;
  // The bands form one combined adjustment that then composes with the master:
  // their weighted saturation offsets from 1 SUM (a product would bump the
  // ratio above a single band's in every default-hextant crossfade), and the
  // band total MULTIPLIES the master ratio. Hue rotations add in whole wheel
  // steps, converted per band.
  auto band_saturation_offset = 0.0;
  auto rotation = std::floor(static_cast<double>(hue_shift) * 4.25 + 0.5);
  for (std::size_t index = 0; index < settings.bands.size(); ++index) {
    const auto weight = band_weights[index];
    if (weight <= 0.0) {
      continue;
    }
    const auto& band = settings.bands[index];
    const auto band_saturation = std::clamp(band.saturation_delta, -100, 100);
    if (band_saturation != 0) {
      band_saturation_offset +=
          weight * (kMasterSaturationScale[static_cast<std::size_t>(band_saturation + 100)] - 1.0);
    }
    const auto band_hue = std::clamp(band.hue_shift, -180, 180);
    if (band_hue != 0) {
      rotation += std::floor(weight * static_cast<double>(band_hue) * 4.25 + 0.5);
    }
  }
  const auto ratio = kMasterSaturationScale[static_cast<std::size_t>(saturation_delta + 100)] *
                     std::max(0.0, 1.0 + band_saturation_offset);
  // Saturation grows toward the in-gamut limit and never falls below the
  // incoming chroma, which is what keeps an all-zero render an exact identity
  // for the {min == 0, max odd} and {max == 255, min even} inputs.
  const auto limit = std::max(static_cast<double>(std::min(light, 255 - light)), half);
  const auto half_chroma = std::min(half * ratio, limit);

  // Photoshop rotates by a whole number of wheel steps, floor(degrees * 4.25 +
  // 0.5), not by the continuous 4.25 * degrees: measured on all 360 slider
  // degrees, the two disagree at every degree that is not a multiple of four.
  int sector = 0;
  double interpolant = 0.0;
  photoshop_wheel_split(photoshop_wheel_position(lit) + rotation, sector, interpolant);
  return photoshop_hsl_reconstruct(light, half_chroma, sector, interpolant);
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

std::array<HueSaturationBand, 6> default_hue_saturation_bands() {
  std::array<HueSaturationBand, 6> bands{};
  for (std::size_t index = 0; index < bands.size(); ++index) {
    const auto& range = kHueSaturationDefaultBandRanges[index];
    bands[index] = HueSaturationBand{range[0], range[1], range[2], range[3], 0, 0, 0};
  }
  return bands;
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

void set_levels_master_record(LevelsAdjustment& settings, LevelsRecord record) {
  record = clamp_levels_record(record);
  settings.black_input = record.black_input;
  settings.white_input = record.white_input;
  settings.gamma_percent = record.gamma_percent;
  settings.black_output = record.black_output;
  settings.white_output = record.white_output;
}

LevelsRecord levels_record_for_channel(const LevelsAdjustment& settings, LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return clamp_levels_record(settings.red);
    case LevelsChannel::Green:
      return clamp_levels_record(settings.green);
    case LevelsChannel::Blue:
      return clamp_levels_record(settings.blue);
    case LevelsChannel::Rgb:
      return levels_master_record(settings);
  }
  return {};
}

void set_levels_record_for_channel(LevelsAdjustment& settings, LevelsChannel channel, LevelsRecord record) {
  record = clamp_levels_record(record);
  switch (channel) {
    case LevelsChannel::Red:
      settings.red = record;
      return;
    case LevelsChannel::Green:
      settings.green = record;
      return;
    case LevelsChannel::Blue:
      settings.blue = record;
      return;
    case LevelsChannel::Rgb:
      set_levels_master_record(settings, record);
      return;
  }
}

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

CurvesAdjustment curves_adjustment_from_eyedropper_samples(const CurvesEyedropperSamples& samples) {
  CurvesAdjustment curves;
  const auto channel_curve = [&samples](auto component) {
    int black = samples.black.has_value() ? static_cast<int>(component(*samples.black)) : 0;
    int white = samples.white.has_value() ? static_cast<int>(component(*samples.white)) : 255;
    black = std::clamp(black, 0, 254);
    white = std::clamp(white, 1, 255);
    if (black >= white) {
      // Degenerate samples still produce a deterministic usable range without
      // duplicate inputs (the native Curves formats reject those).
      if (samples.white.has_value() && !samples.black.has_value()) {
        black = std::max(0, white - 1);
      } else {
        white = std::min(255, black + 1);
        if (black >= white) {
          black = white - 1;
        }
      }
    }

    CurveControlPoints points{{black, 0}, {white, 255}};
    if (samples.gray.has_value() && white - black >= 2) {
      const auto neutral_input =
          std::clamp(static_cast<int>(component(*samples.gray)), black + 1, white - 1);
      points.insert(points.begin() + 1, CurveControlPoint{neutral_input, 128});
    }
    return normalized_curve_control_points(std::move(points));
  };

  curves.red = channel_curve([](RgbColor color) { return color.red; });
  curves.green = channel_curve([](RgbColor color) { return color.green; });
  curves.blue = channel_curve([](RgbColor color) { return color.blue; });
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

std::uint8_t posterize_channel_value(std::uint8_t value, int levels) {
  const auto denominator = std::max(1, levels - 1);
  const auto bucket =
      static_cast<int>(std::round(static_cast<double>(value) * denominator / 255.0));
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(static_cast<double>(bucket) * 255.0 / denominator), 0L, 255L));
}

int threshold_luminance(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return (static_cast<int>(red) * 30 + static_cast<int>(green) * 59 + static_cast<int>(blue) * 11) / 100;
}

std::uint8_t brightness_contrast_channel_value(std::uint8_t value, int brightness, int contrast) {
  // PS 2026 legacy-mode calibration (nine 256-ramp COM captures, July 2026):
  // positive contrast folds brightness into the INPUT and expands around the
  // 127.5 pivot with slope 100/(100-c); c = 100 is a hard threshold at
  // (v + b) >= 127; negative contrast compresses around 127.5 first and adds
  // brightness to the OUTPUT. Every capture matches this model within +/-1
  // (Photoshop's own LUT shows sub-LSB fixed-point noise); the c = 0,
  // c = 100, and both 100-level interaction captures match exactly.
  const auto b = std::clamp(brightness, -100, 100);
  const auto c = std::clamp(contrast, -100, 100);
  if (c == 0) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value) + b, 0, 255));
  }
  if (c >= 100) {
    return static_cast<int>(value) + b >= 127 ? 255 : 0;
  }
  if (c > 0) {
    const auto raw = (static_cast<double>(value) + b - 127.5) * 100.0 / (100.0 - c) + 127.5;
    return static_cast<std::uint8_t>(std::clamp(std::lround(raw), 0L, 255L));
  }
  const auto raw = (static_cast<double>(value) - 127.5) * (100.0 + c) / 100.0 + 127.5;
  return static_cast<std::uint8_t>(std::clamp(std::lround(raw) + b, 0L, 255L));
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
    case AdjustmentKind::Invert:
      return "invert";
    case AdjustmentKind::Posterize:
      return "posterize";
    case AdjustmentKind::Threshold:
      return "threshold";
    case AdjustmentKind::BrightnessContrast:
      return "brightness_contrast";
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
    case AdjustmentKind::Invert:
      return "Invert";
    case AdjustmentKind::Posterize:
      return "Posterize";
    case AdjustmentKind::Threshold:
      return "Threshold";
    case AdjustmentKind::BrightnessContrast:
      return "Brightness/Contrast";
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
  if (key == "invert") {
    return AdjustmentKind::Invert;
  }
  if (key == "posterize") {
    return AdjustmentKind::Posterize;
  }
  if (key == "threshold") {
    return AdjustmentKind::Threshold;
  }
  if (key == "brightness_contrast") {
    return AdjustmentKind::BrightnessContrast;
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
  settings.hue_saturation.bands = default_hue_saturation_bands();
  for (std::size_t index = 0; index < settings.hue_saturation.bands.size(); ++index) {
    const auto key = std::string(kLayerMetadataAdjustmentHueSaturationBandPrefix) + std::to_string(index);
    const auto encoded = metadata_string_or(layer, key.c_str(), {});
    if (const auto band = parse_hue_saturation_band(encoded)) {
      settings.hue_saturation.bands[index] = *band;
    }
  }
  settings.color_balance.cyan_red = metadata_int_or(layer, kLayerMetadataAdjustmentColorBalanceCyanRed, 0);
  settings.color_balance.magenta_green = metadata_int_or(layer, kLayerMetadataAdjustmentColorBalanceMagentaGreen, 0);
  settings.color_balance.yellow_blue = metadata_int_or(layer, kLayerMetadataAdjustmentColorBalanceYellowBlue, 0);
  settings.posterize.levels =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentPosterizeLevels, 4), 2, 255);
  settings.threshold.level =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentThresholdLevel, 128), 1, 255);
  settings.brightness_contrast.brightness =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentBrightnessContrastBrightness, 0), -100, 100);
  settings.brightness_contrast.contrast =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentBrightnessContrastContrast, 0), -100, 100);
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
  for (std::size_t index = 0; index < settings.hue_saturation.bands.size(); ++index) {
    set_metadata_string(layer, (std::string(kLayerMetadataAdjustmentHueSaturationBandPrefix) +
                                std::to_string(index))
                                   .c_str(),
                        serialize_hue_saturation_band(settings.hue_saturation.bands[index]));
  }
  set_metadata_int(layer, kLayerMetadataAdjustmentColorBalanceCyanRed,
                   std::clamp(settings.color_balance.cyan_red, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentColorBalanceMagentaGreen,
                   std::clamp(settings.color_balance.magenta_green, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentColorBalanceYellowBlue,
                   std::clamp(settings.color_balance.yellow_blue, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentPosterizeLevels,
                   std::clamp(settings.posterize.levels, 2, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentThresholdLevel,
                   std::clamp(settings.threshold.level, 1, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentBrightnessContrastBrightness,
                   std::clamp(settings.brightness_contrast.brightness, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentBrightnessContrastContrast,
                   std::clamp(settings.brightness_contrast.contrast, -100, 100));
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
    case AdjustmentKind::Invert:
      // The one shared formula with the destructive patchy.filters.invert path
      // (filter_engine.cpp); both must stay 255 - v per channel.
      return RgbColor{static_cast<std::uint8_t>(255 - color.red), static_cast<std::uint8_t>(255 - color.green),
                      static_cast<std::uint8_t>(255 - color.blue)};
    case AdjustmentKind::Posterize:
      return RgbColor{posterize_channel_value(color.red, settings.posterize.levels),
                      posterize_channel_value(color.green, settings.posterize.levels),
                      posterize_channel_value(color.blue, settings.posterize.levels)};
    case AdjustmentKind::Threshold: {
      const auto value = threshold_luminance(color.red, color.green, color.blue) >= settings.threshold.level
                             ? std::uint8_t{255}
                             : std::uint8_t{0};
      return RgbColor{value, value, value};
    }
    case AdjustmentKind::BrightnessContrast: {
      const auto brightness = settings.brightness_contrast.brightness;
      const auto contrast = settings.brightness_contrast.contrast;
      return RgbColor{brightness_contrast_channel_value(color.red, brightness, contrast),
                      brightness_contrast_channel_value(color.green, brightness, contrast),
                      brightness_contrast_channel_value(color.blue, brightness, contrast)};
    }
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
  // Hue/Saturation mixes channels through HSL; Threshold compares the mixed
  // RGB luminance, so a per-channel gray-probe LUT would be wrong for any
  // colored pixel. Both take the per-pixel path.
  if (settings.kind == AdjustmentKind::HueSaturation || settings.kind == AdjustmentKind::Threshold) {
    return std::nullopt;
  }
  if (settings.kind == AdjustmentKind::Curves) {
    return build_curves_lut(settings.curves);
  }
  AdjustmentLut lut;
  for (int value = 0; value < 256; ++value) {
    const auto probe = static_cast<std::uint8_t>(value);
    // Levels, Curves, Color Balance and Invert are per-channel maps, so a gray
    // probe reads off each channel's transfer curve exactly.
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
             settings.hue_saturation.saturation_delta != 0 || settings.hue_saturation.lightness_delta != 0 ||
             settings.hue_saturation.any_band_has_effect();
    case AdjustmentKind::ColorBalance:
      return settings.color_balance.cyan_red != 0 || settings.color_balance.magenta_green != 0 ||
             settings.color_balance.yellow_blue != 0;
    case AdjustmentKind::Invert:
      return true;  // parameterless; the layer always inverts
    case AdjustmentKind::Posterize:
    case AdjustmentKind::Threshold:
      // Any legal level alters typical content; the conservative "always
      // effective" posture keeps live previews from being dropped.
      return true;
    case AdjustmentKind::BrightnessContrast:
      return settings.brightness_contrast.brightness != 0 || settings.brightness_contrast.contrast != 0;
  }
  return false;
}

}  // namespace patchy
