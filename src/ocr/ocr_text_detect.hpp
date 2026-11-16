#pragma once

// Text line detection for a model engine (docs/ocr-engines.md, "PP-OCR"): the picture a
// detector network is given, and the rectangles read off the probability map it returns.
// No network runs here; src/ai runs it and hands the map over.
//
// Binding rules (the "Text recognition" entry of docs/legal-constraints-scanning.md): the
// only thing thresholded is the network's own probability map, never the image; a region of
// that map becomes one rotated rectangle for one line, never a free quadrilateral and never
// a box per character; and the map is not dilated before its regions are found.

#include "ocr/ocr_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace patchy::ocr {

// The detector's input for an image: the image scaled down so its long side is at most
// `long_side_cap` (never scaled up), then padded with white on the right and bottom to
// multiples of 32, which the network needs.
struct DetectorInput {
  std::int32_t scaled_width{0};   // the image as the network sees it
  std::int32_t scaled_height{0};
  std::int32_t tensor_width{0};   // with the padding
  std::int32_t tensor_height{0};
  // One float per channel and pixel, channels first (blue, green, red), each
  // (value / 255 - mean) / deviation with the ImageNet statistics the PP-OCR detectors use.
  std::vector<float> values;
};

inline constexpr std::int32_t kDetectorLongSide = 1536;

[[nodiscard]] DetectorInput detector_input(const ImageView& image, std::int32_t long_side_cap = kDetectorLongSide);

struct TextDetectSettings {
  float threshold{0.2F};       // a map pixel belongs to text above this
  float box_threshold{0.45F};  // a region is kept when its mean probability reaches this
  double unclip_ratio{1.4};    // each side grows by area * ratio / perimeter
  double min_side{3.0};        // regions thinner than this many map pixels are dropped
  std::size_t max_boxes{3000};
};

struct DetectedText {
  // In image pixels, in the frame closest to upright: top left, top right, bottom right,
  // bottom left.
  Quad quad;
  // Clockwise turn of that frame, y down, in (-45, 45].
  double angle_degrees{0.0};
  double score{0.0};
};

// The text regions of a probability map of `width` x `height` (row-major), mapped to image
// pixels by `scale_x` and `scale_y` (image pixels per map pixel). In the order their first
// pixel is met, rows top to bottom. Deterministic: integer region growing, and one fixed
// rule for the rectangle (the smallest area over the hull's edge directions, the first such
// edge winning a tie).
[[nodiscard]] std::vector<DetectedText> detected_text(std::span<const float> probability, std::int32_t width,
                                                      std::int32_t height, double scale_x, double scale_y,
                                                      const TextDetectSettings& settings = {});

}  // namespace patchy::ocr
