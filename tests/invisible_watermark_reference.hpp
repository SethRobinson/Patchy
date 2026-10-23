#pragma once

// Test-only reference of the embedding side of the invisible-watermark library's
// "dwtDct" method (MIT; docs/legal-constraints.md, "Watermark detection"), so the
// core and UI suites can check Patchy's decoder against images marked the way Stable
// Diffusion's scripts mark them. Qt-free and deterministic. Patchy itself never
// embeds a watermark; this header is compiled into the test programs only.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace patchy::test {

// The library's two pixel paths: the SD 1.x/2.x scripts hand it 8-bit BGR, so the
// YUV planes and the inverse transform's output are 8-bit (numpy truncates the
// float result into the uint8 array); SDXL hands it float BGR in 0..255, where
// OpenCV's float YUV uses a chroma offset of 0.5, and the result is truncated to
// 8 bits when the PNG is written.
enum class WatermarkEmbedPath : std::uint8_t { Uint8, Float };

inline std::uint64_t watermark_fixture_splitmix64(std::uint64_t& state) noexcept {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

// A photo-like RGB image: smooth color gradients, a few soft discs and fine noise.
inline std::vector<std::uint8_t> watermark_fixture_image(int width, int height, std::uint64_t seed) {
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
  std::uint64_t state = seed;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double fx = static_cast<double>(x) / width;
      const double fy = static_cast<double>(y) / height;
      const double disc = std::max(0.0, 1.0 - std::hypot(fx - 0.35, fy - 0.4) * 4.0);
      const double ring = std::max(0.0, 1.0 - std::abs(std::hypot(fx - 0.7, fy - 0.65) - 0.18) * 12.0);
      const int noise = static_cast<int>(watermark_fixture_splitmix64(state) % 25) - 12;
      const auto channel = [&](double value) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(value)) + noise, 0, 255));
      };
      auto* px = rgb.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 3;
      px[0] = channel(40 + 150 * fx + 60 * disc);
      px[1] = channel(70 + 90 * fy + 80 * ring);
      px[2] = channel(160 - 100 * fx * fy + 50 * disc);
    }
  }
  return rgb;
}

// Embeds `bits` into interleaved RGB the way EmbedMaxDct.encode does (scales
// [0, 36, 36] with `for channel in range(2)`: only U is marked). `bgr_order` true
// models a caller that passes RGB where the library expects BGR.
inline std::vector<std::uint8_t> embed_dwt_dct_reference(const std::vector<std::uint8_t>& rgb, int width, int height,
                                                         const std::vector<std::uint8_t>& bits,
                                                         WatermarkEmbedPath path, bool bgr_order = false) {
  const bool integer = path == WatermarkEmbedPath::Uint8;
  const int ri = bgr_order ? 2 : 0;
  const int bi = bgr_order ? 0 : 2;
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<double> yp(count), up(count), vp(count);
  const auto descale = [](std::int64_t value) { return static_cast<int>((value + (1 << 13)) >> 14); };
  for (std::size_t i = 0; i < count; ++i) {
    const int r = rgb[i * 3 + static_cast<std::size_t>(ri)];
    const int g = rgb[i * 3 + 1];
    const int b = rgb[i * 3 + static_cast<std::size_t>(bi)];
    if (integer) {
      const int y = descale(static_cast<std::int64_t>(b) * 1868 + g * 9617 + r * 4899);
      yp[i] = y;
      up[i] = std::clamp(descale(static_cast<std::int64_t>(b - y) * 8061 + (128 << 14)), 0, 255);
      vp[i] = std::clamp(descale(static_cast<std::int64_t>(r - y) * 14369 + (128 << 14)), 0, 255);
    } else {
      const double y = 0.299 * r + 0.587 * g + 0.114 * b;
      yp[i] = y;
      up[i] = (b - y) * 0.492 + 0.5;
      vp[i] = (r - y) * 0.877 + 0.5;
    }
  }
  const int rows = height / 4 * 4;
  const int cols = width / 4 * 4;
  const int ca_rows = rows / 2;
  const int ca_cols = cols / 2;
  const auto at = [&](int y, int x) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x; };
  std::vector<double> ca(static_cast<std::size_t>(ca_rows) * ca_cols), ch(ca.size()), cv(ca.size()), cd(ca.size());
  for (int i = 0; i < ca_rows; ++i) {
    for (int j = 0; j < ca_cols; ++j) {
      const double a = up[at(2 * i, 2 * j)];
      const double b = up[at(2 * i, 2 * j + 1)];
      const double c = up[at(2 * i + 1, 2 * j)];
      const double d = up[at(2 * i + 1, 2 * j + 1)];
      const auto k = static_cast<std::size_t>(i) * ca_cols + j;
      ca[k] = (a + b + c + d) / 2;
      ch[k] = (a + b - c - d) / 2;
      cv[k] = (a - b + c - d) / 2;
      cd[k] = (a - b - c + d) / 2;
    }
  }
  constexpr double scale = 36.0;
  std::size_t num = 0;
  for (int i = 0; i < ca_rows / 4; ++i) {
    for (int j = 0; j < ca_cols / 4; ++j) {
      int best = 1;
      double best_abs = -1.0;
      for (int k = 1; k < 16; ++k) {
        const double value = std::abs(ca[static_cast<std::size_t>(i * 4 + k / 4) * ca_cols + j * 4 + k % 4]);
        if (value > best_abs) {
          best_abs = value;
          best = k;
        }
      }
      auto& value = ca[static_cast<std::size_t>(i * 4 + best / 4) * ca_cols + j * 4 + best % 4];
      const double bit = bits[num % bits.size()];
      const double magnitude = (std::floor(std::abs(value) / scale) + 0.25 + 0.5 * bit) * scale;
      value = value >= 0.0 ? magnitude : -magnitude;
      ++num;
    }
  }
  // pywt.idwt2((ca1, (v1, h1, d1))): the library passes the detail bands exchanged.
  for (int i = 0; i < ca_rows; ++i) {
    for (int j = 0; j < ca_cols; ++j) {
      const auto k = static_cast<std::size_t>(i) * ca_cols + j;
      const double a = ca[k];
      const double h = cv[k];
      const double v = ch[k];
      const double d = cd[k];
      const double out[4] = {(a + h + v + d) / 2, (a + h - v - d) / 2, (a - h + v - d) / 2, (a - h - v + d) / 2};
      const std::size_t targets[4] = {at(2 * i, 2 * j), at(2 * i, 2 * j + 1), at(2 * i + 1, 2 * j),
                                      at(2 * i + 1, 2 * j + 1)};
      for (int n = 0; n < 4; ++n) {
        up[targets[n]] = integer ? std::clamp(std::trunc(out[n]), 0.0, 255.0) : out[n];
      }
    }
  }
  std::vector<std::uint8_t> marked(rgb.size());
  for (std::size_t i = 0; i < count; ++i) {
    double r = 0;
    double g = 0;
    double b = 0;
    if (integer) {
      const auto y = static_cast<std::int64_t>(yp[i]);
      const auto u = static_cast<std::int64_t>(up[i]) - 128;
      const auto v = static_cast<std::int64_t>(vp[i]) - 128;
      b = static_cast<double>(y + descale(u * 33292));
      g = static_cast<double>(y + descale(v * -9519 + u * -6472));
      r = static_cast<double>(y + descale(v * 18678));
    } else {
      const double u = up[i] - 0.5;
      const double v = vp[i] - 0.5;
      b = yp[i] + 2.032 * u;
      g = yp[i] - 0.395 * u - 0.581 * v;
      r = yp[i] + 1.140 * v;
    }
    const auto to8 = [](double value) { return static_cast<std::uint8_t>(std::clamp(std::trunc(value), 0.0, 255.0)); };
    marked[i * 3 + static_cast<std::size_t>(ri)] = to8(r);
    marked[i * 3 + 1] = to8(g);
    marked[i * 3 + static_cast<std::size_t>(bi)] = to8(b);
  }
  return marked;
}

}  // namespace patchy::test
