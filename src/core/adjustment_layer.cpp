#include "core/adjustment_layer.hpp"

#include "core/blend_math.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace photoslop {

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

void set_metadata_int(Layer& layer, const char* key, int value) {
  layer.metadata()[key] = std::to_string(value);
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

std::uint8_t levels_channel(std::uint8_t value, LevelsAdjustment settings) {
  settings.black_input = std::clamp(settings.black_input, 0, 254);
  settings.white_input = std::clamp(settings.white_input, settings.black_input + 1, 255);
  settings.gamma_percent = std::clamp(settings.gamma_percent, 10, 999);
  const auto input_range = static_cast<double>(settings.white_input - settings.black_input);
  const auto gamma = static_cast<double>(settings.gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;
  const auto normalized =
      std::clamp((static_cast<double>(value) - static_cast<double>(settings.black_input)) / input_range, 0.0, 1.0);
  return clamp_byte(static_cast<float>(std::pow(normalized, inverse_gamma) * 255.0));
}

std::uint8_t curves_channel(std::uint8_t value, CurvesAdjustment settings) {
  settings.shadow_output = std::clamp(settings.shadow_output, 0, 255);
  settings.midtone_output = std::clamp(settings.midtone_output, 0, 255);
  settings.highlight_output = std::clamp(settings.highlight_output, 0, 255);
  const auto input = static_cast<double>(value);
  double output = 0.0;
  if (input <= 128.0) {
    const auto t = input / 128.0;
    output = static_cast<double>(settings.shadow_output) +
             (static_cast<double>(settings.midtone_output) - static_cast<double>(settings.shadow_output)) * t;
  } else {
    const auto t = (input - 128.0) / 127.0;
    output = static_cast<double>(settings.midtone_output) +
             (static_cast<double>(settings.highlight_output) - static_cast<double>(settings.midtone_output)) * t;
  }
  return clamp_byte(static_cast<float>(output));
}

RgbColor apply_levels(RgbColor color, LevelsAdjustment settings) {
  return RgbColor{levels_channel(color.red, settings), levels_channel(color.green, settings),
                  levels_channel(color.blue, settings)};
}

RgbColor apply_curves(RgbColor color, CurvesAdjustment settings) {
  return RgbColor{curves_channel(color.red, settings), curves_channel(color.green, settings),
                  curves_channel(color.blue, settings)};
}

RgbColor apply_hue_saturation(RgbColor color, HueSaturationAdjustment settings) {
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
  settings.curves.shadow_output = metadata_int_or(layer, kLayerMetadataAdjustmentCurvesShadowOutput, 0);
  settings.curves.midtone_output = metadata_int_or(layer, kLayerMetadataAdjustmentCurvesMidtoneOutput, 128);
  settings.curves.highlight_output = metadata_int_or(layer, kLayerMetadataAdjustmentCurvesHighlightOutput, 255);
  settings.hue_saturation.hue_shift = metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationHueShift, 0);
  settings.hue_saturation.saturation_delta =
      metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationSaturationDelta, 0);
  settings.hue_saturation.lightness_delta =
      metadata_int_or(layer, kLayerMetadataAdjustmentHueSaturationLightnessDelta, 0);
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
  set_metadata_int(layer, kLayerMetadataAdjustmentCurvesShadowOutput,
                   std::clamp(settings.curves.shadow_output, 0, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentCurvesMidtoneOutput,
                   std::clamp(settings.curves.midtone_output, 0, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentCurvesHighlightOutput,
                   std::clamp(settings.curves.highlight_output, 0, 255));
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationHueShift,
                   std::clamp(settings.hue_saturation.hue_shift, -180, 180));
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationSaturationDelta,
                   std::clamp(settings.hue_saturation.saturation_delta, -100, 100));
  set_metadata_int(layer, kLayerMetadataAdjustmentHueSaturationLightnessDelta,
                   std::clamp(settings.hue_saturation.lightness_delta, -100, 100));
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

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto adjusted = apply_adjustment_to_color(RgbColor{px[0], px[1], px[2]}, settings);
      px[0] = adjusted.red;
      px[1] = adjusted.green;
      px[2] = adjusted.blue;
    }
  }
}

bool adjustment_has_effect(const AdjustmentSettings& settings) {
  switch (settings.kind) {
    case AdjustmentKind::Levels:
      return settings.levels.black_input != 0 || settings.levels.white_input != 255 ||
             settings.levels.gamma_percent != 100;
    case AdjustmentKind::Curves:
      return settings.curves.shadow_output != 0 || settings.curves.midtone_output != 128 ||
             settings.curves.highlight_output != 255;
    case AdjustmentKind::HueSaturation:
      return settings.hue_saturation.hue_shift != 0 || settings.hue_saturation.saturation_delta != 0 ||
             settings.hue_saturation.lightness_delta != 0;
    case AdjustmentKind::ColorBalance:
      return settings.color_balance.cyan_red != 0 || settings.color_balance.magenta_green != 0 ||
             settings.color_balance.yellow_blue != 0;
  }
  return false;
}

}  // namespace photoslop
