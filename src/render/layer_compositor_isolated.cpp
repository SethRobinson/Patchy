#include "render/layer_compositor.hpp"

// The one instantiation of the compositor for the 8-bit isolated group buffer: the
// target native_cmyk, native_duotone and the layer-style tests composite into, and the
// buffer every other 8-bit target's groups and clip runs merge from.
PATCHY_COMPOSITOR_COLOR_INSTANTIATIONS(, patchy::RgbColor)
PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS(, patchy::render_detail::IsolatedClipGroupTarget)
