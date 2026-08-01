#include "filters/auto_levels_math.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

constexpr std::uint64_t kClipDivisor = 1000;  // 0.1% of samples per histogram end
constexpr double kMidtoneTarget = 128.0 / 255.0;

double midtone_for_gamma(double normalized_mean, int gamma_percent) {
  return std::pow(normalized_mean, 100.0 / static_cast<double>(gamma_percent));
}

// The auto adjustments' instance of the levels transfer. The formula is
// deliberately per-consumer (see the note above clamp_levels_record in
// core/adjustment_layer.hpp); this copy matches core's levels_channel, float
// round-trip included, so applying Auto Tone equals committing the Levels
// dialog's Auto scan on every channel.
std::uint8_t levels_transfer(std::uint8_t value, const LevelsRecord& record) {
  const auto input_range = static_cast<double>(record.white_input - record.black_input);
  const auto gamma = static_cast<double>(record.gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;
  const auto normalized = std::clamp(
      (static_cast<double>(value) - static_cast<double>(record.black_input)) / input_range, 0.0, 1.0);
  const auto leveled = std::pow(normalized, inverse_gamma);
  const auto output = static_cast<double>(record.black_output) +
                      leveled * static_cast<double>(record.white_output - record.black_output);
  return static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<float>(output)), 0L, 255L));
}

}  // namespace

std::optional<LevelsRecord> auto_levels_clip_scan(const AutoLevelsHistogram& histogram,
                                                  std::uint64_t total_samples) {
  const auto threshold = std::max<std::uint64_t>(1, total_samples / kClipDivisor);
  std::uint64_t cumulative = 0;
  int black = 0;
  bool black_found = false;
  for (; black < 255; ++black) {
    cumulative += histogram[static_cast<std::size_t>(black)];
    if (cumulative > threshold) {
      black_found = true;
      break;
    }
  }
  cumulative = 0;
  int white = 255;
  bool white_found = false;
  for (; white > black + 1; --white) {
    cumulative += histogram[static_cast<std::size_t>(white)];
    if (cumulative > threshold) {
      white_found = true;
      break;
    }
  }
  if (!black_found || !white_found) {
    return std::nullopt;
  }
  return LevelsRecord{black, white, 100, 0, 255};
}

int auto_color_gamma_percent(double normalized_mean) {
  if (!(normalized_mean > 0.0) || !(normalized_mean < 1.0)) {
    return 100;
  }
  // midtone_for_gamma is strictly increasing in gamma_percent for means in
  // (0, 1), so binary-search the first gamma at or past the target, then let
  // its predecessor win ties.
  int low = 10;
  int high = 999;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    if (midtone_for_gamma(normalized_mean, mid) < kMidtoneTarget) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  if (low > 10) {
    const auto below = std::abs(midtone_for_gamma(normalized_mean, low - 1) - kMidtoneTarget);
    const auto at = std::abs(midtone_for_gamma(normalized_mean, low) - kMidtoneTarget);
    if (below <= at) {
      return low - 1;
    }
  }
  return low;
}

std::optional<LevelsRecord> auto_color_channel_record(const AutoLevelsHistogram& histogram,
                                                      std::uint64_t total_samples) {
  auto record = auto_levels_clip_scan(histogram, total_samples);
  if (!record.has_value() || total_samples == 0) {
    return std::nullopt;
  }
  std::uint64_t weighted = 0;
  for (std::size_t value = 0; value < histogram.size(); ++value) {
    weighted += static_cast<std::uint64_t>(histogram[value]) * static_cast<std::uint64_t>(value);
  }
  const auto mean = static_cast<double>(weighted) / static_cast<double>(total_samples);
  const auto normalized_mean = (mean - static_cast<double>(record->black_input)) /
                               static_cast<double>(record->white_input - record->black_input);
  record->gamma_percent = auto_color_gamma_percent(normalized_mean);
  return record;
}

std::array<std::uint8_t, 256> auto_levels_lut(LevelsRecord record) {
  record = clamp_levels_record(record);
  std::array<std::uint8_t, 256> lut{};
  for (int value = 0; value < 256; ++value) {
    lut[static_cast<std::size_t>(value)] = levels_transfer(static_cast<std::uint8_t>(value), record);
  }
  return lut;
}

std::array<std::uint8_t, 256> auto_levels_identity_lut() {
  std::array<std::uint8_t, 256> lut{};
  for (int value = 0; value < 256; ++value) {
    lut[static_cast<std::size_t>(value)] = static_cast<std::uint8_t>(value);
  }
  return lut;
}

}  // namespace patchy
