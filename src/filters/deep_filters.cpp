#include "filters/deep_filters.hpp"

#include "filters/smart_filter_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace patchy {

namespace {

constexpr double kDeepFilterPi = 3.14159265358979323846;

// Filters with a deep kernel at both depths. 32-bit documents get only these: the
// blurs, Unsharp Mask, Pixel Mosaic, the geometric distortions and Clouds, which mean the
// same thing on linear light (Photoshop's 32-bit filter set is similarly limited).
constexpr std::array<std::string_view, 9> kLinearSafeFilters{
    "patchy.filters.gaussian_blur", "patchy.filters.box_blur",    "patchy.filters.radial_blur",
    "patchy.filters.unsharp_mask",  "patchy.filters.pixelate",    "patchy.filters.twirl",
    "patchy.filters.wave",          "patchy.filters.pinch_bloat", "patchy.filters.clouds"};

// Further deep kernels for 16-bit (display-encoded) documents: point and neighborhood
// math whose 8-bit constants (mid gray 128, the luminance weights) assume encoded values.
constexpr std::array<std::string_view, 12> kEncodedOnlyFilters{
    "patchy.filters.invert",    "patchy.filters.brightness_contrast", "patchy.filters.grayscale",
    "patchy.filters.desaturate", "patchy.filters.sepia",              "patchy.filters.threshold",
    "patchy.filters.posterize", "patchy.filters.vignette",            "patchy.filters.high_pass",
    "patchy.filters.sharpen",   "patchy.filters.emboss",              "patchy.filters.add_noise"};

template <std::size_t N>
[[nodiscard]] bool listed(const std::array<std::string_view, N>& list, std::string_view id) {
  return std::find(list.begin(), list.end(), id) != list.end();
}

struct DeepAccum {
  std::array<double, 3> premultiplied{0.0, 0.0, 0.0};
  double alpha{0.0};
  double weight{0.0};
};

void accumulate_pixel(DeepAccum& accum, const float* px, double weight) {
  if (weight <= 0.0) {
    return;
  }
  const auto alpha = static_cast<double>(px[3]) / 255.0;
  accum.weight += weight;
  accum.alpha += alpha * weight;
  for (std::size_t c = 0; c < 3U; ++c) {
    accum.premultiplied[c] += static_cast<double>(px[c]) * alpha * weight;
  }
}

void accumulate_sample(DeepAccum& accum, const DeepImage& image, double x, double y, double weight = 1.0) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, image.width - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, image.height - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(image.width - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(image.height - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  accumulate_pixel(accum, image.at(x0, y0), weight * (1.0 - tx) * (1.0 - ty));
  accumulate_pixel(accum, image.at(x1, y0), weight * tx * (1.0 - ty));
  accumulate_pixel(accum, image.at(x0, y1), weight * (1.0 - tx) * ty);
  accumulate_pixel(accum, image.at(x1, y1), weight * tx * ty);
}

void write_accumulated(float* dst, const DeepAccum& accum) {
  for (std::size_t c = 0; c < 3U; ++c) {
    dst[c] = accum.alpha > 0.000001 ? static_cast<float>(accum.premultiplied[c] / accum.alpha) : 0.0F;
  }
  dst[3] = accum.weight > 0.0 ? static_cast<float>(accum.alpha / accum.weight * 255.0) : 255.0F;
}

[[nodiscard]] double luminance_of(const float* px) {
  return (static_cast<double>(px[0]) * 30.0 + static_cast<double>(px[1]) * 59.0 +
          static_cast<double>(px[2]) * 11.0) /
         100.0;
}

[[nodiscard]] double sampled_luminance(const DeepImage& image, double x, double y) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, image.width - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, image.height - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(image.width - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(image.height - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  const auto top = luminance_of(image.at(x0, y0)) * (1.0 - tx) + luminance_of(image.at(x1, y0)) * tx;
  const auto bottom = luminance_of(image.at(x0, y1)) * (1.0 - tx) + luminance_of(image.at(x1, y1)) * tx;
  return top * (1.0 - ty) + bottom * ty;
}

[[nodiscard]] std::size_t pixel_count(const DeepImage& image) {
  return static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
}

// One channel of the image as a plane; channel 3 is alpha, colors are premultiplied
// (by alpha / 255) when `premultiply` is set.
[[nodiscard]] std::vector<float> channel_plane(const DeepImage& image, std::size_t channel, bool premultiply) {
  std::vector<float> plane(pixel_count(image));
  for (std::size_t i = 0; i < plane.size(); ++i) {
    const auto* px = image.rgba.data() + i * 4U;
    plane[i] = premultiply && channel < 3U ? px[channel] * px[3] / 255.0F : px[channel];
  }
  return plane;
}

}  // namespace

DeepFilterSupport deep_filter_support(std::string_view filter_id, BitDepth depth) {
  if (depth == BitDepth::UInt8 || listed(kLinearSafeFilters, filter_id)) {
    return DeepFilterSupport::Native;
  }
  if (depth == BitDepth::Float32) {
    return DeepFilterSupport::Unsupported;
  }
  return listed(kEncodedOnlyFilters, filter_id) ? DeepFilterSupport::Native : DeepFilterSupport::EightBitPrecision;
}

DeepImage load_deep_image(const PixelBuffer& pixels) {
  DeepImage image;
  image.width = pixels.width();
  image.height = pixels.height();
  image.domain = deep_domain_for(pixels.format().bit_depth);
  image.rgba.resize(pixel_count(image) * 4U);
  for (std::int32_t y = 0; y < image.height; ++y) {
    load_rgba_row(pixels, y, 0, image.width, image.domain,
                  std::span<float>(image.at(0, y), static_cast<std::size_t>(image.width) * 4U));
  }
  return image;
}

void store_deep_image(PixelBuffer& pixels, const DeepImage& image) {
  if (pixels.width() != image.width || pixels.height() != image.height) {
    return;
  }
  std::vector<float> row(static_cast<std::size_t>(image.width) * 4U);
  const auto keep_alpha = pixels.format().channels >= 4;
  for (std::int32_t y = 0; y < image.height; ++y) {
    std::copy(image.at(0, y), image.at(0, y) + row.size(), row.begin());
    for (std::size_t i = 0; i < row.size(); ++i) {
      if (!std::isfinite(row[i]) || row[i] < 0.0F) {
        row[i] = 0.0F;
      }
      if (!keep_alpha && i % 4U == 3U) {
        row[i] = 255.0F;
      }
    }
    store_rgba_row(pixels, y, 0, image.width, image.domain, row);
  }
}

void report_deep_filter_progress(const FilterProgress* progress, std::int32_t completed, std::int32_t total,
                                 FilterProgressStage stage) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  const auto safe_total = std::max<std::int32_t>(1, total);
  if (!progress->update(std::clamp(completed, 0, safe_total), safe_total, stage)) {
    throw FilterCancelled();
  }
}

void deep_blend_with_original(DeepImage& image, const DeepImage& original, int amount_percent) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  if (amount_percent >= 100) {
    return;
  }
  if (amount_percent <= 0 || image.rgba.size() != original.rgba.size()) {
    image.rgba = original.rgba;
    return;
  }
  const auto effect = static_cast<float>(amount_percent) / 100.0F;
  for (std::size_t i = 0; i < image.rgba.size(); i += 4U) {
    for (std::size_t c = 0; c < 3U; ++c) {
      image.rgba[i + c] = original.rgba[i + c] * (1.0F - effect) + image.rgba[i + c] * effect;
    }
    image.rgba[i + 3U] = original.rgba[i + 3U];
  }
}

void deep_box_blur(DeepImage& image, int radius, const FilterProgress* progress) {
  const auto width = image.width;
  const auto height = image.height;
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto r = std::max(1, radius);
  const auto taps = static_cast<double>(2 * r + 1);
  const auto row_stride = static_cast<std::size_t>(width) * 4U;
  const auto source = image;
  // Alpha-weighted window sums (color * alpha, alpha) over the edge-clamped row, as the
  // 8-bit Box Blur builds them, in double.
  const auto build_h_row = [&](std::int32_t source_y, double* out) {
    source_y = std::clamp<std::int32_t>(source_y, 0, height - 1);
    std::array<double, 4> sum{};
    const auto add = [&](std::int32_t x, double sign) {
      const auto* px = source.at(std::clamp<std::int32_t>(x, 0, width - 1), source_y);
      const auto alpha = static_cast<double>(px[3]) / 255.0;
      for (std::size_t c = 0; c < 3U; ++c) {
        sum[c] += sign * static_cast<double>(px[c]) * alpha;
      }
      sum[3] += sign * alpha;
    };
    for (auto d = -r; d <= r; ++d) {
      add(d, 1.0);
    }
    for (std::int32_t x = 0; x < width; ++x) {
      std::copy(sum.begin(), sum.end(), out + static_cast<std::size_t>(x) * 4U);
      add(x + r + 1, 1.0);
      add(x - r, -1.0);
    }
  };
  std::vector<double> scratch(row_stride);
  std::vector<double> sum(row_stride, 0.0);
  for (auto d = -r; d <= r; ++d) {
    build_h_row(d, scratch.data());
    for (std::size_t i = 0; i < row_stride; ++i) {
      sum[i] += scratch[i];
    }
  }
  for (std::int32_t y = 0; y < height; ++y) {
    report_deep_filter_progress(progress, y, height, FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* accum = sum.data() + static_cast<std::size_t>(x) * 4U;
      auto* dst = image.at(x, y);
      for (std::size_t c = 0; c < 3U; ++c) {
        dst[c] = accum[3] > 0.000001 ? static_cast<float>(accum[c] / accum[3]) : 0.0F;
      }
      dst[3] = static_cast<float>(accum[3] / (taps * taps) * 255.0);
    }
    if (y + 1 >= height) {
      break;
    }
    build_h_row(y + r + 1, scratch.data());
    for (std::size_t i = 0; i < row_stride; ++i) {
      sum[i] += scratch[i];
    }
    build_h_row(y - r, scratch.data());
    for (std::size_t i = 0; i < row_stride; ++i) {
      sum[i] -= scratch[i];
    }
  }
  report_deep_filter_progress(progress, height, height, FilterProgressStage::Blurring);
}

void deep_gaussian_blur(DeepImage& image, double radius, const FilterProgress* progress) {
  if (image.width <= 0 || image.height <= 0) {
    return;
  }
  // The Gaussian Blur primitive's model: alpha and alpha-premultiplied color blur
  // separately, then color divides back out.
  auto alpha = channel_plane(image, 3U, false);
  filter_plane_with_photoshop_kernel(alpha, image.width, image.height, radius, PhotoshopLineKernel::Gaussian);
  report_deep_filter_progress(progress, 1, 4, FilterProgressStage::Blurring);
  for (std::size_t c = 0; c < 3U; ++c) {
    auto plane = channel_plane(image, c, true);
    filter_plane_with_photoshop_kernel(plane, image.width, image.height, radius, PhotoshopLineKernel::Gaussian);
    for (std::size_t i = 0; i < plane.size(); ++i) {
      const auto a = alpha[i];
      image.rgba[i * 4U + c] = a > 0.000001F ? plane[i] * 255.0F / a : 0.0F;
    }
    report_deep_filter_progress(progress, static_cast<std::int32_t>(c) + 2, 4, FilterProgressStage::Blurring);
  }
  for (std::size_t i = 0; i < alpha.size(); ++i) {
    image.rgba[i * 4U + 3U] = std::clamp(alpha[i], 0.0F, 255.0F);
  }
}

namespace {

// High Pass and Unsharp Mask blur straight color (alpha is kept as it is).
template <typename Combine>
void blur_straight_color(DeepImage& image, double radius, PhotoshopLineKernel kernel,
                         const FilterProgress* progress, FilterProgressStage stage, Combine&& combine) {
  for (std::size_t c = 0; c < 3U; ++c) {
    auto plane = channel_plane(image, c, false);
    filter_plane_with_photoshop_kernel(plane, image.width, image.height, radius, kernel);
    for (std::size_t i = 0; i < plane.size(); ++i) {
      auto& value = image.rgba[i * 4U + c];
      value = combine(value, plane[i]);
    }
    report_deep_filter_progress(progress, static_cast<std::int32_t>(c) + 1, 3, stage);
  }
}

}  // namespace

void deep_high_pass(DeepImage& image, double radius, const FilterProgress* progress) {
  if (image.width <= 0 || image.height <= 0) {
    return;
  }
  blur_straight_color(image, radius, PhotoshopLineKernel::HighPass, progress, FilterProgressStage::Sharpening,
                      [](float source, float low) { return std::clamp(source - low + 128.0F, 0.0F, 255.0F); });
}

void deep_unsharp_mask(DeepImage& image, double amount_percent, double radius, int threshold,
                       const FilterProgress* progress) {
  if (image.width <= 0 || image.height <= 0) {
    return;
  }
  const auto amount = static_cast<float>(amount_percent / 100.0);
  const auto limit = static_cast<float>(std::max(0, threshold));
  // Photoshop scales the detail first, then removes Threshold from the signed
  // adjustment (the RGBA8 primitive's order).
  blur_straight_color(image, radius, PhotoshopLineKernel::Unsharp, progress, FilterProgressStage::Sharpening,
                      [amount, limit](float source, float low) {
                        const auto scaled = (source - low) * amount;
                        const auto magnitude = std::abs(scaled);
                        if (magnitude <= limit) {
                          return source;
                        }
                        return source + (scaled < 0.0F ? -(magnitude - limit) : magnitude - limit);
                      });
}

void deep_sharpen(DeepImage& image, int amount_percent, const FilterProgress* progress) {
  const auto original = image;
  const auto amount = static_cast<float>(std::clamp(amount_percent, 0, 300)) / 100.0F;
  for (std::int32_t y = 0; y < image.height; ++y) {
    report_deep_filter_progress(progress, y, image.height, FilterProgressStage::Sharpening);
    for (std::int32_t x = 0; x < image.width; ++x) {
      const auto* center = original.at(x, y);
      const auto* left = original.at(std::max(0, x - 1), y);
      const auto* right = original.at(std::min(image.width - 1, x + 1), y);
      const auto* up = original.at(x, std::max(0, y - 1));
      const auto* down = original.at(x, std::min(image.height - 1, y + 1));
      auto* dst = image.at(x, y);
      for (std::size_t c = 0; c < 3U; ++c) {
        const auto sharpened = center[c] * 5.0F - left[c] - right[c] - up[c] - down[c];
        dst[c] = center[c] + (sharpened - center[c]) * amount;
      }
    }
  }
  report_deep_filter_progress(progress, image.height, image.height, FilterProgressStage::Sharpening);
}

void deep_radial_blur(DeepImage& image, int amount, int samples, double center_x, double center_y,
                      const FilterProgress* progress) {
  amount = std::clamp(amount, 0, 100);
  samples = std::clamp(samples, 4, 32);
  const auto original = image;
  const auto sweep = static_cast<double>(amount) * 3.6 * kDeepFilterPi / 180.0;
  for (std::int32_t y = 0; y < image.height; ++y) {
    report_deep_filter_progress(progress, y, image.height, FilterProgressStage::Blurring);
    for (std::int32_t x = 0; x < image.width; ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      DeepAccum accum;
      for (int sample = 0; sample < samples; ++sample) {
        const auto t = static_cast<double>(sample) / static_cast<double>(samples - 1) - 0.5;
        const auto angle = sweep * t;
        accumulate_sample(accum, original, center_x + dx * std::cos(angle) - dy * std::sin(angle),
                          center_y + dx * std::sin(angle) + dy * std::cos(angle));
      }
      write_accumulated(image.at(x, y), accum);
    }
  }
  report_deep_filter_progress(progress, image.height, image.height, FilterProgressStage::Blurring);
}

void deep_pixelate(DeepImage& image, int block_size, const FilterProgress* progress) {
  block_size = std::max(1, block_size);
  for (std::int32_t block_y = 0; block_y < image.height; block_y += block_size) {
    report_deep_filter_progress(progress, block_y, image.height, FilterProgressStage::Pixelating);
    const auto block_height = std::min(block_size, image.height - block_y);
    for (std::int32_t block_x = 0; block_x < image.width; block_x += block_size) {
      const auto block_width = std::min(block_size, image.width - block_x);
      DeepAccum accum;
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          accumulate_pixel(accum, image.at(x, y), 1.0);
        }
      }
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          write_accumulated(image.at(x, y), accum);
        }
      }
    }
  }
  report_deep_filter_progress(progress, image.height, image.height, FilterProgressStage::Pixelating);
}

void deep_emboss(DeepImage& image, int angle_degrees, int height, int amount, const FilterProgress* progress) {
  const auto original = image;
  const auto angle = static_cast<double>(angle_degrees) * kDeepFilterPi / 180.0;
  const auto distance = static_cast<double>(std::clamp(height, 1, 100));
  const auto offset_x = std::cos(angle) * distance;
  const auto offset_y = -std::sin(angle) * distance;
  const auto scale = static_cast<double>(std::clamp(amount, 0, 500)) / 100.0;
  for (std::int32_t y = 0; y < image.height; ++y) {
    report_deep_filter_progress(progress, y, image.height, FilterProgressStage::Embossing);
    for (std::int32_t x = 0; x < image.width; ++x) {
      const auto highlight =
          sampled_luminance(original, static_cast<double>(x) - offset_x, static_cast<double>(y) - offset_y);
      const auto shadow =
          sampled_luminance(original, static_cast<double>(x) + offset_x, static_cast<double>(y) + offset_y);
      const auto value = static_cast<float>(128.0 + (highlight - shadow) * scale);
      auto* px = image.at(x, y);
      px[0] = value;
      px[1] = value;
      px[2] = value;
    }
  }
  report_deep_filter_progress(progress, image.height, image.height, FilterProgressStage::Embossing);
}

void deep_twirl(DeepImage& image, int angle_degrees, int radius_percent, double center_x, double center_y,
                const FilterProgress* progress) {
  const auto original = image;
  const auto radius = std::max(1.0, static_cast<double>(std::min(image.width, image.height)) * 0.5 *
                                        static_cast<double>(std::clamp(radius_percent, 1, 100)) / 100.0);
  const auto angle = static_cast<double>(std::clamp(angle_degrees, -720, 720)) * kDeepFilterPi / 180.0;
  for (std::int32_t y = 0; y < image.height; ++y) {
    report_deep_filter_progress(progress, y, image.height, FilterProgressStage::Twisting);
    for (std::int32_t x = 0; x < image.width; ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance > radius) {
        continue;
      }
      const auto falloff = 1.0 - distance / radius;
      const auto source_angle = std::atan2(dy, dx) - angle * falloff * falloff;
      const auto source_x = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(std::lround(center_x + std::cos(source_angle) * distance)), 0, image.width - 1);
      const auto source_y = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(std::lround(center_y + std::sin(source_angle) * distance)), 0,
          image.height - 1);
      const auto* src = original.at(source_x, source_y);
      std::copy(src, src + 4, image.at(x, y));
    }
  }
  report_deep_filter_progress(progress, image.height, image.height, FilterProgressStage::Twisting);
}

void deep_wave(DeepImage& image, int amplitude, int wavelength, int phase, const FilterProgress* progress) {
  const auto original = image;
  amplitude = std::clamp(amplitude, 0, 999);
  wavelength = std::clamp(wavelength, 4, 999);
  const auto phase_radians = static_cast<double>(std::clamp(phase, 0, 360)) * kDeepFilterPi / 180.0;
  const auto frequency = 2.0 * kDeepFilterPi / static_cast<double>(wavelength);
  for (std::int32_t y = 0; y < image.height; ++y) {
    report_deep_filter_progress(progress, y, image.height, FilterProgressStage::Distorting);
    for (std::int32_t x = 0; x < image.width; ++x) {
      const auto source_x = static_cast<double>(x) + std::sin(static_cast<double>(y) * frequency + phase_radians) *
                                                         static_cast<double>(amplitude);
      const auto source_y =
          static_cast<double>(y) + std::sin(static_cast<double>(x) * frequency + phase_radians + kDeepFilterPi * 0.5) *
                                       static_cast<double>(amplitude) * 0.5;
      DeepAccum accum;
      accumulate_sample(accum, original, source_x, source_y);
      write_accumulated(image.at(x, y), accum);
    }
  }
  report_deep_filter_progress(progress, image.height, image.height, FilterProgressStage::Distorting);
}

void deep_pinch_bloat(DeepImage& image, int amount, int radius_percent, double center_x, double center_y,
                      const FilterProgress* progress) {
  const auto original = image;
  const auto radius = std::max(1.0, static_cast<double>(std::min(image.width, image.height)) * 0.5 *
                                        static_cast<double>(std::clamp(radius_percent, 1, 100)) / 100.0);
  const auto strength = static_cast<double>(std::clamp(amount, -100, 100)) / 100.0;
  for (std::int32_t y = 0; y < image.height; ++y) {
    report_deep_filter_progress(progress, y, image.height, FilterProgressStage::Distorting);
    for (std::int32_t x = 0; x < image.width; ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance <= 0.0001 || distance > radius) {
        continue;
      }
      const auto normalized = distance / radius;
      const auto falloff = (1.0 - normalized) * (1.0 - normalized);
      const auto source_distance = std::clamp(distance * (1.0 - strength * falloff * 0.75), 0.0, radius);
      const auto sample_scale = source_distance / distance;
      DeepAccum accum;
      accumulate_sample(accum, original, center_x + dx * sample_scale, center_y + dy * sample_scale);
      write_accumulated(image.at(x, y), accum);
    }
  }
  report_deep_filter_progress(progress, image.height, image.height, FilterProgressStage::Distorting);
}

}  // namespace patchy
