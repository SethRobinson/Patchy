#include "core/adjustment_deep.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {
namespace {

// The 8-bit function on the eight byte colors around `color`, blended trilinearly.
DeepChannels lift_byte_function(DeepChannels color, const AdjustmentSettings& settings) {
  std::array<int, 3> low{};
  std::array<float, 3> t{};
  for (std::size_t c = 0; c < 3U; ++c) {
    const auto value = std::clamp(color[c], 0.0F, 255.0F);
    low[c] = std::min(254, static_cast<int>(std::floor(value)));
    t[c] = value - static_cast<float>(low[c]);
  }
  DeepChannels out{0.0F, 0.0F, 0.0F};
  for (int corner = 0; corner < 8; ++corner) {
    float weight = 1.0F;
    std::array<std::uint8_t, 3> byte{};
    for (std::size_t c = 0; c < 3U; ++c) {
      const bool high = ((corner >> c) & 1) != 0;
      weight *= high ? t[c] : 1.0F - t[c];
      byte[c] = static_cast<std::uint8_t>(low[c] + (high ? 1 : 0));
    }
    if (weight <= 0.0F) {
      continue;
    }
    const auto adjusted = apply_adjustment_to_color(RgbColor{byte[0], byte[1], byte[2]}, settings);
    out[0] += weight * static_cast<float>(adjusted.red);
    out[1] += weight * static_cast<float>(adjusted.green);
    out[2] += weight * static_cast<float>(adjusted.blue);
  }
  return out;
}

float clamp255(double value) {
  return static_cast<float>(std::clamp(value, 0.0, 255.0));
}

}  // namespace

// Every kind on display-encoded values (0..255).
DeepChannels DeepAdjuster::apply_encoded(DeepChannels color) const {
  const auto& settings = settings_;
  const auto raw = color;
  for (auto& value : color) {
    value = std::clamp(value, 0.0F, 255.0F);
  }
  // Photoshop's 32-bit Hue/Saturation is unbounded: it sees the values as they are.
  if (settings.kind == AdjustmentKind::HueSaturation && domain_ == DeepDomain::Linear &&
      !settings.hue_saturation.colorize && !adjustment_runs_in_ink_space(settings)) {
    color = raw;
  }
  if (adjustment_runs_in_ink_space(settings)) {
    return lift_byte_function(color, settings);
  }
  DeepChannels out = color;
  switch (settings.kind) {
    case AdjustmentKind::Levels: {
      const auto master = levels_master_record(settings.levels);
      const std::array<LevelsRecord, 3> records{settings.levels.red, settings.levels.green, settings.levels.blue};
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] = clamp255(levels_record_transfer(levels_record_transfer(color[c], records[c]), master));
      }
      return out;
    }
    case AdjustmentKind::Curves:
      // Component curve first, then Composite RGB, as build_curves_lut does.
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] = clamp255(curves_[0].evaluate(std::clamp(curves_[c + 1U].evaluate(color[c]), 0.0, 255.0)));
      }
      return out;
    case AdjustmentKind::HueSaturation:
      if (const auto mapped = hue_saturation_transfer({color[0], color[1], color[2]}, settings.hue_saturation,
                                                      domain_ == DeepDomain::Linear)) {
        return {static_cast<float>((*mapped)[0]), static_cast<float>((*mapped)[1]),
                static_cast<float>((*mapped)[2])};
      }
      return lift_byte_function(color, settings);
    case AdjustmentKind::ColorBalance: {
      const auto delta = [](int slider) {
        return std::round(static_cast<double>(std::clamp(slider, -100, 100)) * 255.0 / 100.0);
      };
      out[0] = clamp255(color[0] + delta(settings.color_balance.cyan_red));
      out[1] = clamp255(color[1] + delta(settings.color_balance.magenta_green));
      out[2] = clamp255(color[2] + delta(settings.color_balance.yellow_blue));
      return out;
    }
    case AdjustmentKind::Invert:
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] = 255.0F - color[c];
      }
      return out;
    case AdjustmentKind::Posterize: {
      const auto levels = std::clamp(settings.posterize.levels, 2, 255);
      for (std::size_t c = 0; c < 3U; ++c) {
        // Photoshop buckets 16-bit samples as floor(v16 * levels / 65536), the 16-bit
        // form of its 8-bit floor(v * levels / 256) (deep corpus adj-posterize-16: the
        // 4/6 boundary sits at 43690). On 8-bit-exact values both agree.
        const auto scaled = static_cast<double>(color[c]) * 257.0 * levels / 65536.0;
        const auto bucket = std::clamp(static_cast<int>(std::floor(scaled)), 0, levels - 1);
        out[c] = static_cast<float>(bucket) * 255.0F / static_cast<float>(levels - 1);
      }
      return out;
    }
    case AdjustmentKind::Threshold: {
      const auto luminance = (color[0] * 30.0F + color[1] * 59.0F + color[2] * 11.0F) / 100.0F;
      const auto value = luminance >= static_cast<float>(settings.threshold.level) ? 255.0F : 0.0F;
      return {value, value, value};
    }
    case AdjustmentKind::BrightnessContrast: {
      const auto& bc = settings.brightness_contrast;
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] = clamp255(brightness_contrast_transfer(color[c], bc.brightness, bc.contrast, bc.use_legacy));
      }
      return out;
    }
    case AdjustmentKind::Exposure:
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] = clamp255(exposure_transfer(color[c], settings.exposure));
      }
      return out;
  }
  return color;
}

CurveSpline::CurveSpline(const CurveControlPoints& source)
    : points_(normalized_curve_control_points(source)), second_derivatives_(points_.size(), 0.0) {
  // build_curve_lut's natural cubic (zero second derivative at both ends).
  const auto count = points_.size();
  std::vector<double> workspace(count, 0.0);
  for (std::size_t index = 1U; index + 1U < count; ++index) {
    const auto previous_span = static_cast<double>(points_[index].input - points_[index - 1U].input);
    const auto next_span = static_cast<double>(points_[index + 1U].input - points_[index].input);
    const auto combined_span = previous_span + next_span;
    const auto sigma = previous_span / combined_span;
    const auto pivot = sigma * second_derivatives_[index - 1U] + 2.0;
    second_derivatives_[index] = (sigma - 1.0) / pivot;
    const auto previous_slope =
        static_cast<double>(points_[index].output - points_[index - 1U].output) / previous_span;
    const auto next_slope = static_cast<double>(points_[index + 1U].output - points_[index].output) / next_span;
    workspace[index] = (6.0 * (next_slope - previous_slope) / combined_span - sigma * workspace[index - 1U]) / pivot;
  }
  for (std::size_t upper = count - 1U; upper > 0U; --upper) {
    const auto index = upper - 1U;
    second_derivatives_[index] = second_derivatives_[index] * second_derivatives_[upper] + workspace[index];
  }
}

double CurveSpline::evaluate(double input) const {
  if (points_.empty()) {
    return input;
  }
  if (input <= points_.front().input) {
    return points_.front().output;
  }
  if (input >= points_.back().input) {
    return points_.back().output;
  }
  std::size_t upper = 1U;
  while (upper + 1U < points_.size() && input > points_[upper].input) {
    ++upper;
  }
  const auto span = static_cast<double>(points_[upper].input - points_[upper - 1U].input);
  const auto left_weight = (static_cast<double>(points_[upper].input) - input) / span;
  const auto right_weight = (input - static_cast<double>(points_[upper - 1U].input)) / span;
  return left_weight * points_[upper - 1U].output + right_weight * points_[upper].output +
         ((left_weight * left_weight * left_weight - left_weight) * second_derivatives_[upper - 1U] +
          (right_weight * right_weight * right_weight - right_weight) * second_derivatives_[upper]) *
             span * span / 6.0;
}

DeepAdjuster::DeepAdjuster(const AdjustmentSettings& settings, DeepDomain domain)
    : settings_(settings), domain_(domain) {
  if (settings.kind == AdjustmentKind::Curves) {
    curves_.emplace_back(settings.curves.rgb);
    curves_.emplace_back(settings.curves.red);
    curves_.emplace_back(settings.curves.green);
    curves_.emplace_back(settings.curves.blue);
  }
}

DeepChannels DeepAdjuster::apply(DeepChannels color) const {
  if (domain_ == DeepDomain::Encoded) {
    return apply_encoded(color);
  }
  if (settings_.kind == AdjustmentKind::Exposure && !adjustment_runs_in_ink_space(settings_)) {
    const auto exposure = clamp_exposure(settings_.exposure);
    const auto gain = std::pow(2.0, static_cast<double>(exposure.exposure_hundredths) / 100.0);
    const auto offset = static_cast<double>(exposure.offset_ten_thousandths) / 10000.0;
    const auto inverse_gamma = 100.0 / static_cast<double>(exposure.gamma_hundredths);
    DeepChannels out{};
    for (std::size_t c = 0; c < 3U; ++c) {
      const auto linear = std::max(0.0, static_cast<double>(color[c]) / 255.0 * gain + offset);
      out[c] = static_cast<float>(std::pow(linear, inverse_gamma) * 255.0);
    }
    return out;
  }
  if (settings_.kind == AdjustmentKind::Levels && !adjustment_runs_in_ink_space(settings_)) {
    // Photoshop 2026's 32-bit Levels (fitted on the deep corpus's adj-levels-32 render):
    // the record maps the linear value itself, (v - black) / (white - black) through a
    // signed gamma power, with no clamping, so values below the black point go negative.
    const auto transfer = [](double value, LevelsRecord record) {
      record = clamp_levels_record(record);
      if (record.black_input == 0 && record.white_input == 255 && record.gamma_percent == 100 &&
          record.black_output == 0 && record.white_output == 255) {
        return value;
      }
      const auto normalized = (value * 255.0 - record.black_input) / (record.white_input - record.black_input);
      const auto inverse_gamma = 100.0 / static_cast<double>(record.gamma_percent);
      const auto leveled = normalized < 0.0 ? -std::pow(-normalized, inverse_gamma) : std::pow(normalized, inverse_gamma);
      return (record.black_output + leveled * (record.white_output - record.black_output)) / 255.0;
    };
    const auto master = levels_master_record(settings_.levels);
    const std::array<LevelsRecord, 3> records{settings_.levels.red, settings_.levels.green, settings_.levels.blue};
    DeepChannels out{};
    for (std::size_t c = 0; c < 3U; ++c) {
      out[c] = static_cast<float>(transfer(transfer(color[c] / 255.0, records[c]), master) * 255.0);
    }
    return out;
  }
  // Every other kind maps the linear values on the 0..255 scale as if they were display
  // values, as Photoshop's 32-bit Curves does (deep corpus adj-curves-32).
  return apply_encoded(color);
}

DeepChannels apply_adjustment_deep(DeepChannels color, const AdjustmentSettings& settings, DeepDomain domain) {
  return DeepAdjuster(settings, domain).apply(color);
}

}  // namespace patchy
