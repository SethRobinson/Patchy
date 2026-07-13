#include "formats/raw_white_balance.hpp"

#include <algorithm>
#include <cmath>

namespace patchy::raw {

namespace {

constexpr double kMinTemperatureK = 1667.0;
constexpr double kMaxTemperatureK = 25000.0;

// Tint slider unit -> Duv displacement in CIE 1960 uv (positive tint = magenta = below the
// locus). 150 tint units ~= 0.05 Duv, in the neighborhood of the ACR/Lightroom slider feel.
constexpr double kTintToDuv = 1.0 / 3000.0;

struct Uv {
  double u{0.0};
  double v{0.0};
};

Uv uv_from_xy(const std::array<double, 2>& xy) {
  const auto denominator = -2.0 * xy[0] + 12.0 * xy[1] + 3.0;
  return {4.0 * xy[0] / denominator, 6.0 * xy[1] / denominator};
}

std::array<double, 2> xy_from_uv(const Uv& uv) {
  const auto denominator = 2.0 * uv.u - 8.0 * uv.v + 4.0;
  return {3.0 * uv.u / denominator, 2.0 * uv.v / denominator};
}

std::array<double, 3> xyz_from_xy(const std::array<double, 2>& xy) {
  if (xy[1] <= 1e-9) {
    return {0.0, 0.0, 0.0};
  }
  return {xy[0] / xy[1], 1.0, (1.0 - xy[0] - xy[1]) / xy[1]};
}

// Locus tangent direction at `temperature_k`, used to build the isotherm normal.
Uv locus_normal(double temperature_k) {
  const auto step = std::max(1.0, temperature_k * 0.001);
  const auto ahead = uv_from_xy(chromaticity_for_temperature(std::min(temperature_k + step, kMaxTemperatureK)));
  const auto behind = uv_from_xy(chromaticity_for_temperature(std::max(temperature_k - step, kMinTemperatureK)));
  const auto tangent_u = ahead.u - behind.u;
  const auto tangent_v = ahead.v - behind.v;
  const auto length = std::hypot(tangent_u, tangent_v);
  if (length <= 1e-12) {
    return {0.0, 1.0};
  }
  // Rotate the tangent 90 degrees counter-clockwise. Along the locus u decreases as
  // temperature rises, so this normal points to the green side (above the locus).
  return {-tangent_v / length, tangent_u / length};
}

Uv uv_for_white_balance(const WhiteBalance& wb) {
  const auto temperature = std::clamp(wb.temperature_k, kMinTemperatureK, kMaxTemperatureK);
  const auto locus = uv_from_xy(chromaticity_for_temperature(temperature));
  const auto normal = locus_normal(temperature);
  const auto duv = -wb.tint * kTintToDuv;
  return {locus.u + duv * normal.u, locus.v + duv * normal.v};
}

bool row_is_zero(const std::array<double, 3>& row) {
  return std::abs(row[0]) + std::abs(row[1]) + std::abs(row[2]) <= 1e-9;
}

// XYZ(D65) -> linear sRGB, the fallback "camera" for files without a usable matrix.
constexpr CameraMatrix kSrgbCameraMatrix = {{
    {{3.2404542, -1.5371385, -0.4985314}},
    {{-0.9692660, 1.8760108, 0.0415560}},
    {{0.0556434, -0.2040259, 1.0572252}},
    {{0.0, 0.0, 0.0}},
}};

const CameraMatrix& usable_matrix(const CameraMatrix& cam_xyz) {
  if (row_is_zero(cam_xyz[0]) || row_is_zero(cam_xyz[1]) || row_is_zero(cam_xyz[2])) {
    return kSrgbCameraMatrix;
  }
  return cam_xyz;
}

std::array<double, 4> camera_response_to_xyz(const CameraMatrix& matrix, const std::array<double, 3>& xyz) {
  std::array<double, 4> response{};
  for (std::size_t channel = 0; channel < 4; ++channel) {
    response[channel] =
        matrix[channel][0] * xyz[0] + matrix[channel][1] * xyz[1] + matrix[channel][2] * xyz[2];
  }
  if (row_is_zero(matrix[3])) {
    response[3] = response[1];
  }
  return response;
}

// Solve the 3x3 system cam_xyz_rgb * xyz = response for xyz (Cramer's rule; the camera
// matrices involved are well-conditioned).
std::optional<std::array<double, 3>> solve_xyz(const CameraMatrix& matrix, const std::array<double, 3>& response) {
  const auto& m = matrix;
  const auto det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                   m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  if (std::abs(det) <= 1e-12) {
    return std::nullopt;
  }
  const auto solve_column = [&](int column) {
    std::array<std::array<double, 3>, 3> replaced{};
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        replaced[row][col] = (col == column) ? response[row] : m[row][col];
      }
    }
    return replaced[0][0] * (replaced[1][1] * replaced[2][2] - replaced[1][2] * replaced[2][1]) -
           replaced[0][1] * (replaced[1][0] * replaced[2][2] - replaced[1][2] * replaced[2][0]) +
           replaced[0][2] * (replaced[1][0] * replaced[2][1] - replaced[1][1] * replaced[2][0]);
  };
  return std::array<double, 3>{solve_column(0) / det, solve_column(1) / det, solve_column(2) / det};
}

}  // namespace

std::array<double, 2> chromaticity_for_temperature(double temperature_k) {
  const auto t = std::clamp(temperature_k, kMinTemperatureK, kMaxTemperatureK);
  double x = 0.0;
  if (t < 4000.0) {
    // Kim et al. cubic approximation of the Planckian locus.
    const auto t1 = 1e3 / t;
    const auto t2 = t1 * t1;
    const auto t3 = t2 * t1;
    x = -0.2661239 * t3 - 0.2343589 * t2 + 0.8776956 * t1 + 0.179910;
  } else if (t <= 7000.0) {
    // CIE daylight locus.
    const auto t1 = 1e3 / t;
    const auto t2 = t1 * t1;
    const auto t3 = t2 * t1;
    x = -4.6070 * t3 + 2.9678 * t2 + 0.09911 * t1 + 0.244063;
  } else {
    const auto t1 = 1e3 / t;
    const auto t2 = t1 * t1;
    const auto t3 = t2 * t1;
    x = -2.0064 * t3 + 1.9018 * t2 + 0.24748 * t1 + 0.237040;
  }

  double y = 0.0;
  if (t < 2222.0) {
    y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
  } else if (t < 4000.0) {
    y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
  } else {
    y = -3.000 * x * x + 2.870 * x - 0.275;
  }
  return {x, y};
}

std::array<double, 4> multipliers_for_white_balance(const WhiteBalance& wb, const CameraMatrix& cam_xyz) {
  const auto& matrix = usable_matrix(cam_xyz);
  const auto white_xyz = xyz_from_xy(xy_from_uv(uv_for_white_balance(wb)));
  auto response = camera_response_to_xyz(matrix, white_xyz);
  for (auto& channel : response) {
    channel = std::max(channel, 1e-6);
  }
  return {response[1] / response[0], 1.0, response[1] / response[2], response[1] / response[3]};
}

std::optional<WhiteBalance> white_balance_for_multipliers(const std::array<double, 4>& multipliers,
                                                          const CameraMatrix& cam_xyz) {
  if (multipliers[0] <= 0.0 || multipliers[1] <= 0.0 || multipliers[2] <= 0.0) {
    return std::nullopt;
  }
  const auto& matrix = usable_matrix(cam_xyz);
  // The camera's response to the illuminant is the reciprocal of the multipliers.
  const std::array<double, 3> response = {1.0 / multipliers[0], 1.0 / multipliers[1], 1.0 / multipliers[2]};
  const auto white_xyz = solve_xyz(matrix, response);
  if (!white_xyz.has_value() || (*white_xyz)[1] <= 1e-9) {
    return std::nullopt;
  }
  const auto sum = (*white_xyz)[0] + (*white_xyz)[1] + (*white_xyz)[2];
  if (sum <= 1e-9) {
    return std::nullopt;
  }
  const auto white_uv = uv_from_xy({(*white_xyz)[0] / sum, (*white_xyz)[1] / sum});

  // Nearest locus temperature: coarse log-spaced scan, then ternary refinement. Plain
  // deterministic double math (cross-toolchain rule).
  const auto distance_squared_at = [&](double temperature) {
    const auto locus = uv_from_xy(chromaticity_for_temperature(temperature));
    const auto du = white_uv.u - locus.u;
    const auto dv = white_uv.v - locus.v;
    return du * du + dv * dv;
  };
  constexpr int kScanSteps = 64;
  const auto log_min = std::log(kMinTemperatureK);
  const auto log_max = std::log(kMaxTemperatureK);
  double best_temperature = kMinTemperatureK;
  double best_distance = distance_squared_at(kMinTemperatureK);
  for (int step = 1; step <= kScanSteps; ++step) {
    const auto temperature = std::exp(log_min + (log_max - log_min) * step / kScanSteps);
    const auto distance = distance_squared_at(temperature);
    if (distance < best_distance) {
      best_distance = distance;
      best_temperature = temperature;
    }
  }
  auto low = best_temperature / std::exp((log_max - log_min) / kScanSteps);
  auto high = best_temperature * std::exp((log_max - log_min) / kScanSteps);
  low = std::max(low, kMinTemperatureK);
  high = std::min(high, kMaxTemperatureK);
  for (int iteration = 0; iteration < 60; ++iteration) {
    const auto third = (high - low) / 3.0;
    const auto probe_low = low + third;
    const auto probe_high = high - third;
    if (distance_squared_at(probe_low) < distance_squared_at(probe_high)) {
      high = probe_high;
    } else {
      low = probe_low;
    }
  }
  const auto temperature = (low + high) / 2.0;

  const auto locus = uv_from_xy(chromaticity_for_temperature(temperature));
  const auto normal = locus_normal(temperature);
  const auto duv = (white_uv.u - locus.u) * normal.u + (white_uv.v - locus.v) * normal.v;
  return WhiteBalance{temperature, -duv / kTintToDuv};
}

}  // namespace patchy::raw
