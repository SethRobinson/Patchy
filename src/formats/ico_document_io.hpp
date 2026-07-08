#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::ico {

// Layer metadata key carrying a cursor hotspot ("x,y" in that layer's pixel space) so a
// .cur round trip can prefill the export dialog. Session-only: no file format persists it
// except CUR itself.
inline constexpr const char* kLayerMetadataCursorHotspot = "patchy.cursor_hotspot";

// ICO entries at 256 px are PNG-compressed per the Vista+ spec. The formats library is
// Qt-free, so the PNG codec is injected by the UI layer once at startup (capture-free
// lambdas over QImage). A null decoder skips PNG entries with a notice; a null encoder
// stores 256 px entries as plain BMP entries instead.
struct RgbaImage {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> rgba;  // 4 bytes per pixel, row-major, straight alpha
};
using PngDecodeFn = RgbaImage (*)(std::span<const std::uint8_t>);
using PngEncodeFn = std::vector<std::uint8_t> (*)(const RgbaImage&);
void set_png_codec(PngDecodeFn decode, PngEncodeFn encode);

struct WriteOptions {
  std::vector<int> sizes{16, 24, 32, 48, 64, 128, 256};
  bool nearest_neighbor{true};  // resample for generated sizes; exact "WxH" layers are used verbatim
  bool as_cursor{false};        // write a .cur (type 2) with hotspots
  int hotspot_x{0};             // hotspot in the coordinate space of the LARGEST written size
  int hotspot_y{0};
};

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  // Every embedded size imports as its own layer named "WxH" (only the largest visible),
  // so hand-authored small sizes survive a round trip; the writer reuses a pixel layer
  // named exactly "WxH" with matching dimensions verbatim for that size.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document, WriteOptions options = {});
  static void write_file(const Document& document, const std::filesystem::path& path, WriteOptions options = {});
};

}  // namespace patchy::ico
