#pragma once

#include "core/document.hpp"
#include "core/rect_utils.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace photoslop {

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
  bool fill_shapes{false};
  bool lock_transparent_pixels{false};
  std::optional<Rect> selection;
  std::function<bool(std::int32_t, std::int32_t)> selection_mask;
  std::function<float(std::int32_t, std::int32_t)> selection_coverage;
  std::function<bool(std::int32_t, std::int32_t)> stroke_pixel_gate;
  std::function<float(std::int32_t, std::int32_t, float)> stroke_coverage_gate;
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
[[nodiscard]] Rect paint_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0,
                                       std::int32_t x1, std::int32_t y1, const EditOptions& options, bool erase);
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
[[nodiscard]] Rect clear_rect(Document& document, LayerId layer_id, Rect rect, const EditOptions& options);
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

}  // namespace photoslop
