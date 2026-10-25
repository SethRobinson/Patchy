#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Krita's native .kra documents (and .krz, the same container), read-only. A zip: a stored
// "mimetype" entry holding "application/x-krita", maindoc.xml (the layer tree), per-layer
// tiled pixel data compressed with LZF, ICC profiles, SVG for vector layers, filter and fill
// settings as XML, layer styles as an ASL file, and Krita's own rendered mergedimage.png.
// Written only from files Krita writes, inspected as data (docs/kra.md); Krita's source was
// never read. Qt-free: zip through the vendored miniz, XML through svg_xml.
namespace patchy::kra {

[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] const std::vector<std::string>& kra_extensions();

class DocumentIo {
public:
  // Layers, groups, masks, blend modes, vector, fill, filter, clone and file layers and
  // layer styles, mapped to Patchy's equivalents where one exists (docs/kra.md says
  // which). Notices name everything approximated or left out. Throws std::runtime_error
  // with a plain message for files that are not Krita documents or are damaged past
  // reading. `source_directory`, when given, is the folder the file sits in: file layers
  // that point at a PNG or JPEG beside it load from there.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices = nullptr,
                                     const std::filesystem::path& source_directory = {});
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
};

namespace detail {

// Decompresses one LZF block (the tile compression; liblzf's published stream format) into
// exactly `out.size()` bytes. False when the input is damaged or does not fill `out` exactly.
[[nodiscard]] bool lzf_decompress(std::span<const std::uint8_t> in, std::span<std::uint8_t> out) noexcept;

}  // namespace detail

}  // namespace patchy::kra
