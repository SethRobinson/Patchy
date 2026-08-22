#pragma once

#include <cstdint>
#include <span>
#include <vector>

// A minimal PNG writer for 8-bit RGBA, used to park decoded PDF image samples in the
// SmartObjectStore, which holds embedded sources as real file bytes.
//
// Qt could encode this, but the formats library is Qt-free on purpose and the store
// is filled while reading. PNG over raw samples because the store's sources are
// files: a smart object's bytes are what Photoshop would round-trip, and "png " is
// already one of the OSTypes the PSD writer emits.

namespace patchy::formats {

// `rgba` must hold width * height * 4 bytes. Returns an empty vector on bad input.
[[nodiscard]] std::vector<std::uint8_t> encode_png_rgba8(std::span<const std::uint8_t> rgba, int width, int height);

}  // namespace patchy::formats
