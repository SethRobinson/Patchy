#pragma once

// The Pick tool's canvas overlays, kept free of CanvasWidget so their geometry is
// testable on its own (docs/pick-tool.md):
// - the sample square's outline at the pointer (GitHub issue 319), and
// - the sample loupe shown while a pick is held (GitHub issue 320): a magnified view
//   of the pixels around the pointer with the sampled square outlined, inside a band
//   of two arcs, the live sample on top and the color it replaces below.
// Both are drawn in cursor ink (a dark-plus-light pair), which is exempt from the
// theme roles like every tool cursor (docs/ui-conventions.md).

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>

class QPainter;

namespace patchy::ui {

// The outline shows for an average (not Point Sample) whose square is at least this
// many screen pixels across.
inline constexpr double kEyedropperSampleOutlineMinScreenPixels = 4.0;
[[nodiscard]] bool eyedropper_sample_outline_visible(int sample_size, double zoom) noexcept;
// The document square a pick of `sample_size` averages around `point`, clipped to the document.
[[nodiscard]] QRect eyedropper_sample_rect(QPoint point, int sample_size, QSize document_size) noexcept;

inline constexpr double kSampleLoupeLensRadius = 34.0;
inline constexpr double kSampleLoupeBandInnerRadius = 40.0;
inline constexpr double kSampleLoupeBandOuterRadius = 58.0;
// Border and soft shadow outside the band.
inline constexpr double kSampleLoupeMargin = 6.0;

struct SampleLoupeGeometry {
  QPointF center;
  // True when the loupe moved off the pointer to stay inside the viewport.
  bool shifted{false};
  // Everything the loupe paints, shadow included (widget coordinates).
  [[nodiscard]] QRectF bounds() const;
};

// Centered on the pointer, slid inward (never clipped) where it would cross the viewport edge.
[[nodiscard]] SampleLoupeGeometry sample_loupe_geometry(QPointF pointer, QRectF viewport) noexcept;
// Document pixels across the lens: odd, the sample square plus context around it.
[[nodiscard]] int sample_loupe_view_pixels(int sample_size) noexcept;
// The document square the lens shows around `point` (not clipped to the document).
[[nodiscard]] QRect sample_loupe_view_rect(QPoint point, int sample_size) noexcept;

struct SampleLoupeContent {
  // Display-encoded pixels of source_rect (never a 32-bit float image).
  QImage pixels;
  QRect view_rect;    // the whole square the lens shows
  QRect source_rect;  // view_rect inside the document
  QRect sample_rect;  // the averaged square, inside the document
  QColor sample;      // the live pick
  QColor previous;    // the target color when the press began
  QColor backdrop;    // the lens outside the document
};

// The pointer position is drawn as a small crosshair when the loupe is shifted off it.
void paint_sample_loupe(QPainter& painter, const SampleLoupeGeometry& geometry, const SampleLoupeContent& content,
                        QPointF pointer);

}  // namespace patchy::ui
