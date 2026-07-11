#pragma once

#include "core/adjustment_layer.hpp"

#include <QImage>

#include <optional>

namespace patchy::ui {

enum class CurvesClippingMode {
  Shadows,
  Highlights,
  Both
};

// Produces a false-color clipping view from an already rendered Curves preview.
// Exact 0 and 255 component values count as clipped. With no active component,
// or with CurvesChannel::Rgb, all RGB components participate; Red, Green, and
// Blue restrict the indication to that component.
//
// Highlights use black as the background and add the colors of clipped
// components. Shadows use white and subtract clipped components, producing the
// familiar cyan/magenta/yellow indications. Both uses neutral gray, moving
// each clipped component to 0 or 255 so shadow and highlight clipping remain
// distinguishable in one view. Source alpha is copied byte-for-byte.
//
// The source is never mutated. The result is RGBA8888 for alpha-bearing input
// and RGB888 otherwise. A null source produces a null result. Runtime and
// additional storage are both linear in the pixel count.
[[nodiscard]] QImage render_curves_clipping_preview(
    const QImage& source, CurvesClippingMode mode,
    std::optional<CurvesChannel> active_component = std::nullopt);

}  // namespace patchy::ui
