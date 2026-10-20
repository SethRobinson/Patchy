#include "core/adjustment_layer.hpp"

#include "core/blend_math.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <map>
#include <mutex>
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
  auto curves = CurvesAdjustment{*rgb, *red, *green, *blue};
  if (const auto black = parse_curve_control_points(
          metadata_string_or(layer, kLayerMetadataAdjustmentCurvesBlackPoints, {}));
      black.has_value()) {
    curves.black_ink = *black;
  }
  return curves;
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

// "red;green;blue;constant" for a Channel Mixer row; a malformed value keeps the fallback.
std::string serialize_channel_mixer_row(const ChannelMixerRow& row) {
  const auto clamped = clamp_channel_mixer_row(row);
  return std::to_string(clamped.red) + ';' + std::to_string(clamped.green) + ';' + std::to_string(clamped.blue) +
         ';' + std::to_string(clamped.constant);
}

ChannelMixerRow parse_channel_mixer_row(std::string_view encoded, ChannelMixerRow fallback) {
  std::array<int, 4> fields{};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    const auto separator = encoded.find(';');
    const auto value = parse_int(encoded.substr(0, separator));
    if (!value.has_value() || (index + 1U < fields.size()) == (separator == std::string_view::npos)) {
      return fallback;
    }
    fields[index] = *value;
    encoded = separator == std::string_view::npos ? std::string_view{} : encoded.substr(separator + 1U);
  }
  return clamp_channel_mixer_row(ChannelMixerRow{fields[0], fields[1], fields[2], fields[3]});
}

// "L;a;b" in hundredths for the Photo Filter color; a malformed value keeps the fallback.
std::string serialize_photo_filter_lab(const PhotoFilterAdjustment& settings) {
  const auto clamped = clamp_photo_filter(settings);
  return std::to_string(clamped.lab_l) + ';' + std::to_string(clamped.lab_a) + ';' + std::to_string(clamped.lab_b);
}

PhotoFilterAdjustment parse_photo_filter_lab(std::string_view encoded, PhotoFilterAdjustment fallback) {
  std::array<int, 3> fields{};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    const auto separator = encoded.find(';');
    const auto value = parse_int(encoded.substr(0, separator));
    if (!value.has_value() || (index + 1U < fields.size()) == (separator == std::string_view::npos)) {
      return fallback;
    }
    fields[index] = *value;
    encoded = separator == std::string_view::npos ? std::string_view{} : encoded.substr(separator + 1U);
  }
  fallback.lab_l = fields[0];
  fallback.lab_a = fields[1];
  fallback.lab_b = fields[2];
  return clamp_photo_filter(fallback);
}

// sRGB (IEC 61966-2-1) to CIE XYZ under D50, Bradford-adapted as in the ICC sRGB profile;
// Photoshop's Photo Filter works in this space (calibrated, #250).
constexpr std::array<std::array<double, 3>, 3> kSrgbToXyzD50{{{0.4360747, 0.3850649, 0.1430804},
                                                               {0.2225045, 0.7168786, 0.0606169},
                                                               {0.0139322, 0.0971045, 0.7141733}}};

std::array<double, 3> multiply3(const std::array<std::array<double, 3>, 3>& matrix, const std::array<double, 3>& v) {
  return {matrix[0][0] * v[0] + matrix[0][1] * v[1] + matrix[0][2] * v[2],
          matrix[1][0] * v[0] + matrix[1][1] * v[1] + matrix[1][2] * v[2],
          matrix[2][0] * v[0] + matrix[2][1] * v[1] + matrix[2][2] * v[2]};
}

std::array<std::array<double, 3>, 3> inverse3(const std::array<std::array<double, 3>, 3>& m) {
  const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                     m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                     m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  return {{{(m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det,
            (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det},
           {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det,
            (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det},
           {(m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det,
            (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det}}};
}

const std::array<std::array<double, 3>, 3>& xyz_d50_to_srgb() {
  static const auto inverse = inverse3(kSrgbToXyzD50);
  return inverse;
}

std::array<double, 3> d50_white() {
  return {kSrgbToXyzD50[0][0] + kSrgbToXyzD50[0][1] + kSrgbToXyzD50[0][2],
          kSrgbToXyzD50[1][0] + kSrgbToXyzD50[1][1] + kSrgbToXyzD50[1][2],
          kSrgbToXyzD50[2][0] + kSrgbToXyzD50[2][1] + kSrgbToXyzD50[2][2]};
}

// 0..255 encoded to linear 0..1 and back; the encoder clamps to the sRGB gamut.
double srgb_byte_to_linear(double value) {
  const double v = value / 255.0;
  return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double linear_to_srgb_byte(double value) {
  const double v = std::clamp(value, 0.0, 1.0);
  return 255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055);
}

double round_half_up(double value) {
  return std::floor(value + 0.5);
}

// PDF luma on the 0..255 scale; Photoshop's 8-bit path rounds it to a whole level.
double photo_filter_luma(const std::array<double, 3>& color, bool byte_stages) {
  const double luma = (30.0 * color[0] + 59.0 * color[1] + 11.0 * color[2]) / 100.0;
  return byte_stages ? round_half_up(luma) : luma;
}

// The Gradient Map gradient as "method;reverse;dither;smoothness|stops|opacity stops" in
// Photoshop's integer units, so a 'grdm' round trip through metadata is exact.
std::string serialize_gradient_map(const LayerStyleGradient& gradient) {
  const auto unit = [](float value, float scale) { return std::to_string(std::lround(value * scale)); };
  std::string encoded = std::to_string(static_cast<int>(gradient.interpolation)) + ';' +
                        (gradient.reverse ? "1" : "0") + ';' + (gradient.dither ? "1" : "0") + ';' +
                        std::to_string(gradient.smoothness) + '|';
  for (std::size_t index = 0; index < gradient.color_stops.size(); ++index) {
    const auto& stop = gradient.color_stops[index];
    encoded += (index == 0 ? "" : ";") + unit(stop.location, 4096.0F) + ',' + unit(stop.midpoint, 100.0F) + ',' +
               std::to_string(stop.color.red) + ',' + std::to_string(stop.color.green) + ',' +
               std::to_string(stop.color.blue);
  }
  encoded += '|';
  for (std::size_t index = 0; index < gradient.alpha_stops.size(); ++index) {
    const auto& stop = gradient.alpha_stops[index];
    encoded += (index == 0 ? "" : ";") + unit(stop.location, 4096.0F) + ',' + unit(stop.midpoint, 100.0F) + ',' +
               unit(stop.opacity, 255.0F);
  }
  return encoded;
}

std::vector<std::vector<int>> parse_int_records(std::string_view encoded, std::size_t fields) {
  std::vector<std::vector<int>> records;
  while (!encoded.empty()) {
    const auto end = encoded.find(';');
    auto record = encoded.substr(0, end);
    std::vector<int> values;
    while (!record.empty()) {
      const auto comma = record.find(',');
      const auto value = parse_int(record.substr(0, comma));
      if (!value.has_value()) {
        return {};
      }
      values.push_back(*value);
      record = comma == std::string_view::npos ? std::string_view{} : record.substr(comma + 1U);
    }
    if (values.size() != fields) {
      return {};
    }
    records.push_back(std::move(values));
    encoded = end == std::string_view::npos ? std::string_view{} : encoded.substr(end + 1U);
  }
  return records;
}

std::optional<LayerStyleGradient> parse_gradient_map(std::string_view encoded) {
  const auto first = encoded.find('|');
  const auto second = first == std::string_view::npos ? first : encoded.find('|', first + 1U);
  if (second == std::string_view::npos) {
    return std::nullopt;
  }
  std::vector<int> head;
  {
    auto text = encoded.substr(0, first);
    while (!text.empty()) {
      const auto end = text.find(';');
      const auto value = parse_int(text.substr(0, end));
      if (!value.has_value()) {
        return std::nullopt;
      }
      head.push_back(*value);
      text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1U);
    }
  }
  if (head.size() != 4U) {
    return std::nullopt;
  }
  const auto colors = parse_int_records(encoded.substr(first + 1U, second - first - 1U), 5);
  const auto alphas = parse_int_records(encoded.substr(second + 1U), 3);
  if (colors.empty()) {
    return std::nullopt;
  }
  LayerStyleGradient gradient = GradientMapAdjustment::default_gradient_map_gradient();
  gradient.interpolation = static_cast<GradientInterpolationMethod>(std::clamp(head[0], 0, 2));
  gradient.reverse = head[1] != 0;
  gradient.dither = head[2] != 0;
  gradient.smoothness = static_cast<std::uint16_t>(std::clamp(head[3], 0, 4096));
  gradient.color_stops.clear();
  for (const auto& record : colors) {
    GradientColorStop stop;
    stop.location = static_cast<float>(std::clamp(record[0], 0, 4096)) / 4096.0F;
    stop.midpoint = static_cast<float>(std::clamp(record[1], 0, 100)) / 100.0F;
    stop.color = RgbColor{static_cast<std::uint8_t>(std::clamp(record[2], 0, 255)),
                          static_cast<std::uint8_t>(std::clamp(record[3], 0, 255)),
                          static_cast<std::uint8_t>(std::clamp(record[4], 0, 255))};
    gradient.color_stops.push_back(stop);
  }
  gradient.alpha_stops.clear();
  for (const auto& record : alphas) {
    GradientAlphaStop stop;
    stop.location = static_cast<float>(std::clamp(record[0], 0, 4096)) / 4096.0F;
    stop.midpoint = static_cast<float>(std::clamp(record[1], 0, 100)) / 100.0F;
    stop.opacity = static_cast<float>(std::clamp(record[2], 0, 255)) / 255.0F;
    gradient.alpha_stops.push_back(stop);
  }
  return gradient;
}

// "a;b;c;..." integers; a malformed or short value returns nullopt.
std::optional<std::vector<int>> parse_int_list(std::string_view encoded, std::size_t count) {
  std::vector<int> values;
  while (!encoded.empty()) {
    const auto separator = encoded.find(';');
    const auto value = parse_int(encoded.substr(0, separator));
    if (!value.has_value()) {
      return std::nullopt;
    }
    values.push_back(*value);
    encoded = separator == std::string_view::npos ? std::string_view{} : encoded.substr(separator + 1U);
  }
  if (values.size() != count) {
    return std::nullopt;
  }
  return values;
}

// PDF SetLum with ClipColor on the 0..255 scale, with luma (30R + 59G + 11B) / 100 rounded
// to a whole level when byte_stages is set.
std::array<double, 3> set_luma(std::array<double, 3> color, double target, bool byte_stages) {
  const auto luma = [byte_stages](const std::array<double, 3>& value) {
    const double raw = (30.0 * value[0] + 59.0 * value[1] + 11.0 * value[2]) / 100.0;
    return byte_stages ? std::floor(raw + 0.5) : raw;
  };
  const double shift = target - luma(color);
  for (auto& value : color) {
    value += shift;
  }
  const double center = luma(color);
  const double low = std::min({color[0], color[1], color[2]});
  const double high = std::max({color[0], color[1], color[2]});
  if (low < 0.0 && center > low) {
    for (auto& value : color) {
      value = center + (value - center) * center / (center - low);
    }
  }
  if (high > 255.0 && high > center) {
    for (auto& value : color) {
      value = center + (value - center) * (255.0 - center) / (high - center);
    }
  }
  return color;
}

// The luma Photoshop's Gradient Map indexes the gradient with (calibrated, #250).
double gradient_map_luma(double red, double green, double blue) {
  return (30.0 * red + 59.0 * green + 11.0 * blue) / 100.0;
}

// "cyan_red;magenta_green;yellow_blue" for the shadows and highlights triples.
std::string serialize_color_balance_tone(const ColorBalanceTone& tone) {
  return std::to_string(std::clamp(tone.cyan_red, -100, 100)) + ';' +
         std::to_string(std::clamp(tone.magenta_green, -100, 100)) + ';' +
         std::to_string(std::clamp(tone.yellow_blue, -100, 100));
}

ColorBalanceTone parse_color_balance_tone(std::string_view encoded) {
  std::array<int, 3> fields{};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    const auto separator = encoded.find(';');
    const auto value = parse_int(encoded.substr(0, separator));
    if (!value.has_value() || (index + 1U < fields.size()) == (separator == std::string_view::npos)) {
      return {};
    }
    fields[index] = std::clamp(*value, -100, 100);
    encoded = separator == std::string_view::npos ? std::string_view{} : encoded.substr(separator + 1U);
  }
  return ColorBalanceTone{fields[0], fields[1], fields[2]};
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

// Photoshop's Levels gamma curve, normalized 0..1 in and out. Above 1 the plain power
// x^(1/gamma) has an infinite slope at black; Photoshop replaces it below
// t = 2^(-gamma - 1/(gamma - 1)) with a cubic Hermite segment that leaves black at slope
// 2^gamma and meets the power curve in value and slope at t. Fitted on Photoshop 2026
// ramps at gamma 1.2 to 9.99, 8 and 16-bit (docs/high-bit-depth.md): within 3/65535
// up to gamma 3, where the plain power is off by up to 9072/65535 near black.
double photoshop_levels_gamma(double normalized, double gamma) {
  if (gamma <= 1.0) {
    return std::pow(normalized, 1.0 / gamma);
  }
  const auto inverse_gamma = 1.0 / gamma;
  const auto toe_end = std::exp2(-gamma - 1.0 / (gamma - 1.0));
  if (normalized >= toe_end) {
    return std::pow(normalized, inverse_gamma);
  }
  const auto end_value = std::pow(toe_end, inverse_gamma);
  const auto end_slope = inverse_gamma * end_value / toe_end;
  const auto start_slope = std::exp2(gamma);
  const auto u = normalized / toe_end;
  const auto u2 = u * u;
  const auto u3 = u2 * u;
  return (u3 - 2.0 * u2 + u) * toe_end * start_slope + (3.0 * u2 - 2.0 * u3) * end_value +
         (u3 - u2) * toe_end * end_slope;
}

// One Levels record as a real-valued transfer; an identity record returns its input
// untouched so a master-only adjustment stays bit-identical to a single stage.
double levels_value(double value, LevelsRecord record) {
  record = clamp_levels_record(record);
  if (!levels_record_has_effect(record)) {
    return value;
  }
  const auto input_range = static_cast<double>(record.white_input - record.black_input);
  const auto gamma = static_cast<double>(record.gamma_percent) / 100.0;
  const auto normalized = std::clamp((value - static_cast<double>(record.black_input)) / input_range, 0.0, 1.0);
  const auto leveled = gamma <= 0.0 ? normalized : photoshop_levels_gamma(normalized, gamma);
  return static_cast<double>(record.black_output) +
         leveled * static_cast<double>(record.white_output - record.black_output);
}

// Photoshop applies the component channel first, then Composite RGB, as it does for
// Curves. Pinned against Photoshop's render of psd-tools' levels_rgb.psd (October 2026):
// this order is within 2/255 everywhere, the reverse is off by up to 52. The channel
// result feeds the composite stage unrounded; rounding it to a byte in between measures
// ten times as many 2/255 misses on the same render.
RgbColor apply_levels(RgbColor color, LevelsAdjustment settings) {
  const auto master = levels_master_record(settings);
  const auto channel = [&master](std::uint8_t value, LevelsRecord record) {
    return clamp_byte(static_cast<float>(levels_value(levels_value(static_cast<double>(value), record), master)));
  };
  return RgbColor{channel(color.red, settings.red), channel(color.green, settings.green),
                  channel(color.blue, settings.blue)};
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
// probe files (docs/adjustments-calibration.md "Hue/Saturation calibration"). Colorize and the master
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
// (docs/adjustments-calibration.md). No closed form reproduces it: -100 is exactly 0, 0 is
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

RgbColor apply_color_balance(RgbColor color, const ColorBalanceAdjustment& settings) {
  const auto levels = color_balance_channel_levels(settings);
  const auto channel = [](std::uint8_t value, ColorBalanceChannelLevels record) {
    const auto mapped = color_balance_transfer(static_cast<double>(value), record, true);
    return static_cast<std::uint8_t>(std::clamp(std::floor(mapped + 0.5), 0.0, 255.0));
  };
  return RgbColor{channel(color.red, levels[0]), channel(color.green, levels[1]), channel(color.blue, levels[2])};
}

}  // namespace

const char* photoshop_placeholder_adjustment_kind(std::string_view block_key) noexcept {
  // Channel Mixer and Photo Filter stay listed for block versions their parsers do not read.
  static constexpr std::array<std::pair<std::string_view, const char*>, 7> kKinds{{
      {"vibA", "vibrance"},
      {"blwh", "black_white"},
      {"phfl", "photo_filter"},
      {"mixr", "channel_mixer"},
      {"grdm", "gradient_map"},
      {"clrL", "color_lookup"},
      {"selc", "selective_color"},
  }};
  for (const auto& [key, kind] : kKinds) {
    if (key == block_key) {
      return kind;
    }
  }
  return nullptr;
}

std::string_view adjustment_placeholder_kind(const Layer& layer) {
  return metadata_string_or(layer, kLayerMetadataAdjustmentPlaceholderKind, {});
}

ChannelMixerRow clamp_channel_mixer_row(ChannelMixerRow row) noexcept {
  return ChannelMixerRow{std::clamp(row.red, -200, 200), std::clamp(row.green, -200, 200),
                         std::clamp(row.blue, -200, 200), std::clamp(row.constant, -200, 200)};
}

int channel_mixer_weight(int percent) noexcept {
  // Photoshop truncates each percentage toward zero onto a 1/1024 scale (calibrated, #250).
  return std::clamp(percent, -200, 200) * 1024 / 100;
}

double channel_mixer_transfer(const ChannelMixerRow& row, double red, double green, double blue) {
  // Integer weights over a power-of-two divisor keep 8-bit inputs exact, so rounding this
  // half up reproduces Photoshop's 8-bit output.
  return (red * channel_mixer_weight(row.red) + green * channel_mixer_weight(row.green) +
          blue * channel_mixer_weight(row.blue) + 255.0 * channel_mixer_weight(row.constant)) /
         1024.0;
}

bool ChannelMixerAdjustment::has_effect() const noexcept {
  if (monochrome) {
    return true;  // even 100/0/0 turns color into gray
  }
  const ChannelMixerAdjustment identity;
  return clamp_channel_mixer_row(red) != identity.red || clamp_channel_mixer_row(green) != identity.green ||
         clamp_channel_mixer_row(blue) != identity.blue;
}

PhotoFilterAdjustment clamp_photo_filter(PhotoFilterAdjustment settings) noexcept {
  settings.lab_l = std::clamp(settings.lab_l, 0, 10000);
  settings.lab_a = std::clamp(settings.lab_a, -12800, 12700);
  settings.lab_b = std::clamp(settings.lab_b, -12800, 12700);
  settings.density = std::clamp(settings.density, 1, 100);
  return settings;
}

RgbColor photo_filter_color(const PhotoFilterAdjustment& settings) {
  const auto clamped = clamp_photo_filter(settings);
  const auto f_inverse = [](double t) {
    return t * t * t > 216.0 / 24389.0 ? t * t * t : (116.0 * t - 16.0) / (24389.0 / 27.0);
  };
  const double fy = (clamped.lab_l / 100.0 + 16.0) / 116.0;
  const double fx = fy + clamped.lab_a / 100.0 / 500.0;
  const double fz = fy - clamped.lab_b / 100.0 / 200.0;
  const auto white = d50_white();
  const auto linear =
      multiply3(xyz_d50_to_srgb(), {f_inverse(fx) * white[0], f_inverse(fy) * white[1], f_inverse(fz) * white[2]});
  const auto byte = [](double value) { return static_cast<std::uint8_t>(round_half_up(linear_to_srgb_byte(value))); };
  return RgbColor{byte(linear[0]), byte(linear[1]), byte(linear[2])};
}

PhotoFilterAdjustment photo_filter_with_color(PhotoFilterAdjustment settings, RgbColor color) {
  const auto xyz = multiply3(kSrgbToXyzD50, {srgb_byte_to_linear(color.red), srgb_byte_to_linear(color.green),
                                             srgb_byte_to_linear(color.blue)});
  const auto white = d50_white();
  const auto f = [](double t) { return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
  const double fx = f(xyz[0] / white[0]);
  const double fy = f(xyz[1] / white[1]);
  const double fz = f(xyz[2] / white[2]);
  settings.lab_l = static_cast<int>(round_half_up(100.0 * (116.0 * fy - 16.0)));
  settings.lab_a = static_cast<int>(round_half_up(100.0 * 500.0 * (fx - fy)));
  settings.lab_b = static_cast<int>(round_half_up(100.0 * 200.0 * (fy - fz)));
  return clamp_photo_filter(settings);
}

std::array<double, 3> photo_filter_transfer(const PhotoFilterAdjustment& settings, double red, double green,
                                            double blue, bool byte_stages) {
  const auto clamped = clamp_photo_filter(settings);
  const auto filter = photo_filter_color(clamped);
  const auto white = d50_white();
  const auto filter_xyz = multiply3(kSrgbToXyzD50, {srgb_byte_to_linear(filter.red),
                                                    srgb_byte_to_linear(filter.green),
                                                    srgb_byte_to_linear(filter.blue)});
  const auto source_xyz = multiply3(
      kSrgbToXyzD50, {srgb_byte_to_linear(red), srgb_byte_to_linear(green), srgb_byte_to_linear(blue)});
  const double density = clamped.density / 100.0;
  std::array<double, 3> filtered_xyz{};
  for (std::size_t c = 0; c < 3U; ++c) {
    filtered_xyz[c] = source_xyz[c] * (1.0 - density + density * filter_xyz[c] / white[c]);
  }
  const auto linear = multiply3(xyz_d50_to_srgb(), filtered_xyz);
  std::array<double, 3> out{};
  for (std::size_t c = 0; c < 3U; ++c) {
    out[c] = linear_to_srgb_byte(linear[c]);
    if (byte_stages) {
      out[c] = round_half_up(out[c]);
    }
  }
  if (!clamped.preserve_luminosity) {
    return out;
  }
  // PDF SetLum with ClipColor: shift to the source's luma, then pull out-of-range channels
  // toward the luma. Photoshop clips bright colors slightly differently (up to 7 levels on
  // the probe; docs/adjustment-kinds.md, "Photo Filter").
  const double shift = photo_filter_luma({red, green, blue}, byte_stages) - photo_filter_luma(out, byte_stages);
  for (auto& value : out) {
    value += shift;
  }
  const double luma = photo_filter_luma(out, byte_stages);
  const double low = std::min({out[0], out[1], out[2]});
  const double high = std::max({out[0], out[1], out[2]});
  if (low < 0.0 && luma > low) {
    for (auto& value : out) {
      value = luma + (value - luma) * luma / (luma - low);
    }
  }
  if (high > 255.0 && high > luma) {
    for (auto& value : out) {
      value = luma + (value - luma) * (255.0 - luma) / (high - luma);
    }
  }
  return out;
}

bool ColorLookupTable::valid() const noexcept {
  return size >= 2 && size <= 256 &&
         values.size() == static_cast<std::size_t>(size) * static_cast<std::size_t>(size) *
                              static_cast<std::size_t>(size) * 3U;
}

std::array<double, 3> ColorLookupTable::apply(double red, double green, double blue) const noexcept {
  if (!valid()) {
    return {red, green, blue};
  }
  const double scale = static_cast<double>(size - 1) / 255.0;
  const std::array<double, 3> position{std::clamp(red, 0.0, 255.0) * scale, std::clamp(green, 0.0, 255.0) * scale,
                                       std::clamp(blue, 0.0, 255.0) * scale};
  std::array<int, 3> base{};
  std::array<double, 3> fraction{};
  for (std::size_t c = 0; c < 3U; ++c) {
    base[c] = std::min(static_cast<int>(position[c]), size - 2);
    fraction[c] = position[c] - base[c];
  }
  const auto node = [this, &base](int dr, int dg, int db) {
    const auto index = (static_cast<std::size_t>(base[0] + dr) +
                        static_cast<std::size_t>(base[1] + dg) * static_cast<std::size_t>(size) +
                        static_cast<std::size_t>(base[2] + db) * static_cast<std::size_t>(size) *
                            static_cast<std::size_t>(size)) *
                       3U;
    return std::array<double, 3>{values[index] / 65535.0, values[index + 1] / 65535.0, values[index + 2] / 65535.0};
  };
  if (size == 2) {
    // Photoshop interpolates a two-point grid trilinearly and every larger grid
    // tetrahedrally (probed at 2, 3, 5, 9, 17 and 33 points, #250).
    std::array<double, 3> out{};
    for (int dr = 0; dr < 2; ++dr) {
      for (int dg = 0; dg < 2; ++dg) {
        for (int db = 0; db < 2; ++db) {
          const double weight = (dr != 0 ? fraction[0] : 1.0 - fraction[0]) *
                                (dg != 0 ? fraction[1] : 1.0 - fraction[1]) *
                                (db != 0 ? fraction[2] : 1.0 - fraction[2]);
          const auto corner = node(dr, dg, db);
          for (std::size_t c = 0; c < 3U; ++c) {
            out[c] += 255.0 * weight * corner[c];
          }
        }
      }
    }
    return out;
  }
  // Tetrahedral interpolation: the cube splits into six tetrahedra along its main diagonal,
  // picked by the order of the fractions.
  const double fr = fraction[0];
  const double fg = fraction[1];
  const double fb = fraction[2];
  std::array<double, 3> v1{};
  std::array<double, 3> v2{};
  std::array<double, 3> t{};
  if (fr >= fg && fg >= fb) {
    v1 = node(1, 0, 0);
    v2 = node(1, 1, 0);
    t = {fr, fg, fb};
  } else if (fr >= fb && fb >= fg) {
    v1 = node(1, 0, 0);
    v2 = node(1, 0, 1);
    t = {fr, fb, fg};
  } else if (fb >= fr && fr >= fg) {
    v1 = node(0, 0, 1);
    v2 = node(1, 0, 1);
    t = {fb, fr, fg};
  } else if (fg >= fr && fr >= fb) {
    v1 = node(0, 1, 0);
    v2 = node(1, 1, 0);
    t = {fg, fr, fb};
  } else if (fg >= fb && fb >= fr) {
    v1 = node(0, 1, 0);
    v2 = node(0, 1, 1);
    t = {fg, fb, fr};
  } else {
    v1 = node(0, 0, 1);
    v2 = node(0, 1, 1);
    t = {fb, fg, fr};
  }
  const auto origin = node(0, 0, 0);
  const auto far_corner = node(1, 1, 1);
  std::array<double, 3> out{};
  for (std::size_t c = 0; c < 3U; ++c) {
    out[c] = 255.0 * ((1.0 - t[0]) * origin[c] + (t[0] - t[1]) * v1[c] + (t[1] - t[2]) * v2[c] + t[2] * far_corner[c]);
  }
  return out;
}

std::shared_ptr<const ColorLookupTable> make_color_lookup_table(int size, std::vector<std::uint16_t> values) {
  auto table = std::make_shared<ColorLookupTable>();
  table->size = size;
  table->values = std::move(values);
  if (!table->valid()) {
    return nullptr;
  }
  // FNV-1a over the grid size and the nodes: equal tables share an id.
  std::uint64_t hash = 14695981039346656037ULL;
  const auto mix = [&hash](std::uint64_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<std::uint64_t>(size));
  for (const auto value : table->values) {
    mix(value & 0xFFU);
    mix(value >> 8U);
  }
  static constexpr char kHex[] = "0123456789abcdef";
  table->id = "lut";
  for (int shift = 60; shift >= 0; shift -= 4) {
    table->id += kHex[(hash >> static_cast<unsigned>(shift)) & 0xFU];
  }
  return table;
}

namespace {

// Whitespace-separated tokens of one line, without a trailing comment.
std::vector<std::string_view> lut_tokens(std::string_view line) {
  if (const auto hash = line.find('#'); hash != std::string_view::npos) {
    line = line.substr(0, hash);
  }
  std::vector<std::string_view> tokens;
  std::size_t index = 0;
  while (index < line.size()) {
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t' || line[index] == '\r')) {
      ++index;
    }
    const auto start = index;
    while (index < line.size() && line[index] != ' ' && line[index] != '\t' && line[index] != '\r') {
      ++index;
    }
    if (index > start) {
      tokens.push_back(line.substr(start, index - start));
    }
  }
  return tokens;
}

// A decimal number such as "0.123456", "-1", "1e-3" or ".5", parsed without the C locale
// (strtod reads "0.5" wrongly under a comma-decimal locale) or floating from_chars (missing
// from some supported standard libraries).
std::optional<double> lut_number(std::string_view token) {
  std::size_t index = 0;
  const bool negative = index < token.size() && token[index] == '-';
  if (index < token.size() && (token[index] == '-' || token[index] == '+')) {
    ++index;
  }
  std::uint64_t mantissa = 0;
  int exponent = 0;
  int digits = 0;
  for (; index < token.size() && token[index] >= '0' && token[index] <= '9'; ++index, ++digits) {
    if (mantissa < 100000000000000000ULL) {
      mantissa = mantissa * 10U + static_cast<std::uint64_t>(token[index] - '0');
    } else {
      ++exponent;
    }
  }
  if (index < token.size() && token[index] == '.') {
    for (++index; index < token.size() && token[index] >= '0' && token[index] <= '9'; ++index, ++digits) {
      if (mantissa < 100000000000000000ULL) {
        mantissa = mantissa * 10U + static_cast<std::uint64_t>(token[index] - '0');
        --exponent;
      }
    }
  }
  if (digits == 0) {
    return std::nullopt;
  }
  if (index < token.size() && (token[index] == 'e' || token[index] == 'E')) {
    ++index;
    const bool negative_exponent = index < token.size() && token[index] == '-';
    if (index < token.size() && (token[index] == '-' || token[index] == '+')) {
      ++index;
    }
    int power = 0;
    int power_digits = 0;
    for (; index < token.size() && token[index] >= '0' && token[index] <= '9'; ++index, ++power_digits) {
      power = std::min(power * 10 + (token[index] - '0'), 400);
    }
    if (power_digits == 0) {
      return std::nullopt;
    }
    exponent += negative_exponent ? -power : power;
  }
  if (index != token.size()) {
    return std::nullopt;
  }
  // Powers of ten up to 1e22 are exact doubles, so one division or multiplication rounds
  // correctly and the result is the same on every toolchain.
  double scale = 1.0;
  for (int step = 0; step < std::min(std::abs(exponent), 22); ++step) {
    scale *= 10.0;
  }
  double value = exponent < 0 ? static_cast<double>(mantissa) / scale : static_cast<double>(mantissa) * scale;
  if (std::abs(exponent) > 22) {
    value = exponent < 0 ? value / std::pow(10.0, -exponent - 22) : value * std::pow(10.0, exponent - 22);
  }
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  return negative ? -value : value;
}

std::uint16_t lut_node(double unit) {
  return static_cast<std::uint16_t>(std::lround(std::clamp(unit, 0.0, 1.0) * 65535.0));
}

ColorLookupFileResult parse_cube_lut(std::string_view text) {
  ColorLookupFileResult result;
  int size = 0;
  std::vector<std::uint16_t> values;
  while (!text.empty()) {
    const auto end = text.find('\n');
    const auto tokens = lut_tokens(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1U);
    if (tokens.empty()) {
      continue;
    }
    const auto keyword = tokens.front();
    if (keyword == "TITLE") {
      continue;
    }
    if (keyword == "LUT_1D_SIZE" || keyword == "LUT_1D_INPUT_RANGE") {
      result.error = "1D LUTs are not supported";
      return result;
    }
    if (keyword == "LUT_3D_SIZE") {
      const auto parsed = tokens.size() == 2U ? lut_number(tokens[1]) : std::nullopt;
      size = parsed.has_value() ? static_cast<int>(*parsed) : 0;
      if (size < 2 || size > 256) {
        result.error = "unsupported LUT_3D_SIZE";
        return result;
      }
      values.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) *
                     static_cast<std::size_t>(size) * 3U);
      continue;
    }
    if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX" || keyword == "LUT_3D_INPUT_RANGE") {
      const double expected = keyword == "DOMAIN_MAX" ? 1.0 : 0.0;
      for (std::size_t index = 1; index < tokens.size(); ++index) {
        const auto value = lut_number(tokens[index]);
        if (keyword == "LUT_3D_INPUT_RANGE") {
          if (!value.has_value() || *value != (index == 1U ? 0.0 : 1.0)) {
            result.error = "only the 0..1 input range is supported";
            return result;
          }
        } else if (!value.has_value() || *value != expected) {
          result.error = "only the 0..1 domain is supported";
          return result;
        }
      }
      continue;
    }
    if (tokens.size() != 3U || size == 0) {
      result.error = "unexpected line in the LUT";
      return result;
    }
    for (const auto token : tokens) {
      const auto value = lut_number(token);
      if (!value.has_value()) {
        result.error = "unexpected number in the LUT";
        return result;
      }
      values.push_back(lut_node(*value));
    }
  }
  if (size == 0 || values.size() != static_cast<std::size_t>(size) * static_cast<std::size_t>(size) *
                                          static_cast<std::size_t>(size) * 3U) {
    result.error = "the LUT has the wrong number of entries";
    return result;
  }
  result.table = make_color_lookup_table(size, std::move(values));
  return result;
}

ColorLookupFileResult parse_3dl_lut(std::string_view text) {
  ColorLookupFileResult result;
  std::vector<double> inputs;
  std::vector<std::array<double, 3>> rows;
  while (!text.empty()) {
    const auto end = text.find('\n');
    const auto tokens = lut_tokens(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1U);
    if (tokens.empty() || tokens.front().starts_with("Mesh") || tokens.front().starts_with("LUT")) {
      continue;
    }
    std::vector<double> numbers;
    for (const auto token : tokens) {
      const auto value = lut_number(token);
      if (!value.has_value()) {
        result.error = "unexpected number in the LUT";
        return result;
      }
      numbers.push_back(*value);
    }
    if (inputs.empty()) {
      inputs = std::move(numbers);
      continue;
    }
    if (numbers.size() != 3U) {
      result.error = "unexpected line in the LUT";
      return result;
    }
    rows.push_back({numbers[0], numbers[1], numbers[2]});
  }
  const auto size = static_cast<int>(inputs.size());
  if (size < 2 || size > 256 ||
      rows.size() != static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * static_cast<std::size_t>(size)) {
    result.error = "the LUT has the wrong number of entries";
    return result;
  }
  // The output bit depth is the smallest of 10, 12, 14 and 16 bits that holds every value.
  double largest = 0.0;
  for (const auto& row : rows) {
    largest = std::max({largest, row[0], row[1], row[2]});
  }
  double full_scale = 65535.0;
  for (const double candidate : {1023.0, 4095.0, 16383.0}) {
    if (largest <= candidate) {
      full_scale = candidate;
      break;
    }
  }
  std::vector<std::uint16_t> values(rows.size() * 3U);
  const auto points = static_cast<std::size_t>(size);
  for (std::size_t r = 0; r < points; ++r) {
    for (std::size_t g = 0; g < points; ++g) {
      for (std::size_t b = 0; b < points; ++b) {
        const auto& row = rows[(r * points + g) * points + b];
        const auto index = (r + g * points + b * points * points) * 3U;
        for (std::size_t c = 0; c < 3U; ++c) {
          values[index + c] = lut_node(row[c] / full_scale);
        }
      }
    }
  }
  result.table = make_color_lookup_table(size, std::move(values));
  return result;
}

std::mutex& color_lookup_registry_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::map<std::string, std::shared_ptr<const ColorLookupTable>, std::less<>>& color_lookup_registry() {
  static std::map<std::string, std::shared_ptr<const ColorLookupTable>, std::less<>> registry;
  return registry;
}

}  // namespace

ColorLookupFileResult parse_color_lookup_file(std::span<const std::uint8_t> bytes, std::string_view extension) {
  std::string lowered(extension);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
  if (!lowered.empty() && lowered.front() == '.') {
    lowered.erase(0, 1);
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  ColorLookupFileResult result;
  if (lowered == "cube") {
    result = parse_cube_lut(text);
  } else if (lowered == "3dl") {
    result = parse_3dl_lut(text);
  } else {
    result.error = "unsupported LUT format";
    return result;
  }
  if (result.table != nullptr) {
    auto table = std::make_shared<ColorLookupTable>(*result.table);
    table->source_format = lowered;
    table->source.assign(bytes.begin(), bytes.end());
    result.table = std::move(table);
  }
  return result;
}

void register_color_lookup_table(std::shared_ptr<const ColorLookupTable> table) {
  if (table == nullptr || table->id.empty() || !table->valid()) {
    return;
  }
  const std::lock_guard lock(color_lookup_registry_mutex());
  color_lookup_registry()[table->id] = std::move(table);
}

std::shared_ptr<const ColorLookupTable> find_color_lookup_table(std::string_view id) {
  if (id.empty()) {
    return nullptr;
  }
  const std::lock_guard lock(color_lookup_registry_mutex());
  const auto found = color_lookup_registry().find(id);
  return found != color_lookup_registry().end() ? found->second : nullptr;
}

SelectiveColorAdjustment clamp_selective_color(SelectiveColorAdjustment settings) noexcept {
  for (auto& range : settings.ranges) {
    for (int* value : {&range.cyan, &range.magenta, &range.yellow, &range.black}) {
      *value = std::clamp(*value, -100, 100);
    }
  }
  return settings;
}

bool SelectiveColorAdjustment::has_effect() const noexcept {
  const SelectiveColorCorrection none{};
  return std::any_of(ranges.begin(), ranges.end(), [&none](const auto& range) { return !(range == none); });
}

std::array<double, 3> selective_color_transfer(const SelectiveColorAdjustment& settings, double red, double green,
                                               double blue) {
  // Each range weighs the color by how strongly it belongs there: the largest channel's
  // primary (reds, greens, blues) by largest minus middle, the secondary the two largest
  // make (yellows, cyans, magentas) by middle minus smallest, whites and blacks by how far
  // the smallest (largest) channel sits above (below) mid gray, and neutrals by how close
  // both extremes are to mid gray. Calibrated against Photoshop 2026 (#250).
  const auto clamped = clamp_selective_color(settings);
  const std::array<double, 3> in{std::clamp(red, 0.0, 255.0) / 255.0, std::clamp(green, 0.0, 255.0) / 255.0,
                                 std::clamp(blue, 0.0, 255.0) / 255.0};
  const double high = std::max({in[0], in[1], in[2]});
  const double low = std::min({in[0], in[1], in[2]});
  const double middle = in[0] + in[1] + in[2] - high - low;
  std::array<double, 9> weight{};
  const auto range = [](SelectiveColorRange which) { return static_cast<std::size_t>(which); };
  if (in[0] == high) {
    weight[range(SelectiveColorRange::Reds)] = high - middle;
  }
  if (in[1] == high) {
    weight[range(SelectiveColorRange::Greens)] = high - middle;
  }
  if (in[2] == high) {
    weight[range(SelectiveColorRange::Blues)] = high - middle;
  }
  if (in[0] == low) {
    weight[range(SelectiveColorRange::Cyans)] = middle - low;
  }
  if (in[1] == low) {
    weight[range(SelectiveColorRange::Magentas)] = middle - low;
  }
  if (in[2] == low) {
    weight[range(SelectiveColorRange::Yellows)] = middle - low;
  }
  if (low > 0.5) {
    weight[range(SelectiveColorRange::Whites)] = (low - 0.5) * 2.0;
  }
  if (high < 0.5) {
    weight[range(SelectiveColorRange::Blacks)] = (0.5 - high) * 2.0;
  }
  if (!(high == 0.0 && low == 0.0) && !(high == 1.0 && low == 1.0)) {
    weight[range(SelectiveColorRange::Neutrals)] = 1.0 - (std::abs(high - 0.5) + std::abs(low - 0.5));
  }
  std::array<double, 3> out = in;
  for (std::size_t index = 0; index < clamped.ranges.size(); ++index) {
    if (weight[index] == 0.0) {
      continue;
    }
    const auto& correction = clamped.ranges[index];
    const double black = correction.black / 100.0;
    const std::array<int, 3> inks{correction.cyan, correction.magenta, correction.yellow};
    for (std::size_t c = 0; c < 3U; ++c) {
      // An ink takes its channel away; black takes all three. Relative scales by the ink
      // the channel already carries (1 - value).
      const double ink = inks[c] / 100.0;
      const double scale = clamped.absolute ? 1.0 : 1.0 - in[c];
      const double shift = std::clamp(((-1.0 - ink) * black - ink) * scale, -in[c], 1.0 - in[c]);
      out[c] += shift * weight[index];
    }
  }
  return {out[0] * 255.0, out[1] * 255.0, out[2] * 255.0};
}

BlackWhiteAdjustment clamp_black_white(BlackWhiteAdjustment settings) noexcept {
  for (int* weight : {&settings.reds, &settings.yellows, &settings.greens, &settings.cyans, &settings.blues,
                      &settings.magentas}) {
    *weight = std::clamp(*weight, -200, 300);
  }
  return settings;
}

std::array<double, 3> black_white_transfer(const BlackWhiteAdjustment& settings, double red, double green,
                                           double blue, bool byte_stages) {
  const auto clamped = clamp_black_white(settings);
  // The primary is the largest channel; the secondary is the color of the two largest. Ties
  // resolve red, then green, which the probe's grays and pure colors cannot tell apart.
  int primary = 0;
  int secondary = 0;
  if (red >= green && red >= blue) {
    primary = clamped.reds;
    secondary = green >= blue ? clamped.yellows : clamped.magentas;
  } else if (green >= blue) {
    primary = clamped.greens;
    secondary = red >= blue ? clamped.yellows : clamped.cyans;
  } else {
    primary = clamped.blues;
    secondary = green >= red ? clamped.cyans : clamped.magentas;
  }
  const double high = std::max({red, green, blue});
  const double low = std::min({red, green, blue});
  const double middle = red + green + blue - high - low;
  // Hundredths of a level; exact for 8-bit inputs, so the byte path rounds half up in integers.
  const double scaled = low * 100.0 + (middle - low) * secondary + (high - middle) * primary;
  double gray = scaled / 100.0;
  if (byte_stages) {
    const auto hundredths = std::llround(scaled) + 50;
    const auto level = hundredths >= 0 ? hundredths / 100 : -((-hundredths + 99) / 100);
    gray = std::clamp(static_cast<double>(level), 0.0, 255.0);
  }
  if (!clamped.tint) {
    return {gray, gray, gray};
  }
  return set_luma({static_cast<double>(clamped.tint_color.red), static_cast<double>(clamped.tint_color.green),
                   static_cast<double>(clamped.tint_color.blue)},
                  std::clamp(gray, 0.0, 255.0), byte_stages);
}

LayerStyleGradient GradientMapAdjustment::default_gradient_map_gradient() {
  LayerStyleGradient gradient;
  gradient.name = "Black, White";
  gradient.smoothness = 4096;
  gradient.interpolation = GradientInterpolationMethod::Classic;
  gradient.color_stops = {GradientColorStop{0.0F, RgbColor{0, 0, 0}, 0.5F},
                          GradientColorStop{1.0F, RgbColor{255, 255, 255}, 0.5F}};
  gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F}, GradientAlphaStop{1.0F, 1.0F, 0.5F}};
  return gradient;
}

namespace {

// A gradient map reads 256 luma levels, so the 8-bit path keeps one table per thread for
// the last gradient it saw; the compositor calls this per pixel.
const std::array<RgbColor, 256>& gradient_map_table(const GradientMapAdjustment& settings) {
  thread_local GradientMapAdjustment cached_settings;
  thread_local std::array<RgbColor, 256> cached_table{};
  thread_local bool cached = false;
  if (!cached || !(cached_settings == settings)) {
    for (int level = 0; level < 256; ++level) {
      auto position = static_cast<float>(level) / 255.0F;
      if (settings.gradient.reverse) {
        position = 1.0F - position;
      }
      // Photoshop eases a two-stop Classic map like a gradient fill (probe: 64 maps to 52).
      cached_table[static_cast<std::size_t>(level)] = gradient_color(settings.gradient, position, true, true);
    }
    cached_settings = settings;
    cached = true;
  }
  return cached_table;
}

}  // namespace

RgbColor gradient_map_color(const GradientMapAdjustment& settings, RgbColor color) {
  const auto luma = std::clamp(std::floor(gradient_map_luma(color.red, color.green, color.blue) + 0.5), 0.0, 255.0);
  return gradient_map_table(settings)[static_cast<std::size_t>(luma)];
}

std::array<double, 3> gradient_map_transfer(const GradientMapAdjustment& settings, double red, double green,
                                            double blue) {
  auto position = static_cast<float>(std::clamp(gradient_map_luma(red, green, blue) / 255.0, 0.0, 1.0));
  if (settings.gradient.reverse) {
    position = 1.0F - position;
  }
  return gradient_color_precise(settings.gradient, position, true, false, true);
}

ColorBalanceTone ColorBalanceAdjustment::tone(ColorBalanceToneRange range) const noexcept {
  switch (range) {
    case ColorBalanceToneRange::Shadows:
      return shadows;
    case ColorBalanceToneRange::Highlights:
      return highlights;
    case ColorBalanceToneRange::Midtones:
      break;
  }
  return ColorBalanceTone{cyan_red, magenta_green, yellow_blue};
}

void ColorBalanceAdjustment::set_tone(ColorBalanceToneRange range, ColorBalanceTone value) noexcept {
  switch (range) {
    case ColorBalanceToneRange::Shadows:
      shadows = value;
      return;
    case ColorBalanceToneRange::Highlights:
      highlights = value;
      return;
    case ColorBalanceToneRange::Midtones:
      break;
  }
  cyan_red = value.cyan_red;
  magenta_green = value.magenta_green;
  yellow_blue = value.yellow_blue;
}

// Preserve Luminosity alone changes nothing: with every slider at zero the colors are
// untouched, so their luminosity already matches.
bool ColorBalanceAdjustment::has_effect() const noexcept {
  return tone(ColorBalanceToneRange::Midtones).has_effect() || shadows.has_effect() ||
         highlights.has_effect();
}

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

// Photoshop's Posterize: `levels` equal-width input buckets (floor(value * levels / 256)),
// each mapped to its step on the 0..255 output ramp with the fraction dropped (levels 3
// gives 0, 127, 255). Byte-exact against Photoshop's render of psd-tools' posterize_rgb.psd
// at 3, 7, 13 and 21 levels (October 2026); rounding to the nearest step is not.
std::uint8_t posterize_channel_value(std::uint8_t value, int levels) {
  levels = std::clamp(levels, 2, 255);
  const int bucket = static_cast<int>(value) * levels / 256;
  return static_cast<std::uint8_t>(bucket * 255 / (levels - 1));
}

ExposureAdjustment clamp_exposure(ExposureAdjustment settings) {
  settings.exposure_hundredths = std::clamp(settings.exposure_hundredths, -kExposureValueRange, kExposureValueRange);
  settings.offset_ten_thousandths =
      std::clamp(settings.offset_ten_thousandths, -kExposureOffsetRange, kExposureOffsetRange);
  settings.gamma_hundredths = std::clamp(settings.gamma_hundredths, kExposureGammaMin, kExposureGammaMax);
  return settings;
}

std::uint8_t exposure_channel_value(std::uint8_t value, ExposureAdjustment settings) {
  settings = clamp_exposure(settings);
  constexpr double kDisplayGamma = 2.2;
  const auto linear = std::pow(static_cast<double>(value) / 255.0, kDisplayGamma);
  const auto exposed = linear * std::pow(2.0, static_cast<double>(settings.exposure_hundredths) / 100.0) +
                       static_cast<double>(settings.offset_ten_thousandths) / 10000.0;
  const auto corrected =
      std::pow(std::max(0.0, exposed), 100.0 / static_cast<double>(settings.gamma_hundredths));
  const auto encoded = std::pow(std::clamp(corrected, 0.0, 1.0), 1.0 / kDisplayGamma);
  return static_cast<std::uint8_t>(std::clamp(std::lround(encoded * 255.0), 0L, 255L));
}

int threshold_luminance(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  // Rounded half up: Photoshop 2026's Threshold adjustment layer and Smart Filter both
  // match it on every color of a 16-level cube at level 140, where the floored quotient
  // misses 13 (October 2026 probes, docs/smart-filters-adjustments.md).
  return (static_cast<int>(red) * 30 + static_cast<int>(green) * 59 + static_cast<int>(blue) * 11 + 50) / 100;
}

namespace {

// Photoshop 2026 modern-mode Brightness/Contrast, the full closed form
// recovered from 300 16-bit (15-bit precision) plus 451 8-bit COM ramp
// captures (July 2026, docs/adjustments-calibration.md "Modern Brightness/Contrast").
// Everything runs on the unit interval; each byte maps through
// lround(255 * contrast(brightness(v / 255))).
//
// Brightness b in 1..100 is a gain ray with a cubic-Hermite shoulder:
//   sigma = 2^(b/110)  (the 110th root of 2 multiplied out per step, never
//   exp2(), so every toolchain computes the identical double)
//   f(v) = sigma * v                      while the OUTPUT stays <= 0.5
//   then one Hermite from (0.5/sigma, 0.5, slope sigma) to (1, 1, slope tau)
//   with tau = max(0.1, 1 / (1 + 12 * (sigma - 1))), clamped to 1 (the
//   Hermite legitimately overshoots for b > 88, exactly where the tau floor
//   engages: 1/(1+12(sigma-1)) = 0.1 at sigma = 1.75, i.e. b = 110*log2(1.75)).
// b in 101..150 composes the two halves: f_b = f_{b-100} o f_100.
// Negative b is the exact functional inverse of f_{-b}, evaluated by fixed
// 64-step bisection (pure IEEE arithmetic, deterministic across toolchains).
// Contrast is linear in c with a parabola in each half, pivoting at (.5,.5):
//   beta = 1 - 0.0076 * c;  y = (2 - 2*beta)*v^2 + beta*v  mirrored above 0.5.
// Validation: all 450 15-bit captures within 1 LSB15; 449 of 452 8-bit curves
// byte-exact, the other three within 1/255.
constexpr double kModernBrightnessGainStep = 1.006321233202252;  // 2^(1/110)

double modern_brightness_gain(int b) {
  auto sigma = 1.0;
  for (int step = 0; step < b; ++step) {
    sigma *= kModernBrightnessGainStep;
  }
  return sigma;
}

// b in 1..100 only; the >100 composition and the negative inverse live in
// modern_brightness_value.
double modern_brightness_core(int b, double v) {
  const auto sigma = modern_brightness_gain(b);
  const auto ray_end = 0.5 / sigma;
  if (v <= ray_end) {
    return sigma * v;
  }
  const auto h = 1.0 - ray_end;
  const auto t = (v - ray_end) / h;
  const auto tau = std::max(0.1, 1.0 / (1.0 + 12.0 * (sigma - 1.0)));
  const auto hermite = ((2.0 * t - 3.0) * t * t + 1.0) * 0.5 + ((t - 2.0) * t + 1.0) * t * h * sigma +
                       (3.0 - 2.0 * t) * t * t + (t - 1.0) * t * t * h * tau;
  return std::min(1.0, hermite);
}

double modern_brightness_positive(int b, double v) {
  if (b <= 100) {
    return modern_brightness_core(b, v);
  }
  return modern_brightness_core(b - 100, modern_brightness_core(100, v));
}

double modern_brightness_value(int b, double v) {
  if (b == 0) {
    return v;
  }
  if (b > 0) {
    return modern_brightness_positive(b, v);
  }
  if (v <= 0.0) {
    return 0.0;
  }
  if (v >= 1.0) {
    return 1.0;
  }
  auto low = 0.0;
  auto high = 1.0;
  for (int step = 0; step < 64; ++step) {
    const auto mid = 0.5 * (low + high);
    if (modern_brightness_positive(-b, mid) < v) {
      low = mid;
    } else {
      high = mid;
    }
  }
  return 0.5 * (low + high);
}

double modern_contrast_value(int c, double v) {
  const auto beta = 1.0 - 0.0076 * static_cast<double>(c);
  const auto alpha = 2.0 - 2.0 * beta;
  if (v <= 0.5) {
    return (alpha * v + beta) * v;
  }
  const auto w = 1.0 - v;
  return 1.0 - (alpha * w + beta) * w;
}

}  // namespace

BrightnessContrastAdjustment clamp_brightness_contrast(BrightnessContrastAdjustment settings) {
  if (settings.use_legacy) {
    settings.brightness = std::clamp(settings.brightness, -kBrightnessContrastLegacyRange,
                                     kBrightnessContrastLegacyRange);
    settings.contrast =
        std::clamp(settings.contrast, -kBrightnessContrastLegacyRange, kBrightnessContrastLegacyRange);
  } else {
    settings.brightness = std::clamp(settings.brightness, -kModernBrightnessRange, kModernBrightnessRange);
    settings.contrast = std::clamp(settings.contrast, kModernContrastMin, kModernContrastMax);
  }
  return settings;
}

std::uint8_t brightness_contrast_channel_value(std::uint8_t value, int brightness, int contrast,
                                               bool use_legacy) {
  if (!use_legacy) {
    const auto b = std::clamp(brightness, -kModernBrightnessRange, kModernBrightnessRange);
    const auto c = std::clamp(contrast, kModernContrastMin, kModernContrastMax);
    const auto adjusted =
        modern_contrast_value(c, modern_brightness_value(b, static_cast<double>(value) / 255.0));
    return static_cast<std::uint8_t>(std::clamp(std::lround(255.0 * adjusted), 0L, 255L));
  }
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

namespace {

// Photoshop 2026's master saturation multiplier for 16-bit documents, indexed
// delta + 100: not its 8-bit table (kMasterSaturationScale). Negative percents are
// exactly (256 + trunc(2.56 * delta)) / 256, +1..+49 are whole 256ths, the rest were
// measured on chroma ramps (relative error below 0.1%; +99 is 255/4, +100 is 128).
constexpr std::array<double, 201> kDeepSaturationScale = {
    0.000000, 0.011719, 0.023438, 0.031250, 0.042969, 0.050781,
    0.062500, 0.070312, 0.082031, 0.093750, 0.101562, 0.113281,
    0.121094, 0.132812, 0.140625, 0.152344, 0.160156, 0.171875,
    0.183594, 0.191406, 0.203125, 0.210938, 0.222656, 0.230469,
    0.242188, 0.250000, 0.261719, 0.273438, 0.281250, 0.292969,
    0.300781, 0.312500, 0.320312, 0.332031, 0.343750, 0.351562,
    0.363281, 0.371094, 0.382812, 0.390625, 0.402344, 0.410156,
    0.421875, 0.433594, 0.441406, 0.453125, 0.460938, 0.472656,
    0.480469, 0.492188, 0.500000, 0.511719, 0.523438, 0.531250,
    0.542969, 0.550781, 0.562500, 0.570312, 0.582031, 0.593750,
    0.601562, 0.613281, 0.621094, 0.632812, 0.640625, 0.652344,
    0.660156, 0.671875, 0.683594, 0.691406, 0.703125, 0.710938,
    0.722656, 0.730469, 0.742188, 0.750000, 0.761719, 0.773438,
    0.781250, 0.792969, 0.800781, 0.812500, 0.820312, 0.832031,
    0.843750, 0.851562, 0.863281, 0.871094, 0.882812, 0.890625,
    0.902344, 0.910156, 0.921875, 0.933594, 0.941406, 0.953125,
    0.960938, 0.972656, 0.980469, 0.992188, 1.000000, 1.007812,
    1.019531, 1.027344, 1.039062, 1.046875, 1.062500, 1.070312,
    1.082031, 1.093750, 1.105469, 1.117188, 1.132812, 1.148438,
    1.156250, 1.171875, 1.183594, 1.199219, 1.210938, 1.230469,
    1.242188, 1.261719, 1.273438, 1.292969, 1.304688, 1.328125,
    1.347656, 1.363281, 1.382812, 1.398438, 1.421875, 1.437500,
    1.464844, 1.480469, 1.507812, 1.523438, 1.554688, 1.570312,
    1.601562, 1.632812, 1.652344, 1.687500, 1.710938, 1.746094,
    1.769531, 1.804688, 1.832031, 1.875000, 1.902344, 1.945312,
    1.992240, 2.023492, 2.070372, 2.105531, 2.160224, 2.195383,
    2.253980, 2.296957, 2.359462, 2.402432, 2.472753, 2.523539,
    2.601674, 2.683712, 2.738405, 2.832167, 2.894674, 3.000155,
    3.070476, 3.187685, 3.265814, 3.398644, 3.492405, 3.640865,
    3.750289, 3.922154, 4.109675, 4.250369, 4.473016, 4.633206,
    4.901786, 5.097000, 5.421123, 5.663295, 6.069502, 6.374434,
    6.889676, 7.284091, 7.967697, 8.791667, 9.440196, 10.623992,
    11.588521, 13.416667, 14.998611, 18.209514, 21.249023, 28.330537,
    36.424342, 63.750000, 128.000000,
};

double wheel_position_continuous(const std::array<double, 3>& color) {
  const auto red = color[0];
  const auto green = color[1];
  const auto blue = color[2];
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto span = maximum - minimum;
  if (span <= 0.0) {
    return 0.0;
  }
  const auto ramp = [span](double middle, double low) { return 255.0 * (middle - low) / span; };
  if (red == maximum && blue == minimum) {
    return ramp(green, blue);
  }
  if (green == maximum && blue == minimum) {
    return 510.0 - ramp(red, blue);
  }
  if (green == maximum && red == minimum) {
    return 510.0 + ramp(blue, red);
  }
  if (blue == maximum && red == minimum) {
    return 1020.0 - ramp(green, red);
  }
  if (blue == maximum && green == minimum) {
    return 1020.0 + ramp(red, green);
  }
  return 1530.0 - ramp(blue, green);
}

std::array<double, 3> reconstruct_continuous(double light, double half_chroma, int sector, double interpolant,
                                             bool unbounded) {
  auto q = light + half_chroma;
  auto p = light - half_chroma;
  if (!unbounded) {
    q = std::min(255.0, q);
    p = std::max(0.0, p);
  }
  const auto mid = p + (q - p) * interpolant / 255.0;
  switch (sector) {
    case 0:
      return {q, mid, p};
    case 1:
      return {mid, q, p};
    case 2:
      return {p, q, mid};
    case 3:
      return {p, mid, q};
    case 4:
      return {mid, p, q};
    default:
      return {q, p, mid};
  }
}

}  // namespace

std::optional<std::array<double, 3>> hue_saturation_transfer(std::array<double, 3> color,
                                                             const HueSaturationAdjustment& settings,
                                                             bool unbounded) {
  if (settings.colorize) {
    return std::nullopt;
  }
  const auto hue_shift = std::clamp(settings.hue_shift, -180, 180);
  const auto saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  const auto lightness_delta = std::clamp(settings.lightness_delta, -100, 100);
  std::array<double, 6> band_weights{};
  if (settings.any_band_has_effect()) {
    const auto wheel = wheel_position_continuous(color);
    int sector = 0;
    double interpolant = 0.0;
    photoshop_wheel_split(wheel, sector, interpolant);
    auto maximum = std::max({color[0], color[1], color[2]});
    auto minimum = std::min({color[0], color[1], color[2]});
    double lightness = 0.0;
    for (std::size_t index = 0; index < settings.bands.size(); ++index) {
      const auto& band = settings.bands[index];
      if (!band.has_effect()) {
        continue;
      }
      band_weights[index] = hue_saturation_band_weight(wheel / 4.25, band);
      lightness += band_weights[index] * std::clamp(band.lightness_delta, -100, 100);
    }
    if (lightness > 0.0) {
      minimum += (maximum - minimum) * std::min(lightness, 100.0) / 100.0;
    } else if (lightness < 0.0) {
      maximum += (minimum - maximum) * std::min(-lightness, 100.0) / 100.0;
    }
    color = reconstruct_continuous((maximum + minimum) * 0.5, (maximum - minimum) * 0.5, sector, interpolant,
                                   unbounded);
  }
  // The lightness slider per channel; at depth the percent is exact (8 bits quantize
  // it to whole bytes).
  const auto fraction = static_cast<double>(std::abs(lightness_delta)) / 100.0;
  std::array<double, 3> lit{};
  for (std::size_t c = 0; c < 3U; ++c) {
    const auto value = color[c];
    lit[c] = lightness_delta > 0   ? value + (255.0 - value) * fraction
             : lightness_delta < 0 ? value * (1.0 - fraction)
                                   : value;
  }
  const auto maximum = std::max({lit[0], lit[1], lit[2]});
  const auto minimum = std::min({lit[0], lit[1], lit[2]});
  if (maximum == minimum) {
    return lit;
  }
  const auto light = (maximum + minimum) * 0.5;
  const auto half = (maximum - minimum) * 0.5;
  auto band_saturation_offset = 0.0;
  auto rotation = static_cast<double>(hue_shift) * 4.25;
  for (std::size_t index = 0; index < settings.bands.size(); ++index) {
    const auto weight = band_weights[index];
    if (weight <= 0.0) {
      continue;
    }
    const auto& band = settings.bands[index];
    const auto band_saturation = std::clamp(band.saturation_delta, -100, 100);
    if (band_saturation != 0) {
      band_saturation_offset +=
          weight * (kDeepSaturationScale[static_cast<std::size_t>(band_saturation + 100)] - 1.0);
    }
    const auto band_hue = std::clamp(band.hue_shift, -180, 180);
    if (band_hue != 0) {
      rotation += weight * static_cast<double>(band_hue) * 4.25;
    }
  }
  const auto ratio = kDeepSaturationScale[static_cast<std::size_t>(saturation_delta + 100)] *
                     std::max(0.0, 1.0 + band_saturation_offset);
  auto half_chroma = half * ratio;
  if (!unbounded) {
    half_chroma = std::min(half_chroma, std::max(std::min(light, 255.0 - light), half));
  }
  int sector = 0;
  double interpolant = 0.0;
  photoshop_wheel_split(wheel_position_continuous(lit) + rotation, sector, interpolant);
  return reconstruct_continuous(light, half_chroma, sector, interpolant, unbounded);
}

double levels_gamma_curve(double normalized, double gamma) {
  return photoshop_levels_gamma(normalized, gamma);
}

// Per channel c, with the three tone triples S (shadows), M (midtones), H (highlights):
//   without Preserve Luminosity: input black max(0, -S[c]), input white 255 - max(0, H[c]),
//     gamma exponent M[c] + S[c]/2 + H[c]/2 (each half truncated toward zero);
//   with it: input black max(S) - S[c], input white 255 - (H[c] - min(H)),
//     gamma exponent M[c] - (max(M) + min(M))/2; shadows and highlights add no gamma.
std::array<ColorBalanceChannelLevels, 3> color_balance_channel_levels(const ColorBalanceAdjustment& settings) {
  std::array<std::array<int, 3>, 3> tones{};
  const std::array<ColorBalanceTone, 3> source{settings.shadows, settings.tone(ColorBalanceToneRange::Midtones),
                                               settings.highlights};
  for (std::size_t range = 0; range < 3U; ++range) {
    tones[range] = {std::clamp(source[range].cyan_red, -100, 100),
                    std::clamp(source[range].magenta_green, -100, 100),
                    std::clamp(source[range].yellow_blue, -100, 100)};
  }
  const auto& shadows = tones[0];
  const auto& midtones = tones[1];
  const auto& highlights = tones[2];
  std::array<ColorBalanceChannelLevels, 3> levels{};
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    auto& record = levels[channel];
    if (settings.preserve_luminosity) {
      const auto [midtones_min, midtones_max] = std::minmax_element(midtones.begin(), midtones.end());
      record.black_input = *std::max_element(shadows.begin(), shadows.end()) - shadows[channel];
      record.white_input = 255 - (highlights[channel] - *std::min_element(highlights.begin(), highlights.end()));
      record.gamma_exponent = midtones[channel] - (*midtones_max + *midtones_min) / 2;
    } else {
      record.black_input = std::max(0, -shadows[channel]);
      record.white_input = 255 - std::max(0, highlights[channel]);
      record.gamma_exponent = midtones[channel] + shadows[channel] / 2 + highlights[channel] / 2;
    }
    // Opposing extremes under Preserve Luminosity can cross the points; keep a valid range.
    record.white_input = std::max(record.white_input, record.black_input + 1);
  }
  return levels;
}

double color_balance_transfer(double value, ColorBalanceChannelLevels levels, bool byte_points) {
  auto normalized = std::clamp((value - static_cast<double>(levels.black_input)) /
                                   static_cast<double>(levels.white_input - levels.black_input),
                               0.0, 1.0);
  if (byte_points && (levels.black_input != 0 || levels.white_input != 255)) {
    normalized = std::floor(normalized * 255.0 + 0.5) / 255.0;
  }
  if (levels.gamma_exponent != 0) {
    const auto gamma =
        static_cast<double>(std::lround(100.0 * std::exp2(static_cast<double>(levels.gamma_exponent) / 100.0))) /
        100.0;
    normalized = photoshop_levels_gamma(normalized, gamma);
  }
  return normalized * 255.0;
}

double levels_record_transfer(double value, LevelsRecord record) {
  return levels_value(value, record);
}

double brightness_contrast_transfer(double value, int brightness, int contrast, bool use_legacy) {
  if (!use_legacy) {
    const auto b = std::clamp(brightness, -kModernBrightnessRange, kModernBrightnessRange);
    const auto c = std::clamp(contrast, kModernContrastMin, kModernContrastMax);
    return 255.0 * modern_contrast_value(c, modern_brightness_value(b, std::clamp(value / 255.0, 0.0, 1.0)));
  }
  const auto b = static_cast<double>(std::clamp(brightness, -100, 100));
  const auto c = std::clamp(contrast, -100, 100);
  if (c == 0) {
    return std::clamp(value + b, 0.0, 255.0);
  }
  if (c >= 100) {
    return value + b >= 127.0 ? 255.0 : 0.0;
  }
  if (c > 0) {
    return std::clamp((value + b - 127.5) * 100.0 / (100.0 - c) + 127.5, 0.0, 255.0);
  }
  return std::clamp((value - 127.5) * (100.0 + c) / 100.0 + 127.5 + b, 0.0, 255.0);
}

double exposure_transfer(double value, ExposureAdjustment settings) {
  settings = clamp_exposure(settings);
  constexpr double kDisplayGamma = 2.2;
  const auto linear = std::pow(std::clamp(value / 255.0, 0.0, 1.0), kDisplayGamma);
  const auto exposed = linear * std::pow(2.0, static_cast<double>(settings.exposure_hundredths) / 100.0) +
                       static_cast<double>(settings.offset_ten_thousandths) / 10000.0;
  const auto corrected =
      std::pow(std::max(0.0, exposed), 100.0 / static_cast<double>(settings.gamma_hundredths));
  return 255.0 * std::pow(std::clamp(corrected, 0.0, 1.0), 1.0 / kDisplayGamma);
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
    case AdjustmentKind::Exposure:
      return "exposure";
    case AdjustmentKind::ChannelMixer:
      return "channel_mixer";
    case AdjustmentKind::PhotoFilter:
      return "photo_filter";
    case AdjustmentKind::GradientMap:
      return "gradient_map";
    case AdjustmentKind::BlackWhite:
      return "black_white";
    case AdjustmentKind::ColorLookup:
      return "color_lookup";
    case AdjustmentKind::SelectiveColor:
      return "selective_color";
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
    case AdjustmentKind::Exposure:
      return "Exposure";
    case AdjustmentKind::ChannelMixer:
      return "Channel Mixer";
    case AdjustmentKind::PhotoFilter:
      return "Photo Filter";
    case AdjustmentKind::GradientMap:
      return "Gradient Map";
    case AdjustmentKind::BlackWhite:
      return "Black & White";
    case AdjustmentKind::ColorLookup:
      return "Color Lookup";
    case AdjustmentKind::SelectiveColor:
      return "Selective Color";
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
  if (key == "exposure") {
    return AdjustmentKind::Exposure;
  }
  if (key == "channel_mixer") {
    return AdjustmentKind::ChannelMixer;
  }
  if (key == "photo_filter") {
    return AdjustmentKind::PhotoFilter;
  }
  if (key == "gradient_map") {
    return AdjustmentKind::GradientMap;
  }
  if (key == "black_white") {
    return AdjustmentKind::BlackWhite;
  }
  if (key == "color_lookup") {
    return AdjustmentKind::ColorLookup;
  }
  if (key == "selective_color") {
    return AdjustmentKind::SelectiveColor;
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
  settings.levels.black_ink =
      metadata_levels_record_or(layer, kLayerMetadataAdjustmentLevelsBlackInkBlackInput,
                                kLayerMetadataAdjustmentLevelsBlackInkWhiteInput,
                                kLayerMetadataAdjustmentLevelsBlackInkGammaPercent,
                                kLayerMetadataAdjustmentLevelsBlackInkBlackOutput,
                                kLayerMetadataAdjustmentLevelsBlackInkWhiteOutput);
  settings.ink_space = find_ink_space(metadata_string_or(layer, kLayerMetadataAdjustmentInkSpace, {}));
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
  settings.color_balance.shadows =
      parse_color_balance_tone(metadata_string_or(layer, kLayerMetadataAdjustmentColorBalanceShadows, {}));
  settings.color_balance.highlights =
      parse_color_balance_tone(metadata_string_or(layer, kLayerMetadataAdjustmentColorBalanceHighlights, {}));
  // Absent in documents from before tone ranges, which rendered without it.
  settings.color_balance.preserve_luminosity =
      metadata_int_or(layer, kLayerMetadataAdjustmentColorBalancePreserveLuminosity, 0) != 0;
  {
    const ChannelMixerAdjustment defaults;
    auto& mixer = settings.channel_mixer;
    mixer.monochrome = metadata_int_or(layer, kLayerMetadataAdjustmentChannelMixerMonochrome, 0) != 0;
    mixer.red = parse_channel_mixer_row(metadata_string_or(layer, kLayerMetadataAdjustmentChannelMixerRed, {}),
                                        defaults.red);
    mixer.green = parse_channel_mixer_row(metadata_string_or(layer, kLayerMetadataAdjustmentChannelMixerGreen, {}),
                                          defaults.green);
    mixer.blue = parse_channel_mixer_row(metadata_string_or(layer, kLayerMetadataAdjustmentChannelMixerBlue, {}),
                                         defaults.blue);
    mixer.gray = parse_channel_mixer_row(metadata_string_or(layer, kLayerMetadataAdjustmentChannelMixerGray, {}),
                                         defaults.gray);
  }
  {
    const PhotoFilterAdjustment defaults;
    auto& filter = settings.photo_filter;
    filter = parse_photo_filter_lab(metadata_string_or(layer, kLayerMetadataAdjustmentPhotoFilterLab, {}), defaults);
    filter.density = std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentPhotoFilterDensity, defaults.density),
                                1, 100);
    filter.preserve_luminosity =
        metadata_int_or(layer, kLayerMetadataAdjustmentPhotoFilterPreserveLuminosity, 1) != 0;
  }
  if (auto gradient = parse_gradient_map(metadata_string_or(layer, kLayerMetadataAdjustmentGradientMapGradient, {}));
      gradient.has_value()) {
    gradient->name = std::string(metadata_string_or(layer, kLayerMetadataAdjustmentGradientMapName, gradient->name));
    settings.gradient_map.gradient = std::move(*gradient);
  }
  if (const auto weights =
          parse_int_list(metadata_string_or(layer, kLayerMetadataAdjustmentBlackWhiteWeights, {}), 6);
      weights.has_value()) {
    auto& bw = settings.black_white;
    bw.reds = (*weights)[0];
    bw.yellows = (*weights)[1];
    bw.greens = (*weights)[2];
    bw.cyans = (*weights)[3];
    bw.blues = (*weights)[4];
    bw.magentas = (*weights)[5];
    bw = clamp_black_white(bw);
  }
  {
    // "relative|c,m,y,k;..." with nine corrections; anything malformed keeps the defaults.
    const auto encoded = metadata_string_or(layer, kLayerMetadataAdjustmentSelectiveColor, {});
    const auto bar = encoded.find('|');
    if (bar != std::string_view::npos) {
      SelectiveColorAdjustment parsed;
      parsed.absolute = encoded.substr(0, bar) == "absolute";
      // Nine "c,m,y,k" groups; read as 36 comma-separated numbers.
      std::string flat(encoded.substr(bar + 1U));
      std::replace(flat.begin(), flat.end(), ';', ',');
      std::vector<int> numbers;
      bool ok = true;
      std::string_view rest(flat);
      while (!rest.empty() && ok) {
        const auto comma = rest.find(',');
        const auto value = parse_int(rest.substr(0, comma));
        ok = value.has_value();
        if (ok) {
          numbers.push_back(*value);
        }
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1U);
      }
      if (ok && numbers.size() == 36U) {
        for (std::size_t index = 0; index < 9U; ++index) {
          parsed.ranges[index] = SelectiveColorCorrection{numbers[index * 4U], numbers[index * 4U + 1U],
                                                          numbers[index * 4U + 2U], numbers[index * 4U + 3U]};
        }
        settings.selective_color = clamp_selective_color(parsed);
      }
    }
  }
  settings.color_lookup.table =
      find_color_lookup_table(metadata_string_or(layer, kLayerMetadataAdjustmentColorLookupTable, {}));
  settings.color_lookup.name = std::string(metadata_string_or(layer, kLayerMetadataAdjustmentColorLookupName, {}));
  settings.black_white.tint = metadata_int_or(layer, kLayerMetadataAdjustmentBlackWhiteTint, 0) != 0;
  if (const auto tint = parse_int_list(metadata_string_or(layer, kLayerMetadataAdjustmentBlackWhiteTintColor, {}), 3);
      tint.has_value()) {
    settings.black_white.tint_color = RgbColor{static_cast<std::uint8_t>(std::clamp((*tint)[0], 0, 255)),
                                               static_cast<std::uint8_t>(std::clamp((*tint)[1], 0, 255)),
                                               static_cast<std::uint8_t>(std::clamp((*tint)[2], 0, 255))};
  }
  settings.posterize.levels =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentPosterizeLevels, 4), 2, 255);
  settings.threshold.level =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentThresholdLevel, 128), 1, 255);
  settings.exposure = clamp_exposure(ExposureAdjustment{
      metadata_int_or(layer, kLayerMetadataAdjustmentExposureValue, 0),
      metadata_int_or(layer, kLayerMetadataAdjustmentExposureOffset, 0),
      metadata_int_or(layer, kLayerMetadataAdjustmentExposureGamma, 100)});
  // Default legacy when the key is absent: pre-July-2026 documents were always
  // legacy-mode and must keep their render.
  settings.brightness_contrast.use_legacy =
      metadata_int_or(layer, kLayerMetadataAdjustmentBrightnessContrastUseLegacy, 1) != 0;
  const auto brightness_range =
      settings.brightness_contrast.use_legacy ? kBrightnessContrastLegacyRange : kModernBrightnessRange;
  const auto contrast_low =
      settings.brightness_contrast.use_legacy ? -kBrightnessContrastLegacyRange : kModernContrastMin;
  const auto contrast_high =
      settings.brightness_contrast.use_legacy ? kBrightnessContrastLegacyRange : kModernContrastMax;
  settings.brightness_contrast.brightness =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentBrightnessContrastBrightness, 0),
                 -brightness_range, brightness_range);
  settings.brightness_contrast.contrast =
      std::clamp(metadata_int_or(layer, kLayerMetadataAdjustmentBrightnessContrastContrast, 0), contrast_low,
                 contrast_high);
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
  // The ink space and the black ink's record are written only for an adjustment that
  // has them, so layers from RGB documents keep exactly the metadata they always had.
  if (settings.ink_space != nullptr) {
    set_metadata_string(layer, kLayerMetadataAdjustmentInkSpace, settings.ink_space->id);
    set_metadata_levels_record(layer, settings.levels.black_ink, kLayerMetadataAdjustmentLevelsBlackInkBlackInput,
                               kLayerMetadataAdjustmentLevelsBlackInkWhiteInput,
                               kLayerMetadataAdjustmentLevelsBlackInkGammaPercent,
                               kLayerMetadataAdjustmentLevelsBlackInkBlackOutput,
                               kLayerMetadataAdjustmentLevelsBlackInkWhiteOutput);
  }
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
    if (settings.ink_space != nullptr) {
      metadata[kLayerMetadataAdjustmentCurvesBlackPoints] =
          serialize_curve_control_points(settings.curves.black_ink);
    }
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
  set_metadata_string(layer, kLayerMetadataAdjustmentColorBalanceShadows,
                      serialize_color_balance_tone(settings.color_balance.shadows));
  set_metadata_string(layer, kLayerMetadataAdjustmentColorBalanceHighlights,
                      serialize_color_balance_tone(settings.color_balance.highlights));
  set_metadata_int(layer, kLayerMetadataAdjustmentColorBalancePreserveLuminosity,
                   settings.color_balance.preserve_luminosity ? 1 : 0);
  set_metadata_int(layer, kLayerMetadataAdjustmentChannelMixerMonochrome, settings.channel_mixer.monochrome ? 1 : 0);
  set_metadata_string(layer, kLayerMetadataAdjustmentChannelMixerRed,
                      serialize_channel_mixer_row(settings.channel_mixer.red));
  set_metadata_string(layer, kLayerMetadataAdjustmentChannelMixerGreen,
                      serialize_channel_mixer_row(settings.channel_mixer.green));
  set_metadata_string(layer, kLayerMetadataAdjustmentChannelMixerBlue,
                      serialize_channel_mixer_row(settings.channel_mixer.blue));
  set_metadata_string(layer, kLayerMetadataAdjustmentChannelMixerGray,
                      serialize_channel_mixer_row(settings.channel_mixer.gray));
  set_metadata_string(layer, kLayerMetadataAdjustmentPhotoFilterLab, serialize_photo_filter_lab(settings.photo_filter));
  set_metadata_int(layer, kLayerMetadataAdjustmentPhotoFilterDensity,
                   clamp_photo_filter(settings.photo_filter).density);
  set_metadata_int(layer, kLayerMetadataAdjustmentPhotoFilterPreserveLuminosity,
                   settings.photo_filter.preserve_luminosity ? 1 : 0);
  set_metadata_string(layer, kLayerMetadataAdjustmentGradientMapGradient,
                      serialize_gradient_map(settings.gradient_map.gradient));
  set_metadata_string(layer, kLayerMetadataAdjustmentGradientMapName, settings.gradient_map.gradient.name);
  {
    const auto bw = clamp_black_white(settings.black_white);
    set_metadata_string(layer, kLayerMetadataAdjustmentBlackWhiteWeights,
                        std::to_string(bw.reds) + ';' + std::to_string(bw.yellows) + ';' + std::to_string(bw.greens) +
                            ';' + std::to_string(bw.cyans) + ';' + std::to_string(bw.blues) + ';' +
                            std::to_string(bw.magentas));
    set_metadata_int(layer, kLayerMetadataAdjustmentBlackWhiteTint, bw.tint ? 1 : 0);
    if (settings.color_lookup.table != nullptr) {
      register_color_lookup_table(settings.color_lookup.table);
    }
    set_metadata_string(layer, kLayerMetadataAdjustmentColorLookupTable,
                        settings.color_lookup.table != nullptr ? settings.color_lookup.table->id : std::string());
    set_metadata_string(layer, kLayerMetadataAdjustmentColorLookupName, settings.color_lookup.name);
    {
      const auto selective = clamp_selective_color(settings.selective_color);
      std::string encoded = selective.absolute ? "absolute|" : "relative|";
      for (std::size_t index = 0; index < selective.ranges.size(); ++index) {
        const auto& range = selective.ranges[index];
        encoded += (index == 0 ? "" : ";") + std::to_string(range.cyan) + ',' + std::to_string(range.magenta) + ',' +
                   std::to_string(range.yellow) + ',' + std::to_string(range.black);
      }
      set_metadata_string(layer, kLayerMetadataAdjustmentSelectiveColor, encoded);
    }
    set_metadata_string(layer, kLayerMetadataAdjustmentBlackWhiteTintColor,
                        std::to_string(bw.tint_color.red) + ';' + std::to_string(bw.tint_color.green) + ';' +
                            std::to_string(bw.tint_color.blue));
  }
  set_metadata_int(layer, kLayerMetadataAdjustmentPosterizeLevels,
                   std::clamp(settings.posterize.levels, 2, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentThresholdLevel,
                   std::clamp(settings.threshold.level, 1, 255));
  const auto exposure = clamp_exposure(settings.exposure);
  set_metadata_int(layer, kLayerMetadataAdjustmentExposureValue, exposure.exposure_hundredths);
  set_metadata_int(layer, kLayerMetadataAdjustmentExposureOffset, exposure.offset_ten_thousandths);
  set_metadata_int(layer, kLayerMetadataAdjustmentExposureGamma, exposure.gamma_hundredths);
  const auto bc_brightness_range =
      settings.brightness_contrast.use_legacy ? kBrightnessContrastLegacyRange : kModernBrightnessRange;
  const auto bc_contrast_low =
      settings.brightness_contrast.use_legacy ? -kBrightnessContrastLegacyRange : kModernContrastMin;
  const auto bc_contrast_high =
      settings.brightness_contrast.use_legacy ? kBrightnessContrastLegacyRange : kModernContrastMax;
  set_metadata_int(layer, kLayerMetadataAdjustmentBrightnessContrastBrightness,
                   std::clamp(settings.brightness_contrast.brightness, -bc_brightness_range,
                              bc_brightness_range));
  set_metadata_int(layer, kLayerMetadataAdjustmentBrightnessContrastContrast,
                   std::clamp(settings.brightness_contrast.contrast, bc_contrast_low, bc_contrast_high));
  set_metadata_int(layer, kLayerMetadataAdjustmentBrightnessContrastUseLegacy,
                   settings.brightness_contrast.use_legacy ? 1 : 0);
}

namespace {

// One 256-entry transfer per ink for an adjustment that runs in its ink space, built
// from the same per-channel math the RGB path uses. Cyan, magenta and yellow read the
// records RGB documents call red, green and blue; the black ink has its own.
struct InkAdjustmentTables {
  AdjustmentSettings settings;
  std::array<std::array<std::uint8_t, 256>, 4> ink{};
  bool valid{false};
};

bool same_ink_adjustment(const AdjustmentSettings& a, const AdjustmentSettings& b) {
  return a.kind == b.kind && a.ink_space == b.ink_space && a.levels == b.levels && a.curves == b.curves &&
         a.posterize.levels == b.posterize.levels && a.threshold.level == b.threshold.level &&
         a.brightness_contrast.brightness == b.brightness_contrast.brightness &&
         a.brightness_contrast.contrast == b.brightness_contrast.contrast &&
         a.brightness_contrast.use_legacy == b.brightness_contrast.use_legacy &&
         a.exposure.exposure_hundredths == b.exposure.exposure_hundredths &&
         a.exposure.offset_ten_thousandths == b.exposure.offset_ten_thousandths &&
         a.exposure.gamma_hundredths == b.exposure.gamma_hundredths;
}

RgbColor apply_adjustment_on_rgb(RgbColor color, const AdjustmentSettings& settings);

void build_ink_adjustment_tables(InkAdjustmentTables& tables, const AdjustmentSettings& settings) {
  tables.settings = settings;
  auto rgb_settings = settings;
  rgb_settings.ink_space = nullptr;
  // The black ink goes through the same stages as the others, with its own record: run
  // it as the "red" channel of a second pass.
  auto black_settings = rgb_settings;
  black_settings.levels.red = settings.levels.black_ink;
  black_settings.curves.red = settings.curves.black_ink;
  for (int value = 0; value < 256; ++value) {
    const auto probe = static_cast<std::uint8_t>(value);
    const auto adjusted = apply_adjustment_on_rgb(RgbColor{probe, probe, probe}, rgb_settings);
    const auto black = apply_adjustment_on_rgb(RgbColor{probe, probe, probe}, black_settings);
    tables.ink[0][static_cast<std::size_t>(value)] = adjusted.red;
    tables.ink[1][static_cast<std::size_t>(value)] = adjusted.green;
    tables.ink[2][static_cast<std::size_t>(value)] = adjusted.blue;
    tables.ink[3][static_cast<std::size_t>(value)] = black.red;
  }
  tables.valid = true;
}

RgbColor apply_adjustment_in_ink_space(RgbColor color, const AdjustmentSettings& settings) {
  // Per render thread, like apply_curves: the single-color hook is called per pixel.
  thread_local InkAdjustmentTables tables;
  if (!tables.valid || !same_ink_adjustment(tables.settings, settings)) {
    build_ink_adjustment_tables(tables, settings);
  }
  if (settings.ink_space->is_gray()) {
    // One channel: its record is the one RGB documents call red (see the PSD reader).
    return settings.ink_space->rgb_from_gray(tables.ink[0][settings.ink_space->gray_from_rgb(color)]);
  }
  auto ink = settings.ink_space->ink_from_rgb(color);
  for (std::size_t channel = 0; channel < ink.size(); ++channel) {
    ink[channel] = tables.ink[channel][ink[channel]];
  }
  return settings.ink_space->rgb_from_ink(ink);
}

}  // namespace

bool adjustment_runs_in_ink_space(const AdjustmentSettings& settings) noexcept {
  if (settings.ink_space == nullptr) {
    return false;
  }
  switch (settings.kind) {
    case AdjustmentKind::Levels:
    case AdjustmentKind::Curves:
    case AdjustmentKind::Invert:
    case AdjustmentKind::Posterize:
    case AdjustmentKind::BrightnessContrast:
    case AdjustmentKind::Exposure:
      return true;
    // Threshold compares one value: on a single gray channel that is channel-wise.
    case AdjustmentKind::Threshold:
      return settings.ink_space->is_gray();
    // Hue/Saturation and Color Balance mix channels (as does Threshold on four inks);
    // Photoshop's CMYK forms of them are not modeled, so they stay on the RGB math.
    case AdjustmentKind::HueSaturation:
    case AdjustmentKind::ColorBalance:
    case AdjustmentKind::ChannelMixer:
    case AdjustmentKind::PhotoFilter:
    case AdjustmentKind::GradientMap:
    case AdjustmentKind::BlackWhite:
    case AdjustmentKind::ColorLookup:
    case AdjustmentKind::SelectiveColor:
      return false;
  }
  return false;
}

RgbColor apply_adjustment_to_color(RgbColor color, const AdjustmentSettings& settings) {
  if (adjustment_runs_in_ink_space(settings)) {
    return apply_adjustment_in_ink_space(color, settings);
  }
  return apply_adjustment_on_rgb(color, settings);
}

namespace {

RgbColor apply_adjustment_on_rgb(RgbColor color, const AdjustmentSettings& settings) {
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
      const auto use_legacy = settings.brightness_contrast.use_legacy;
      return RgbColor{brightness_contrast_channel_value(color.red, brightness, contrast, use_legacy),
                      brightness_contrast_channel_value(color.green, brightness, contrast, use_legacy),
                      brightness_contrast_channel_value(color.blue, brightness, contrast, use_legacy)};
    }
    case AdjustmentKind::Exposure:
      return RgbColor{exposure_channel_value(color.red, settings.exposure),
                      exposure_channel_value(color.green, settings.exposure),
                      exposure_channel_value(color.blue, settings.exposure)};
    case AdjustmentKind::ChannelMixer: {
      const auto& mixer = settings.channel_mixer;
      const auto channel = [&color](const ChannelMixerRow& row) {
        const auto mixed = channel_mixer_transfer(row, color.red, color.green, color.blue);
        return static_cast<std::uint8_t>(std::clamp(std::floor(mixed + 0.5), 0.0, 255.0));
      };
      if (mixer.monochrome) {
        const auto gray = channel(mixer.gray);
        return RgbColor{gray, gray, gray};
      }
      return RgbColor{channel(mixer.red), channel(mixer.green), channel(mixer.blue)};
    }
    case AdjustmentKind::PhotoFilter: {
      const auto out = photo_filter_transfer(settings.photo_filter, color.red, color.green, color.blue, true);
      const auto byte = [](double value) {
        return static_cast<std::uint8_t>(std::clamp(std::floor(value + 0.5), 0.0, 255.0));
      };
      return RgbColor{byte(out[0]), byte(out[1]), byte(out[2])};
    }
    case AdjustmentKind::GradientMap:
      return gradient_map_color(settings.gradient_map, color);
    case AdjustmentKind::BlackWhite: {
      const auto out = black_white_transfer(settings.black_white, color.red, color.green, color.blue, true);
      const auto byte = [](double value) {
        return static_cast<std::uint8_t>(std::clamp(std::floor(value + 0.5), 0.0, 255.0));
      };
      return RgbColor{byte(out[0]), byte(out[1]), byte(out[2])};
    }
    case AdjustmentKind::SelectiveColor: {
      const auto out = selective_color_transfer(settings.selective_color, color.red, color.green, color.blue);
      const auto byte = [](double value) {
        return static_cast<std::uint8_t>(std::clamp(std::floor(value + 0.5), 0.0, 255.0));
      };
      return RgbColor{byte(out[0]), byte(out[1]), byte(out[2])};
    }
    case AdjustmentKind::ColorLookup: {
      if (settings.color_lookup.table == nullptr) {
        return color;
      }
      const auto out = settings.color_lookup.table->apply(color.red, color.green, color.blue);
      const auto byte = [](double value) {
        return static_cast<std::uint8_t>(std::clamp(std::floor(value + 0.5), 0.0, 255.0));
      };
      return RgbColor{byte(out[0]), byte(out[1]), byte(out[2])};
    }
  }
  return color;
}

}  // namespace

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
  // An ink-space adjustment is channel-wise on the inks, not on RGB: no RGB table exists.
  if (adjustment_runs_in_ink_space(settings)) {
    return std::nullopt;
  }
  // Hue/Saturation mixes channels through HSL; Threshold compares the mixed
  // RGB luminance, so a per-channel gray-probe LUT would be wrong for any
  // colored pixel. Both take the per-pixel path.
  if (settings.kind == AdjustmentKind::HueSaturation || settings.kind == AdjustmentKind::Threshold ||
      settings.kind == AdjustmentKind::ChannelMixer || settings.kind == AdjustmentKind::PhotoFilter ||
      settings.kind == AdjustmentKind::GradientMap || settings.kind == AdjustmentKind::BlackWhite ||
      settings.kind == AdjustmentKind::ColorLookup || settings.kind == AdjustmentKind::SelectiveColor) {
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
             levels_record_has_effect(settings.levels.blue) ||
             (settings.ink_space != nullptr && levels_record_has_effect(settings.levels.black_ink));
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
      return settings.color_balance.has_effect();
    case AdjustmentKind::Invert:
      return true;  // parameterless; the layer always inverts
    case AdjustmentKind::Posterize:
    case AdjustmentKind::Threshold:
      // Any legal level alters typical content; the conservative "always
      // effective" posture keeps live previews from being dropped.
      return true;
    case AdjustmentKind::BrightnessContrast:
      return settings.brightness_contrast.brightness != 0 || settings.brightness_contrast.contrast != 0;
    case AdjustmentKind::Exposure: {
      const auto exposure = clamp_exposure(settings.exposure);
      return exposure.exposure_hundredths != 0 || exposure.offset_ten_thousandths != 0 ||
             exposure.gamma_hundredths != 100;
    }
    case AdjustmentKind::ChannelMixer:
      return settings.channel_mixer.has_effect();
    case AdjustmentKind::PhotoFilter:
      return true;  // the minimum density is 1 %, which still tints
    case AdjustmentKind::GradientMap:
      return true;  // even black to white turns color into gray
    case AdjustmentKind::BlackWhite:
      return true;  // every setting turns color into gray
    case AdjustmentKind::ColorLookup:
      return settings.color_lookup.table != nullptr;
    case AdjustmentKind::SelectiveColor:
      return settings.selective_color.has_effect();
  }
  return false;
}

}  // namespace patchy
