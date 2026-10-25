#include "render/layer_compositor.hpp"

// The one instantiation of the compositor for 16/32-bit documents (docs/high-bit-depth.md):
// the deep float buffer the UI renders and the deep flatten composite into.
PATCHY_COMPOSITOR_COLOR_INSTANTIATIONS(, patchy::render_detail::DeepRgb)
PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS(, patchy::render_detail::DeepCompositeTarget)
