#pragma once

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/rect_utils.hpp"

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace patchy {

// When the document is a single flat pixel layer carrying an enabled grayscale mask,
// returns an RGBA8 buffer whose colors are the layer's ORIGINAL (unmasked) pixels and
// whose alpha channel is the mask. Saving this to an alpha-capable format preserves the
// mask non-destructively (the colors beneath the mask are kept, matching how Photoshop
// shows an opaque Background plus a separate "Alpha 1" channel). Returns nullopt when the
// document is not a single masked pixel layer.
[[nodiscard]] std::optional<PixelBuffer> document_alpha_rgba8(const Document& document);

[[nodiscard]] Rect outset_rect(Rect rect, int amount) noexcept;
[[nodiscard]] Rect clipped_mask_bounds(Rect full_bounds, Rect draw_rect, int sample_padding) noexcept;
[[nodiscard]] Rect layer_pixel_bounds(const Layer& layer);
[[nodiscard]] std::optional<Rect> visible_alpha_local_bounds(const PixelBuffer& pixels);
[[nodiscard]] std::optional<Rect> visible_alpha_local_bounds(const Layer& layer);
[[nodiscard]] std::optional<Rect> layer_visible_alpha_bounds(const PixelBuffer& pixels, Rect bounds);
[[nodiscard]] std::optional<Rect> layer_visible_alpha_bounds(const Layer& layer, Rect bounds);
[[nodiscard]] std::optional<Rect> layer_visible_alpha_bounds(const Layer& layer, const PixelBuffer& pixels,
                                                             Rect bounds);
[[nodiscard]] int layer_style_effect_padding(const LayerStyle& style) noexcept;
[[nodiscard]] int layer_effect_padding(const Layer& layer) noexcept;
[[nodiscard]] int document_effect_padding(const Document& document) noexcept;
[[nodiscard]] Rect layer_bounds_with_effects(const Layer& layer, Rect bounds) noexcept;
[[nodiscard]] Rect layer_render_bounds(const Layer& layer) noexcept;
// A group whose own layer style should render (kind Group, effects visible,
// style non-empty). Style-less groups keep the historical composite paths.
[[nodiscard]] bool group_style_renders(const Layer& layer) noexcept;
struct AncestorGroupStyleInfo {
  // Summed styled-ancestor padding: the compositor outsets by each styled
  // group's own padding at every nesting level (layer_render_bounds_for_render),
  // so nested styled groups add, unlike layer_effect_padding's max.
  int effect_padding{0};
  bool styled{false};
};
// Styled-ancestor info for every non-group layer that sits under at least one
// group whose style renders; layers with no styled ancestor are absent. Used by
// move/nudge dirty-region and preview-cost calculations, which otherwise see
// only each leaf's own style.
[[nodiscard]] std::unordered_map<LayerId, AncestorGroupStyleInfo> collect_ancestor_group_style_info(
    const std::vector<Layer>& layers);

// PREVIEW-ONLY display-resolution compositing support. `preview_scaled_dimension`
// is `level` successive ceil-halvings (matching the display mip math);
// `downscale_pixel_buffer_by_level` box-halves a gray8/rgb8/rgba8 buffer that
// many times (alpha-weighted for rgba so transparent texels do not bleed dark
// fringes); `build_preview_scaled_document` returns a copy of the document with
// every raster surface downscaled by 2^level and coordinates plus style
// falloffs (shadow/glow/stroke/satin/bevel sizes and distances, vector-mask
// feather) scaled to match, layer ids and structure preserved. Blend math is
// nonlinear, so compositing the scaled copy and upscaling is NOT
// byte-comparable to downscaling a full-res composite: consumers must treat
// the result as an interactive preview and re-render full-res on
// commit/release. Pattern fills keep their full-res tiles (phase/scale drift
// is part of the preview contract).
[[nodiscard]] std::int32_t preview_scaled_dimension(std::int32_t value, int level) noexcept;
[[nodiscard]] PixelBuffer downscale_pixel_buffer_by_level(const PixelBuffer& source, int level);
// One level and one entry per current layer surface. Keeping the COW source
// alive makes storage identity safe against allocator reuse and forces later
// edits to detach. Metadata is always rebuilt from the current document.
struct PreviewScaleCache {
  struct Surface { PixelBuffer source; PixelBuffer scaled; };
  int level{0};
  std::unordered_map<LayerId, std::array<Surface, 3>> layers;
};
[[nodiscard]] Document build_preview_scaled_document(const Document& document, int level,
                                                     PreviewScaleCache* cache = nullptr);
// Recompute `scaled`'s position-bearing fields (bounds, raster-mask bounds,
// vector shape/mask with the downscaled cache and feather kept) from the
// translated full-res `real` layer, exactly as a fresh
// `build_preview_scaled_document` of the committed document would produce
// them. The downscaled buffers are position-independent (the box-downscale
// halves each buffer, not a document-space grid), so a pure translation only
// moves coordinates. Buffers are reused as-is; `real` must be the same layer
// the scaled copy was built from, differing only by translation.
void retarget_preview_scaled_layer_bounds(Layer& scaled, const Layer& real, int level);
[[nodiscard]] bool layer_style_preview_is_expensive(const Layer& layer, Rect document_bounds) noexcept;
// Vector-mask coverage sample (1.0 when absent/disabled; density folds in).
// Both layer_mask_alpha_at overloads already multiply this in, so every mask
// consumer that funnels through them gets vector masks automatically.
[[nodiscard]] float vector_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y);
// Gate helpers for fast paths that skip per-pixel mask sampling (defined here
// so headers do not need the complete LayerVectorMask type).
[[nodiscard]] bool layer_has_enabled_vector_mask(const Layer& layer) noexcept;
[[nodiscard]] bool layer_vector_mask_hides_effects(const Layer& layer) noexcept;
// Which of a layer's masks a mask sample includes. Layer effects split them by
// the "Layer Mask Hides Effects" / "Vector Mask Hides Effects" options: a mask
// with its option off shapes the effect sources, one with it on hides the
// effect output where it lands instead (Photoshop 2026 COM captures, October
// 2026, docs/layer-effects-render.md).
struct LayerMaskParts {
  bool raster{true};
  bool vector{true};
};
[[nodiscard]] LayerMaskParts layer_effect_source_masks(const Layer& layer) noexcept;
[[nodiscard]] LayerMaskParts layer_effect_output_masks(const Layer& layer) noexcept;
// Photoshop's mask feather (raster and vector alike, PS 27.9 calibration in
// docs/vector-psd-encodings.md) is a gaussian of sigma = feather pixels. These are its
// deterministic three-box approximation: the box radii for a sigma, and the
// edge-clamped horizontal+vertical passes over a 16-bit plane (0..65535).
[[nodiscard]] std::array<std::int32_t, 3> mask_feather_box_radii(double sigma) noexcept;
void mask_feather_blur(std::vector<std::uint16_t>& plane, std::int32_t width, std::int32_t height,
                       const std::array<std::int32_t, 3>& radii);
// The raster mask as rendered when its feather parameter is active: the
// painted plane blurred to a gaussian of sigma = feather pixels (three box
// passes; Photoshop 27.9 calibration, docs/vector-psd-encodings.md), edge-clamped at
// LayerMask::feather_canvas. The plane sits at mask.bounds origin + offset and
// reads default_color beyond it. Null when the feather is inactive. Results
// are cached on content_revision, so repaints never re-blur.
struct FeatheredLayerMask {
  std::int32_t offset_x{0};
  std::int32_t offset_y{0};
  PixelBuffer pixels{};  // gray8
};
[[nodiscard]] std::shared_ptr<const FeatheredLayerMask> feathered_layer_mask(const Layer& layer);
// Points every feathered raster mask in the subtree at `canvas` (no revision
// bump when nothing changes).
void sync_layer_mask_feather_canvas(Layer& layer, Rect canvas);
// Both overloads fold in the raster mask's density and feather.
[[nodiscard]] float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y);
[[nodiscard]] float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y, Rect mask_bounds);
// The same samples limited to `parts` (both parts: identical to the above).
[[nodiscard]] float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y, LayerMaskParts parts);
[[nodiscard]] float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y, Rect mask_bounds,
                                        LayerMaskParts parts);
[[nodiscard]] std::vector<float> layer_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                                  Rect mask_bounds, std::int32_t sample_offset_x = 0,
                                                  std::int32_t sample_offset_y = 0,
                                                  std::optional<Rect> layer_mask_bounds = std::nullopt);
[[nodiscard]] std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds,
                                                  std::int32_t sample_offset_x = 0, std::int32_t sample_offset_y = 0,
                                                  std::optional<Rect> layer_mask_bounds = std::nullopt);

}  // namespace patchy
