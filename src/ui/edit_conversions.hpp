#pragma once

#include "core/pixel_tools.hpp"

#include <QColor>
#include <QImage>

#include <cstdint>

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
// A deep image from qimage_from_pixel_buffer as something QPainter can draw for the screen:
// 32-bit linear floats (Format_RGBA32FPx4) scaled by 2^exposure_stops (the 32-bit preview
// exposure) and sRGB-encoded into straight RGBA8888, the way the canvas narrows a 32-bit
// composite. QPainter would otherwise show the linear values as display values, too dark.
// Every other format comes back unchanged (16-bit images are already display-encoded).
[[nodiscard]] QImage display_image_from_deep_qimage(const QImage& image, float exposure_stops);

// Whether `pixels` is a single-channel coverage buffer (a layer or smart filter mask, a saved
// channel) at any depth, so coverage8_at can read it.
[[nodiscard]] bool is_coverage_buffer(const PixelBuffer& pixels) noexcept;
// One sample of a coverage buffer at any depth as an 8-bit value for previews (mask
// thumbnails, the mask view): 8-bit samples as they are, 16 and 32-bit scaled and rounded.
[[nodiscard]] std::uint8_t coverage8_at(const PixelBuffer& pixels, int x, int y);

}  // namespace patchy::ui
