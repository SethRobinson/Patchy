#include "ui/curved_slider.hpp"

#include <QAbstractSlider>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <optional>

namespace patchy::ui {

namespace {

// The value range a curved slider covers, stored on the slider itself so any
// code holding only the slider can convert between positions and values.
constexpr char kCurvedSliderMinimumProperty[] = "patchy.curvedSliderMinimum";
constexpr char kCurvedSliderMaximumProperty[] = "patchy.curvedSliderMaximum";

struct CurveRange {
  int minimum{0};
  int maximum{0};
};

std::optional<CurveRange> curve_range(const QSlider& slider) {
  const auto minimum = slider.property(kCurvedSliderMinimumProperty);
  const auto maximum = slider.property(kCurvedSliderMaximumProperty);
  if (!minimum.isValid() || !maximum.isValid()) {
    return std::nullopt;
  }
  return CurveRange{minimum.toInt(), maximum.toInt()};
}

int value_at(const CurveRange& range, int position) {
  return static_cast<int>(std::lround(curved_slider_value(position, range.minimum, range.maximum)));
}

// The position that shows exactly `value`, or the nearest one where the curve
// steps over it (near the top one position spans more than one unit).
int position_for(const CurveRange& range, int value) {
  const auto guess = curved_slider_position(value, range.minimum, range.maximum);
  for (const auto candidate : {guess, guess - 1, guess + 1}) {
    if (candidate >= 0 && candidate <= kCurvedSliderPositions && value_at(range, candidate) == value) {
      return candidate;
    }
  }
  return guess;
}

}  // namespace

double curved_slider_value(int position, double minimum, double maximum) {
  const auto t = static_cast<double>(std::clamp(position, 0, kCurvedSliderPositions)) / kCurvedSliderPositions;
  return minimum + (maximum - minimum) * t * t;
}

int curved_slider_position(double value, double minimum, double maximum) {
  if (!(maximum > minimum)) {
    return 0;
  }
  const auto fraction = std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0);
  return static_cast<int>(std::lround(std::sqrt(fraction) * kCurvedSliderPositions));
}

void bind_curved_slider(QSlider& slider, QSpinBox& spin, int slider_maximum) {
  const CurveRange range{spin.minimum(),
                         std::clamp(std::min(slider_maximum, spin.maximum()), spin.minimum(), spin.maximum())};
  slider.setProperty(kCurvedSliderMinimumProperty, range.minimum);
  slider.setProperty(kCurvedSliderMaximumProperty, range.maximum);
  slider.setRange(0, kCurvedSliderPositions);
  slider.setSingleStep(1);
  slider.setPageStep(kCurvedSliderPositions / 20);
  {
    const QSignalBlocker blocker(slider);
    set_slider_to_value(slider, spin.value());
  }

  auto* slider_ptr = &slider;
  auto* spin_ptr = &spin;
  QObject::connect(&slider, &QSlider::valueChanged, &spin,
                   [spin_ptr, range](int position) { spin_ptr->setValue(value_at(range, position)); });
  // While dragging, the handle already shows the value it just set, so this
  // leaves it alone instead of snapping it to the value's canonical position.
  QObject::connect(&spin, qOverload<int>(&QSpinBox::valueChanged), &slider, [slider_ptr](int value) {
    const QSignalBlocker blocker(slider_ptr);
    set_slider_to_value(*slider_ptr, value);
  });
  // QAbstractSlider lets an actionTriggered slot adjust the pending position.
  // A single step near the bottom of the curve can land on the same rounded
  // value, so keep walking until the value actually changes. Drags are exempt:
  // the handle follows the mouse.
  QObject::connect(&slider, &QAbstractSlider::actionTriggered, &spin, [slider_ptr, spin_ptr, range](int action) {
    if (action == QAbstractSlider::SliderNoAction || slider_ptr->isSliderDown()) {
      return;
    }
    const auto from = slider_ptr->value();
    auto to = slider_ptr->sliderPosition();
    if (to == from) {
      return;
    }
    const auto direction = to > from ? 1 : -1;
    const auto current = std::clamp(spin_ptr->value(), range.minimum, range.maximum);
    while (value_at(range, to) == current && to + direction >= 0 && to + direction <= kCurvedSliderPositions) {
      to += direction;
    }
    slider_ptr->setSliderPosition(to);
  });
}

int slider_value(const QSlider& slider) {
  if (const auto range = curve_range(slider)) {
    return value_at(*range, slider.value());
  }
  return slider.value();
}

void set_slider_to_value(QSlider& slider, int value) {
  const auto range = curve_range(slider);
  if (!range) {
    slider.setValue(value);
    return;
  }
  const auto clamped = std::clamp(value, range->minimum, range->maximum);
  if (value_at(*range, slider.value()) == clamped) {
    return;
  }
  slider.setValue(position_for(*range, clamped));
}

}  // namespace patchy::ui
