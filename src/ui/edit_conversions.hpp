#pragma once

#include "core/pixel_tools.hpp"

#include <QColor>
#include <QImage>

namespace patchy::ui {

[[nodiscard]] EditColor edit_color(QColor color);

// Converts an RGB(A) PixelBuffer to a QImage at its depth: 8-bit to Format_RGBA8888,
// 16-bit to Format_RGBA64, 32-bit to Format_RGBA32FPx4 holding the buffer's linear
// values (docs/high-bit-depth.md). Gray and other formats produce a fully transparent
// 8-bit image of the buffer's size. pixels_from_image_native reverses each form.
[[nodiscard]] QImage qimage_from_pixel_buffer(const PixelBuffer& pixels);
// For showing or exporting pixels (clipboard, PDF, previews): always Format_RGBA8888,
// a deep buffer narrowed and a 32-bit one sRGB-encoded.
[[nodiscard]] QImage display_qimage_from_pixel_buffer(const PixelBuffer& pixels);

// Converts an 8-bit single-channel PixelBuffer (a layer mask) to a
// Format_Grayscale8 QImage whose bytes equal the mask values. GPU paths upload
// this image as an R8 texture and read the mask from the red channel; it must
// never be built as Format_Alpha8, because Qt converts alpha-only images to
// grayscale as opaque black and the GPU would then treat every mask as empty.
// Empty or non-8-bit buffers produce a null QImage.
[[nodiscard]] QImage grayscale_qimage_from_pixel_buffer(const PixelBuffer& pixels);

}  // namespace patchy::ui
