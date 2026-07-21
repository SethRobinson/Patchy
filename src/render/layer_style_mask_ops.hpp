#pragma once

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pattern_resource.hpp"
#include "core/pattern_sampler.hpp"
#include "core/style_contour.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Non-template layer-style mask machinery used by the reference compositor's
// render templates (render/layer_compositor.hpp includes this header) and
// called directly by the mask-focused core tests. Definitions live in
// layer_style_mask_ops.cpp. All mask math and iteration order are byte-pinned
// by the compositor canaries; keep bodies verbatim when reorganizing.
namespace patchy::render_detail {

struct StyleMaskEntry;
class StyleMaskProvider;

void max_filter_row(const std::vector<float>& input, std::vector<float>& output,
                    std::vector<int>& candidates, int width, int source_y, int radius);

std::vector<float> dilate_mask(const std::vector<float>& input, int width, int height, int radius);

void squared_distance_transform_1d(const float* f, float* d, int* v, double* z, int n);

// Exact squared Euclidean distance transform. On entry `field` holds 0 at source
// pixels and a large sentinel (>= kEdtUnreached) elsewhere; on return every pixel
// holds the exact squared distance to its nearest source (sentinel-sized where no
// source exists at all).
constexpr float kEdtUnreached = 1.0e20F;

void exact_squared_distance_transform(std::vector<float>& field, int width, int height);

std::vector<float> stroke_distance_field(const std::vector<float>& base, int width, int height,
                                         bool sources_are_painted);

// The binary contour sits half a pixel past the last source pixel center, and the
// 1px anti-aliasing ramp is centered on the band edge, so a band reaching `band`
// pixels from the contour fully covers center distances d <= band and fades out by
// d = band + 1. Calibrated against Photoshop 2026: integer sizes on axis-aligned
// edges reproduce the legacy binary dilation exactly.
constexpr float kStrokeContourOffset = 1.0F;

float stroke_band_coverage(float distance, float band) noexcept;

void box_blur_mask_into(const std::vector<float>& input, std::vector<float>& horizontal,
                        std::vector<float>& output, int width, int height, int radius);

void blur_mask_in_place(std::vector<float>& mask, int width, int height, int radius, int passes);

int layer_style_falloff_radius(float size) noexcept;

void blur_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float size);

void expand_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float radius,
                                      float pixels_per_unit);

int layer_style_mask_supersample_scale(int width, int height, float size) noexcept;

float mask_sample_or_zero(const std::vector<float>& mask, int width, int height, int x, int y) noexcept;

float bilinear_mask_sample(const std::vector<float>& mask, int width, int height, float x, float y) noexcept;

std::vector<float> supersampled_layer_style_mask(const std::vector<float>& mask, int width, int height,
                                                 int scale);

void downsample_layer_style_mask(const std::vector<float>& scaled, std::vector<float>& mask, int width,
                                 int height, int scale);

void prepare_layer_style_soft_mask(std::vector<float>& mask, int width, int height, float size, float spread);

int interior_style_blur_radius(float size) noexcept;

void prepare_layer_style_interior_falloff_mask(std::vector<float>& mask, int width, int height, float size,
                                               float choke);

float smoothstep_unit(float value) noexcept;

float layer_style_falloff_alpha(float distance, float size, float spread) noexcept;

void relax_distance(float& distance, float& strength, float candidate_distance,
                    float candidate_strength) noexcept;

std::vector<float> layer_style_source_strengths(const std::vector<float>& input, int width, int height);

void chamfer_distance_and_strengths(const std::vector<float>& input, int width, int height,
                                    std::vector<float>& distances, std::vector<float>& strengths);

std::vector<float> distance_falloff_mask(const std::vector<float>& input, int width, int height,
                                         float size, float spread);

std::vector<float> bevel_technique_height_mask(const std::vector<float>& alpha_mask, int width, int height,
                                               const LayerBevelEmboss& bevel);

int satin_tent_peak(float size) noexcept;

void blur_satin_tent_mask_in_place(std::vector<float>& mask, int width, int height, float size);

std::vector<float> satin_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                    Rect mask_bounds, int offset_x, int offset_y, float size, bool invert,
                                    std::optional<Rect> layer_mask_bounds);

struct PreparedSatin {
  const LayerSatin* effect{nullptr};
  std::shared_ptr<const StyleMaskEntry> entry;
  Rect mask_bounds{};
};

PreparedSatin prepare_satin(const Layer& layer, const PixelBuffer& source, Rect draw_rect, Rect bounds,
                            const LayerSatin& satin, std::optional<Rect> layer_mask_bounds,
                            StyleMaskProvider* masks, std::uint32_t effect_index);

}  // namespace patchy::render_detail
