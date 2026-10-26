#pragma once

#include "formats/metadata/xmp.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// GIMP's "gimp-image-metadata" XCF parasite (docs/xcf.md, "Parasites"): an XML list of
// exiv2 keys and their text values,
//   <metadata><namespace prefix="dc" url="..."/><tag name="Xmp.dc.title">lang="x-default" T</tag>
//   <tag name="Exif.Image.Artist">A</tag><tag name="Iptc.Application2.City">C</tag></metadata>,
// with encoding="base64" on values that are not plain UTF-8. GIMP 2.10 and 3.x keep the
// image's Exif, XMP and IPTC there. Qt-free; malformed input yields empty blocks, never a
// throw.
namespace patchy::metadata {

struct GimpImageMetadata {
  std::optional<XmpPacket> xmp;    // the Xmp.* tags; nullopt when there were none
  std::vector<std::uint8_t> exif;  // the Exif.* tags as a big-endian TIFF stream, or empty
  std::vector<std::uint8_t> iptc;  // the Iptc.Application2.* tags as IPTC-IIM, or empty
};

[[nodiscard]] GimpImageMetadata parse_gimp_image_metadata(std::span<const std::uint8_t> bytes);

// Appends every top-level property of `from` that `into` lacks; returns how many.
std::size_t add_missing_xmp_properties(XmpPacket& into, const XmpPacket& from);

}  // namespace patchy::metadata
