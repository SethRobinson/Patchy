#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// OpenRaster (.ora) reader and writer (docs/ora.md), written from the published spec at
// https://www.openraster.org and from files GIMP and Krita write. A zip of PNG layers:
// the stored "mimetype" entry first, stack.xml (the layer tree), one PNG per layer under
// data/, mergedimage.png and Thumbnails/thumbnail.png. Qt-free: zip through the vendored
// miniz, XML through svg_xml, PNG decoding through the vendored stb_image and encoding
// through a deterministic miniz-based encoder.
namespace patchy::ora {

// True when the bytes start like an OpenRaster file: a zip whose first entry is the
// stored "mimetype" holding "image/openraster".
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

// What a save to OpenRaster bakes or drops, one item per cause (the Save As warning names
// them). Layer items carry the layer's name; document items leave it empty.
enum class LossKind : std::uint8_t {
  TextLayer,         // rasterized
  ShapeLayer,        // rasterized
  SmartObjectLayer,  // rasterized
  AdjustmentLayer,   // merged with the layers below it into one image
  BlendModeMerged,   // a blend mode ORA has no name for: merged with the layers below
  BlendModeKrita,    // kept under Krita's own name, which other apps read as Normal
  LayerStyle,        // effects baked into the pixels
  LayerMask,         // a layer or vector mask baked into the pixels
  ClippingGroup,     // the clipped layers baked into one image with their base
  BlendingOptions,   // fill opacity, Blend If, knockout or channel restrictions baked
  Group,             // a group with styles or masks baked into one image
  Depth32,           // 32-bit channels saved at 16 bits
  ColorMode,         // CMYK, Lab or grayscale working mode saved as RGB
  PaletteMode,       // the palette is not stored (pixels save as RGB)
  Guides,
  Paths,
  Artboards,  // saved as plain groups
};

struct Loss {
  LossKind kind{LossKind::TextLayer};
  std::string layer_name;
};

class DocumentIo {
public:
  // Layer tree (groups, offsets, opacity, visibility, names, blend modes, the selected
  // layer), 8-bit or 16-bit samples, and the resolution. Notices name what was
  // approximated (an unknown composite-op opens as Normal, a missing layer file opens
  // empty). Throws std::runtime_error with a plain message for files that are not
  // OpenRaster or are damaged past reading.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
  // Deterministic bytes: fixed entry order and timestamps, so the same document always
  // writes the same file. 8-bit documents write 8-bit PNGs; 16 and 32-bit documents write
  // 16-bit PNGs. Whatever ORA cannot hold is baked (see lost_content); `notices` receives
  // one plain sentence per item.
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document,
                                                       std::vector<std::string>* notices = nullptr);
  static void write_file(const Document& document, const std::filesystem::path& path,
                         std::vector<std::string>* notices = nullptr);
  // The writer's dry run: what write() would bake or drop, without rendering anything.
  [[nodiscard]] static std::vector<Loss> lost_content(const Document& document);
};

namespace detail {

// The writer's zip container, exposed for tests. Entries are written in order with a
// fixed 1980-01-01 timestamp; `deflated` entries go through raw deflate, the others are
// stored. Zip64 records appear where a size or offset needs them, or everywhere with
// `force_zip64` (so the tests can read that form back without writing 4 GB).
struct ZipEntry {
  std::string name;
  std::vector<std::uint8_t> data;
  bool deflated{false};
};
[[nodiscard]] std::vector<std::uint8_t> zip_archive(const std::vector<ZipEntry>& entries, bool force_zip64 = false);

// The writer's PNG encoder: RGBA at 8 or 16 bits (the buffer's depth; 32-bit buffers are
// refused), adaptive filtering, fixed deflate level. Gray and RGB buffers expand to RGBA.
[[nodiscard]] std::vector<std::uint8_t> encode_png(const PixelBuffer& pixels);

}  // namespace detail

}  // namespace patchy::ora
