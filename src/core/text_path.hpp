#pragma once

// Text along a path (docs/path-text.md). A type layer owns a copy of one open or closed Bezier
// contour in text-local coordinates (kLayerMetadataTextPath) and a start/end range on it
// (kLayerMetadataTextPathRange). The first line of the story is set along the contour between
// the two range points: each glyph sits with its baseline on the path, turned to the path's
// tangent at the middle of its advance. Editing the source shape or path never changes the copy.

#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_shape.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

// The range is in Photoshop's units (Txt2 frame key /6, Action Manager `tRange`): the whole
// part names a cubic segment of the contour, the fraction is the Bezier parameter inside it.
// An open contour of N segments holds 0 <= start < end <= N. On a closed one 0 <= start < N and
// start < end <= start + N: the text may run past the seam, but never more than once around.
struct TextPathRange {
  double start{0.0};
  double end{0.0};

  friend bool operator==(const TextPathRange&, const TextPathRange&) = default;
};

// A contour and the range of it the text is set in.
struct TextPathFrame {
  VectorPath path;
  TextPathRange range{};

  friend bool operator==(const TextPathFrame&, const TextPathFrame&) = default;
};

inline constexpr std::size_t kTextPathMaxAnchors = 4096;

// One subpath, open or closed, two to kTextPathMaxAnchors anchors, finite coordinates.
[[nodiscard]] bool valid_text_path(const VectorPath& path);

// Cubic segments of the contour: anchors - 1 when open, anchors when closed.
[[nodiscard]] int text_path_segment_count(const VectorPath& path);

// The whole contour: 0 to its segment count.
[[nodiscard]] TextPathRange full_text_path_range(const VectorPath& path);

// Clamps a range onto the contour by the rules above; a range that is empty, reversed or not
// finite becomes the whole contour.
[[nodiscard]] TextPathRange normalized_text_path_range(const VectorPath& path, TextPathRange range);

[[nodiscard]] std::string serialize_text_path_range(TextPathRange range);
[[nodiscard]] std::optional<TextPathRange> parse_text_path_range(std::string_view text);

// The layer's contour in text-local coordinates (the text transform maps it to the document),
// and its range, normalized. nullopt for a layer that is not path text.
[[nodiscard]] std::optional<VectorPath> text_path_for_layer(const Layer& layer);
[[nodiscard]] TextPathRange text_path_range_for_layer(const Layer& layer, const VectorPath& path);
[[nodiscard]] std::optional<VectorPath> text_path_in_document(const Layer& layer);
// Both together; nullopt for a layer that is not path text.
[[nodiscard]] std::optional<TextPathFrame> text_path_frame_for_layer(const Layer& layer);
// True for a text layer whose lines are placed by an owned contour: area text or path text.
[[nodiscard]] bool text_layer_has_contour(const Layer& layer);

// The same contour walked the other way, and the range that covers the same stretch of it.
// Text set on the reversed contour runs back along that stretch on the other side, which is
// how Patchy flips path text: the file then holds an ordinary path-text frame.
[[nodiscard]] VectorPath reversed_text_path(const VectorPath& path);
[[nodiscard]] TextPathRange reversed_text_path_range(const VectorPath& path, TextPathRange range);

// Arc-length table over the contour's cubic segments. Deterministic: a fixed number of chords
// per segment, plain double arithmetic in a fixed order.
class TextPathCurve {
 public:
  struct Sample {
    double x{0.0};
    double y{0.0};
    // Unit tangent in the direction of travel. A glyph's "up" is (tangent_y, -tangent_x).
    double tangent_x{1.0};
    double tangent_y{0.0};
  };
  struct Nearest {
    double parameter{0.0};  // segment units, 0 <= parameter <= segment_count
    double distance{0.0};   // arc length from the contour's first anchor
    double gap{0.0};        // how far the point is from the contour
    bool left{false};       // the point is on the side the text stands on (left of travel in y-down space)
  };

  // `scale` multiplies every coordinate first (the render pass's fold scale).
  explicit TextPathCurve(const VectorPath& path, double scale = 1.0);

  [[nodiscard]] bool valid() const noexcept { return segment_count_ > 0 && length_ > 0.0; }
  [[nodiscard]] bool closed() const noexcept { return closed_; }
  [[nodiscard]] int segment_count() const noexcept { return segment_count_; }
  [[nodiscard]] double length() const noexcept { return length_; }

  // Arc length from the first anchor to a range value. On a closed contour a value past the
  // segment count adds whole laps, so distance(end) - distance(start) is the range's length.
  [[nodiscard]] double distance_at_parameter(double parameter) const;
  // The inverse for 0 <= distance <= length (clamped).
  [[nodiscard]] double parameter_at_distance(double distance) const;

  // The point and tangent an arc length along the contour. A closed contour wraps; an open one
  // continues straight along its end tangents, so a glyph hanging over an end keeps its turn.
  [[nodiscard]] Sample sample(double distance) const;

  [[nodiscard]] Nearest nearest(double x, double y) const;

 private:
  struct Segment {
    double x[4]{};
    double y[4]{};
  };
  static constexpr int kChordsPerSegment = 128;

  [[nodiscard]] Sample sample_segment(std::size_t segment, double t) const;

  std::vector<Segment> segments_;
  // cumulative_[segment * (kChordsPerSegment + 1) + chord] = arc length at that chord start.
  std::vector<double> cumulative_;
  int segment_count_{0};
  double length_{0.0};
  bool closed_{false};
};

// The stretch of the contour a range covers.
struct TextPathSpan {
  double start_distance{0.0};
  double length{0.0};
};
[[nodiscard]] TextPathSpan text_path_span(const TextPathCurve& curve, TextPathRange range);

}  // namespace patchy
