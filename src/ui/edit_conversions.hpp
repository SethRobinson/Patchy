#pragma once

#include "core/pixel_tools.hpp"

#include <QColor>
#include <QImage>

namespace patchy::ui {

[[nodiscard]] EditColor edit_color(QColor color);

// Converts an 8-bit RGB(A) PixelBuffer to a Format_RGBA8888 QImage; other
// formats produce a fully transparent image of the buffer's size.
[[nodiscard]] QImage qimage_from_pixel_buffer(const PixelBuffer& pixels);

}  // namespace patchy::ui
