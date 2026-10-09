#pragma once

// Filters on 16 and 32-bit layers (docs/high-bit-depth.md). Internal to src/filters:
// filter_engine.cpp dispatches a deep buffer here.
//
// Kernels run on a DeepImage: straight RGBA floats on the deep scale (255 = full) in
// the buffer's own domain (display-encoded for 16 bits, linear light for 32 bits). They
// are the 8-bit filters' math without the byte rounding between stages, so a 16-bit
// result narrowed to 8 bits stays within a level or two of the 8-bit filter.

#include "core/pixel_buffer.hpp"
#include "core/pixel_depth.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace patchy {

struct DeepImage {
  std::int32_t width{0};
  std::int32_t height{0};
  DeepDomain domain{DeepDomain::Encoded};
  std::vector<float> rgba;

  [[nodiscard]] float* at(std::int32_t x, std::int32_t y) {
    return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(x)) *
                             4U;
  }
  [[nodiscard]] const float* at(std::int32_t x, std::int32_t y) const {
    return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(x)) *
                             4U;
  }
};

[[nodiscard]] DeepImage load_deep_image(const PixelBuffer& pixels);
// Writes RGB (and alpha when `pixels` has it) back. Encoded color clamps to 0..255;
// linear color keeps values above 255 but not below 0.
void store_deep_image(PixelBuffer& pixels, const DeepImage& image);

// Row progress for the kernels below; throws FilterCancelled when the user cancels.
void report_deep_filter_progress(const FilterProgress* progress, std::int32_t completed, std::int32_t total,
                                 FilterProgressStage stage);

// dst = original * (100 - amount) + dst * amount, per color channel; alpha from original.
void deep_blend_with_original(DeepImage& image, const DeepImage& original, int amount_percent);

void deep_box_blur(DeepImage& image, int radius, const FilterProgress* progress);
void deep_gaussian_blur(DeepImage& image, double radius, const FilterProgress* progress);
void deep_high_pass(DeepImage& image, double radius, const FilterProgress* progress);
void deep_unsharp_mask(DeepImage& image, double amount_percent, double radius, int threshold,
                       const FilterProgress* progress);
void deep_sharpen(DeepImage& image, int amount_percent, const FilterProgress* progress);
// Centers are buffer coordinates.
void deep_radial_blur(DeepImage& image, int amount, int samples, double center_x, double center_y,
                      const FilterProgress* progress);
void deep_pixelate(DeepImage& image, int block_size, const FilterProgress* progress);
void deep_emboss(DeepImage& image, int angle_degrees, int height, int amount, const FilterProgress* progress);
void deep_twirl(DeepImage& image, int angle_degrees, int radius_percent, double center_x, double center_y,
                const FilterProgress* progress);
void deep_wave(DeepImage& image, int amplitude, int wavelength, int phase, const FilterProgress* progress);
void deep_pinch_bloat(DeepImage& image, int amount, int radius_percent, double center_x, double center_y,
                      const FilterProgress* progress);

}  // namespace patchy
