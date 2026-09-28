#pragma once

#include <limits>

class QSlider;
class QSpinBox;

namespace patchy::ui {

// How a slider spreads its value range along its track. FineLowEnd is for
// size-like values (brush size, feather, effect sizes) where most picks are
// small: the value grows with the square of the handle position, so the first
// half of the track covers the bottom quarter of the range.
enum class SliderCurve { Linear, FineLowEnd };

// A curved slider always spans positions 0..kCurvedSliderPositions, whatever
// its value range; QSlider::value() is a position, not a value.
inline constexpr int kCurvedSliderPositions = 1000;

// The FineLowEnd mapping between a position (clamped to 0..kCurvedSliderPositions)
// and a value in [minimum, maximum]: value = minimum + (maximum - minimum) * t^2.
[[nodiscard]] double curved_slider_value(int position, double minimum, double maximum);
// Inverse of curved_slider_value, rounded to the nearest position.
[[nodiscard]] int curved_slider_position(double value, double minimum, double maximum);

// Makes `slider` a FineLowEnd slider for `spin` and connects the two both ways;
// call it instead of connecting them yourself. A slider_maximum below the spin
// box's maximum stops the slider there while the spin box still accepts the
// full range (a typed value past it parks the slider at its end). Keyboard,
// wheel and page steps always move the value by at least one unit, even where
// the curve is flat.
void bind_curved_slider(QSlider& slider, QSpinBox& spin,
                        int slider_maximum = std::numeric_limits<int>::max());

// Value-space access that works for curved and linear sliders alike, so code
// and tests never convert positions by hand. slider_value is the value the
// handle shows; set_slider_to_value moves the handle to show `value` (clamped
// to the slider's reach), emitting valueChanged unless the caller blocks it.
// A curved slider whose handle already shows `value` stays where it is.
[[nodiscard]] int slider_value(const QSlider& slider);
void set_slider_to_value(QSlider& slider, int value);

}  // namespace patchy::ui
