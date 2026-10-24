#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace patchy::pdn {

// Paint.NET's native .pdn documents, read-only. A "PDN3" header (a 24-bit
// little-endian length and an XML block with the size and a PNG thumbnail), a .NET
// BinaryFormatter graph (MS-NRBF, src/formats/nrbf_reader) describing the document
// and its layers, then each layer's BGRA pixels as numbered gzip or raw chunks.
// Paint.NET 1.x/2.x files are the same graph gzipped whole, without the header.
// Written from the published MS-NRBF specification, the MIT-licensed pypdn notes and
// byte observation of sample and self-made files; never from Paint.NET's binaries.
// Format record and verification corpus: docs/pdn-format.md.

[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] const std::vector<std::string>& pdn_extensions();

struct Header {
  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t layer_count{0};
  std::string saved_with_version;     // "4.21.6589.7045"; empty when absent
  std::vector<std::uint8_t> thumbnail_png;
  std::size_t payload_offset{0};      // first byte after the XML block
};

// The PDN3 header, or nullopt when the bytes do not start with one.
[[nodiscard]] std::optional<Header> read_header(std::span<const std::uint8_t> bytes);

// The document's user metadata as stored: Paint.NET 4+ key/value items, or the older
// NameValueCollection (one entry per value). EXIF tags appear as "$exif.tag<N>" keys
// whose values are <exif id= len= type= value=(base64)/> elements. Throws when the
// graph cannot be read.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> read_user_metadata(std::span<const std::uint8_t> bytes);

// Rebuilds a little-endian TIFF stream (an EXIF block, File Info's canonical form)
// from the "$exif.tag<N>" entries; empty when there are none. Tags Patchy files under
// the EXIF or GPS sub-IFDs go there; the rest stay in IFD0.
[[nodiscard]] std::vector<std::uint8_t> exif_from_user_metadata(
    const std::vector<std::pair<std::string, std::string>>& items);

class DocumentIo {
public:
  // Imports every layer (bottom first) with its name, visibility, opacity and blend
  // mode; a bottom Background layer that is fully opaque becomes Patchy's Background.
  // When the graph cannot be read the thumbnail opens instead, with a notice; throws
  // only when neither is readable.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
};

}  // namespace patchy::pdn
