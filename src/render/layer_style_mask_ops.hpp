#pragma once

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pattern_resource.hpp"
#include "core/pattern_sampler.hpp"
#include "core/style_contour.hpp"
#include "render/layer_render_types.hpp"

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

inline float layer_mask_alpha_for_render(const Layer& layer, std::int32_t x, std::int32_t y,
                                         std::optional<Rect> mask_bounds) {
  return mask_bounds.has_value() ? layer_mask_alpha_at(layer, x, y, *mask_bounds) : layer_mask_alpha_at(layer, x, y);
}

// The masks that shape an effect's contour: those whose "Hides Effects"
// option is off (layer_effect_source_masks).
inline float effect_source_mask_alpha_for_render(const Layer& layer, std::int32_t x, std::int32_t y,
                                                 std::optional<Rect> mask_bounds) {
  const auto parts = layer_effect_source_masks(layer);
  return mask_bounds.has_value() ? layer_mask_alpha_at(layer, x, y, *mask_bounds, parts)
                                 : layer_mask_alpha_at(layer, x, y, parts);
}

// The masks that hide effect output where it lands: those whose "Hides
// Effects" option is on. Applied only where layer_mask_clips_effect_output.
inline float effect_output_mask_alpha_for_render(const Layer& layer, std::int32_t x, std::int32_t y,
                                                 std::optional<Rect> mask_bounds) {
  const auto parts = layer_effect_output_masks(layer);
  return mask_bounds.has_value() ? layer_mask_alpha_at(layer, x, y, *mask_bounds, parts)
                                 : layer_mask_alpha_at(layer, x, y, parts);
}

// The extra planes stroke_alpha_mask produces beside its over-draw mask when asked.
struct StrokeMaskPlanes {
  std::vector<float>* shape_burst_positions{nullptr};
  std::vector<float>* underlay{nullptr};
  std::vector<float>* inside{nullptr};
  std::vector<float>* knockout{nullptr};
  bool overprint{false};
  bool partial_content{false};
};

std::vector<float> stroke_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                            Rect mask_bounds, float size, LayerStrokePosition position,
                                            std::optional<Rect> layer_mask_bounds, bool mask_shapes_source,
                                            std::vector<float>* shape_burst_positions = nullptr,
                                            StrokeMaskPlanes* planes = nullptr);


void max_filter_row(const std::vector<float>& input, std::vector<float>& output,
                    std::vector<int>& candidates, int width, int source_y, int radius);

std::vector<float> dilate_mask(const std::vector<float>& input, int width, int height, int radius);

// `arg` (optional, size n) receives the index of the sample each output's distance
// came from: the EDT's nearest source along this axis.
void squared_distance_transform_1d(const float* f, float* d, int* v, double* z, int n, int* arg = nullptr);

// Exact squared Euclidean distance transform. On entry `field` holds 0 at source
// pixels and a large sentinel (>= kEdtUnreached) elsewhere; on return every pixel
// holds the exact squared distance to its nearest source (sentinel-sized where no
// source exists at all).
constexpr float kEdtUnreached = 1.0e20F;

void exact_squared_distance_transform(std::vector<float>& field, int width, int height);
// The same transform that also reports, per pixel, the linear index of the nearest
// source pixel (-1 where no source exists). Ties resolve through the lower envelope's
// parabola order, deterministically across toolchains.
void exact_squared_distance_transform(std::vector<float>& field, int width, int height,
                                      std::vector<int>* sources);

std::vector<float> stroke_distance_field(const std::vector<float>& base, int width, int height,
                                         bool sources_are_painted);

// Distance from every pixel center to the nearest painted (alpha > 0) or, with
// `sources_are_painted` false, non-solid (alpha < 1) pixel center of `base`, plus that
// pixel's alpha in `edge_alpha` (0 or 1 where no source exists). The Photoshop stroke
// band's anchor and last-ring value (docs/layer-effects-render.md "Stroke").
void stroke_distance_field_with_edge_alpha(const std::vector<float>& base, int width, int height,
                                           bool sources_are_painted, std::vector<float>& distance,
                                           std::vector<float>& edge_alpha);

// Shape Burst ramp distance fields anchored at the matte's SUBPIXEL half-coverage
// crossing (the band itself uses stroke_distance_field_with_edge_alpha): the matte is supersampled
// 3x (bilinear; the odd scale keeps every coarse pixel center exactly on a
// fine center), thresholded at 0.5 in the fine grid, EDT'd exactly, and read
// back at the coarse centers with a +1/3 px compensation chosen so a BINARY
// matte reproduces the calibrated pixel-center convention exactly on straight
// edges. Anti-aliased edges gain a continuous contour anchor (quantized at
// 1/3 px) instead of the whole-pixel staircase a 0.5 threshold produces.
// Painted-side pixels (matte >= 0.5 at the coarse center) read 0 in `outside`
// and vice versa, like the binary fields. Pinned by the
// photoshop-stroke-aa-matte fixtures (July 2026).
void stroke_subpixel_distance_fields(const std::vector<float>& matte, int width, int height,
                                     bool need_outside, bool need_inside,
                                     std::vector<float>& outside, std::vector<float>& inside);

// The binary contour sits half a pixel past the last source pixel center, and the
// 1px anti-aliasing ramp is centered on the band edge, so a band reaching `band`
// pixels from the contour fully covers center distances d <= band and fades out by
// d = band + 1. Calibrated against Photoshop 2026: integer sizes on axis-aligned
// edges reproduce the legacy binary dilation exactly.
constexpr float kStrokeContourOffset = 1.0F;

float stroke_band_coverage(float distance, float band) noexcept;

void box_blur_mask_into(const std::vector<float>& input, std::vector<float>& horizontal,
                        std::vector<float>& output, int width, int height, int radius);

// Long ("continuous") drop shadow sweep: every pixel takes the maximum of the
// matte samples on the digital line behind it, from its own position back to
// the full offset, so the matte is extruded along (offset_x, offset_y). The
// grid is partitioned into digital lines along the major axis (each pixel is
// visited exactly once) and each line runs a monotone-deque trailing maximum
// of max(|offset_x|, |offset_y|) + 1 samples, so the cost is O(width * height)
// and independent of the offset length. `fade_percent` (0..100) scales the
// swept value by 1 - fade * t / L, where t is the distance along the line since
// the last matte sample above zero: 0 keeps the sweep uniform, 100 fades it to
// nothing by the far end. Integer bookkeeping only; a zero offset is a no-op.
void sweep_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, int offset_x,
                                     int offset_y, float fade_percent);

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

void prepare_outer_glow_softer_mask(std::vector<float>& mask, int width, int height, float size, float spread,
                                    float range);

void prepare_photoshop_interior_soft_mask(std::vector<float>& mask, int width, int height, float size,
                                          float choke);

void prepare_inner_glow_softer_mask(std::vector<float>& mask, int width, int height, float size,
                                    float choke, float range, bool center_source);

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
