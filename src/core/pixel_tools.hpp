#pragma once

#include "core/brush_tip.hpp"
#include "core/document.hpp"
#include "core/palette.hpp"
#include "core/rect_utils.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace patchy {

struct EditColor {
  std::uint8_t r{0};
  std::uint8_t g{0};
  std::uint8_t b{0};
  std::uint8_t a{255};
};

struct EditOptions {
  EditColor primary{};
  EditColor secondary{255, 255, 255, 255};
  int brush_size{12};
  int brush_softness{0};
  int brush_roundness{100};
  double brush_angle_degrees{0.0};
  const ScaledBrushTip* brush_tip{nullptr};  // non-owning; null = procedural round/soft brush
  double brush_tip_spacing{0.25};            // dab spacing as a fraction of brush_size
  BrushDynamics brush_dynamics{};            // per-dab tip dynamics; default = disabled
  bool fill_shapes{false};
  int shape_corner_radius{0};
  double fill_softness_feather{0.0};  // fill_rect: inward edge feather band (px); 0 = hard edge
  bool lock_transparent_pixels{false};
  // Palette-mode write constraint (non-owning, caller keeps the LUT alive for the
  // operation). When set, pixel writes binarize coverage at its threshold, blend
  // at full strength, then snap RGB to the palette and alpha to 0/255. Null =
  // the historical write path, bit for bit.
  const PaletteSnapContext* palette_snap{nullptr};
  std::optional<Rect> selection;
  std::vector<Rect> selection_scan_rects;
  std::function<bool(std::int32_t, std::int32_t)> selection_mask;
  std::function<float(std::int32_t, std::int32_t)> selection_coverage;
  std::function<bool(std::int32_t, std::int32_t)> stroke_pixel_gate;
  std::function<bool(std::int32_t, std::int32_t, std::uint8_t*, std::uint16_t, float)> stroke_pixel_writer;
  std::function<void()> progress_callback;
};

enum class ShapeKind {
  Rectangle,
  Ellipse
};

// Pure geometric coverage of a shape pixel, in [0,1]. Shared by the pixel-layer renderer and the
// layer-mask renderer so both produce identical antialiased / soft / rounded shapes. Carries no
// Document/PixelBuffer dependency.
struct ShapeCoverageParams {
  ShapeKind kind{ShapeKind::Rectangle};
  bool fill{false};
  double cx{0.0};               // shape center (document pixels)
  double cy{0.0};
  double rx{1.0};               // ellipse radii / rectangle half-extents
  double ry{1.0};
  double corner_radius{0.0};    // rounded-rect radius (px); ignored for ellipse
  double half_thickness{1.0};   // outline ring half-width (= brush_size/2)
  double band{1.0};             // antialias + softness band width (px)
};

[[nodiscard]] ShapeCoverageParams make_shape_coverage_params(Rect rect, const EditOptions& options,
                                                            ShapeKind kind);
[[nodiscard]] float shape_pixel_coverage(const ShapeCoverageParams& params, std::int32_t x,
                                         std::int32_t y) noexcept;

enum class GradientMethod {
  Linear,
  Radial
};

struct GradientStop {
  float location{0.0F};
  EditColor color{};
};

struct GradientOptions {
  GradientMethod method{GradientMethod::Linear};
  bool reverse{false};
  float opacity{1.0F};
  std::vector<GradientStop> stops;
};

struct SmudgeState {
  std::int32_t diameter{0};
  bool initialized{false};
  std::vector<std::uint8_t> sample_rgba;
};

enum class CanvasAnchor {
  TopLeft,
  Top,
  TopRight,
  Left,
  Center,
  Right,
  BottomLeft,
  Bottom,
  BottomRight
};

[[nodiscard]] Rect paint_brush(Document& document, LayerId layer_id, std::int32_t x, std::int32_t y,
                               const EditOptions& options, bool erase);
[[nodiscard]] Rect paint_brush_dab(Document& document, LayerId layer_id, double x, double y,
                                   const EditOptions& options, bool erase);
// One stationary Airbrush timer tick. Active shape/Transfer dynamics advance through the
// stroke state, while scatter/count are deliberately suppressed so a held pointer remains one
// flat 2D stamp rather than a particle burst. Falls back to paint_brush_dab without dynamics.
[[nodiscard]] Rect paint_stationary_airbrush_dab(Document& document, LayerId layer_id, double x,
                                                 double y, const EditOptions& options,
                                                 BrushTipStrokeState& state);
[[nodiscard]] Rect paint_brush_segment(Document& document, LayerId layer_id, double x0, double y0, double x1,
                                       double y1, const EditOptions& options, bool erase);
[[nodiscard]] Rect paint_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0,
                                       std::int32_t x1, std::int32_t y1, const EditOptions& options, bool erase);
// Stateful overload for bitmap brush tips: carries dab spacing across chained segments so the
// canvas stroke smoother's short steps do not cluster dabs at joins. Falls back to the
// procedural path when options.brush_tip is null.
[[nodiscard]] Rect paint_brush_segment(Document& document, LayerId layer_id, double x0, double y0, double x1,
                                       double y1, const EditOptions& options, bool erase,
                                       BrushTipStrokeState& state);
[[nodiscard]] Rect smudge_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0,
                                        std::int32_t x1, std::int32_t y1, const EditOptions& options);
[[nodiscard]] Rect smudge_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0,
                                        std::int32_t x1, std::int32_t y1, const EditOptions& options,
                                        SmudgeState& state);
[[nodiscard]] Rect draw_line(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                             std::int32_t y1, const EditOptions& options, bool erase);
[[nodiscard]] Rect draw_rectangle(Document& document, LayerId layer_id, Rect rect, const EditOptions& options,
                                  bool erase);
[[nodiscard]] Rect draw_ellipse(Document& document, LayerId layer_id, Rect rect, const EditOptions& options,
                                bool erase);
[[nodiscard]] Rect flood_fill(Document& document, LayerId layer_id, std::int32_t x, std::int32_t y,
                              const EditOptions& options);
[[nodiscard]] Rect fill_rect(Document& document, LayerId layer_id, Rect rect, const EditOptions& options);
[[nodiscard]] Rect clear_rect_change_bounds(const Document& document, LayerId layer_id, Rect rect,
                                            const EditOptions& options);
[[nodiscard]] Rect clear_rect(Document& document, LayerId layer_id, Rect rect, const EditOptions& options);
[[nodiscard]] std::vector<GradientStop> normalized_gradient_stops(const std::vector<GradientStop>& stops);
[[nodiscard]] EditColor gradient_color_at(const std::vector<GradientStop>& sorted_stops, float opacity, bool reverse,
                                          double position);
[[nodiscard]] Rect draw_gradient(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0,
                                 std::int32_t x1, std::int32_t y1, const EditOptions& options,
                                 const GradientOptions& gradient);
[[nodiscard]] Rect draw_linear_gradient(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0,
                                        std::int32_t x1, std::int32_t y1, const EditOptions& options);
void expand_layer_to_include_rect(Layer& layer, Rect document_rect);
[[nodiscard]] Rect flip_layer_horizontal(Document& document, LayerId layer_id);
[[nodiscard]] Rect flip_layer_vertical(Document& document, LayerId layer_id);
void resize_image_and_layers(Document& document, std::int32_t width, std::int32_t height);
void resize_canvas_and_layers(Document& document, std::int32_t width, std::int32_t height,
                              CanvasAnchor anchor = CanvasAnchor::TopLeft,
                              EditColor extension_color = EditColor{255, 255, 255, 255});
[[nodiscard]] bool crop_document(Document& document, Rect crop);
void rotate_document_clockwise(Document& document);
void rotate_document_counterclockwise(Document& document);

}  // namespace patchy
