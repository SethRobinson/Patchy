#include "formats/metadata/metadata_containers.hpp"

#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/jumbf.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/metadata/xmp.hpp"
#include "formats/jxl_document_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/pdf_crypt.hpp"
#include "formats/pdf_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>

namespace patchy::metadata {
namespace {

// Whole files up to this size are read for extraction; larger ones are scanned from
// their first kPartialReadBytes (enough for every container that keeps its metadata
// ahead of the image data).
constexpr std::uintmax_t kFullReadCap = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kPartialReadBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaxStructureRows = 2000;
constexpr std::size_t kMaxInflatedText = 16U * 1024U * 1024U;

using Bytes = std::vector<std::uint8_t>;
using Span = std::span<const std::uint8_t>;

std::uint16_t be16(Span data, std::size_t at) {
  return static_cast<std::uint16_t>((data[at] << 8) | data[at + 1]);
}
std::uint32_t be32(Span data, std::size_t at) {
  return (static_cast<std::uint32_t>(data[at]) << 24) | (static_cast<std::uint32_t>(data[at + 1]) << 16) |
         (static_cast<std::uint32_t>(data[at + 2]) << 8) | data[at + 3];
}
std::uint32_t le32(Span data, std::size_t at) {
  return data[at] | (static_cast<std::uint32_t>(data[at + 1]) << 8) | (static_cast<std::uint32_t>(data[at + 2]) << 16) |
         (static_cast<std::uint32_t>(data[at + 3]) << 24);
}
std::uint64_t be_n(Span data, std::size_t at, std::size_t size) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < size; ++i) {
    value = (value << 8) | data[at + i];
  }
  return value;
}

bool starts_with(Span data, std::string_view prefix, std::size_t at = 0) {
  return at <= data.size() && data.size() - at >= prefix.size() &&
         std::memcmp(data.data() + at, prefix.data(), prefix.size()) == 0;
}

Bytes to_bytes(Span data) {
  return {data.begin(), data.end()};
}

std::string hex_byte(std::uint8_t value) {
  char buffer[4];
  std::snprintf(buffer, sizeof(buffer), "%02X", static_cast<unsigned>(value));
  return buffer;
}

std::string size_text(std::uint64_t bytes) {
  char buffer[48];
  if (bytes >= 1024ULL * 1024ULL) {
    std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024ULL) {
    std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llu bytes", static_cast<unsigned long long>(bytes));
  }
  return buffer;
}

struct Builder {
  DocumentFileInfo info;

  void segment(std::string name, std::uint64_t offset, std::uint64_t size) {
    if (info.structure.size() < kMaxStructureRows) {
      info.structure.push_back(FileInfoSegment{std::move(name), offset, size});
    }
  }

  void set_exif(Span tiff) {
    if (!info.exif && tiff.size() >= 8 && parse_tiff(tiff)) {
      info.exif = make_metadata_bytes(to_bytes(tiff));
    }
  }

  void set_xmp(Span packet) {
    if (!info.xmp && !packet.empty()) {
      info.xmp = make_metadata_bytes(to_bytes(packet));
    }
  }

  void set_icc(Span profile) {
    if (!info.source_icc && profile.size() >= 128) {
      info.source_icc = make_metadata_bytes(to_bytes(profile));
    }
  }

  void observe(std::string container, std::string label, std::string summary, std::uint64_t size,
               MetadataPrivacy privacy, SharedMetadataBytes bytes = {}, bool is_thumbnail = false) {
    FileInfoObserved item;
    item.container = std::move(container);
    item.label = std::move(label);
    item.summary = std::move(summary);
    item.size = size;
    item.privacy = privacy;
    item.bytes = std::move(bytes);
    item.is_thumbnail = is_thumbnail;
    info.observed.push_back(std::move(item));
  }

  void observe_c2pa(std::string container, Span jumbf) {
    const auto summary = summarize_jumbf(jumbf);
    std::string text;
    if (!summary.claim_generator.empty()) {
      text = "Generator: " + summary.claim_generator;
    }
    for (const auto& action : summary.actions) {
      text += (text.empty() ? "" : "; ") + action;
    }
    if (text.empty() && !summary.labels.empty()) {
      text = std::to_string(summary.labels.size()) + " JUMBF boxes";
    }
    if (summary.declares_watermark()) {
      std::string declared;
      for (const auto& algorithm : summary.soft_bindings) {
        declared += (declared.empty() ? "" : "; ") + ("Soft binding: " + algorithm);
      }
      for (const auto& action : summary.watermark_actions) {
        declared += (declared.empty() ? "" : "; ") + ("Action: " + action);
      }
      observe(container, "Watermark declared in Content Credentials", std::move(declared), 0,
              MetadataPrivacy::Watermark);
    }
    observe(std::move(container), "C2PA Content Credentials", text, jumbf.size(), MetadataPrivacy::Provenance,
            make_metadata_bytes(to_bytes(jumbf)));
  }

  // Photoshop image-resource list (JPEG APP13, TIFF 34377).
  void read_irb(Span data, const std::string& container) {
    for (auto& resource : parse_irb(data)) {
      const auto id = resource.id;
      const auto name = psd_resource_name(id);
      if (id == psd_resource::kIptc) {
        if (!info.iptc && parse_iim(resource.data)) {
          info.iptc = make_metadata_bytes(std::move(resource.data));
        }
      } else if (id == psd_resource::kCaptionDigest && resource.data.size() == 16) {
        std::array<std::uint8_t, 16> digest{};
        std::copy(resource.data.begin(), resource.data.end(), digest.begin());
        info.iptc_digest = digest;
      } else if (id == psd_resource::kCopyrightFlag && !resource.data.empty()) {
        info.copyright_flag = resource.data[0] != 0;
      } else if (id == psd_resource::kUrl) {
        info.copyright_url = legacy_text_to_utf8(resource.data);
        while (!info.copyright_url.empty() && info.copyright_url.back() == '\0') {
          info.copyright_url.pop_back();
        }
      } else if (id == psd_resource::kXmp) {
        set_xmp(resource.data);
      } else if (id == psd_resource::kIccProfile) {
        set_icc(resource.data);
      } else if (id == psd_resource::kExifData1) {
        set_exif(resource.data);
      } else if (id == psd_resource::kThumbnail || id == psd_resource::kThumbnailPs4) {
        auto jpeg = psd_thumbnail_jpeg(resource.data);
        const bool decodable = !jpeg.empty() && id == psd_resource::kThumbnail;
        observe(container, name, size_text(resource.data.size()), resource.data.size(),
                MetadataPrivacy::HiddenImage, decodable ? make_metadata_bytes(std::move(jpeg)) : SharedMetadataBytes{},
                decodable);
      } else if (id != psd_resource::kResolutionInfo) {
        observe(container, "Photoshop resource " + std::to_string(id) + ": " + name, size_text(resource.data.size()),
                resource.data.size(), psd_resource_privacy(id));
      }
    }
  }
};

// ---------------------------------------------------------------- XMP helpers

// Merges a JPEG Extended XMP packet into the standard one (ISO 16684-1 Annex /
// XMP Part 3: the standard packet names the extension with xmpNote:HasExtendedXMP).
Bytes merge_extended_xmp(Span standard, Span extended) {
  auto main = parse_xmp(standard);
  const auto extra = parse_xmp(extended);
  if (!main || !extra) {
    return to_bytes(standard);
  }
  for (const auto& property : extra->properties) {
    if (main->find(property.ns, property.name) == nullptr) {
      main->properties.push_back(property);
    }
  }
  for (const auto& [ns, prefix] : extra->prefixes) {
    main->prefixes.emplace(ns, prefix);
  }
  main->erase(xmp_ns::kXmpNote, "HasExtendedXMP");
  const auto text = serialize_xmp(*main, 0);
  return {text.begin(), text.end()};
}

std::string md5_hex(Span data) {
  const auto digest = pdf::md5(data);
  std::string hex;
  for (const auto byte : digest) {
    hex += hex_byte(byte);
  }
  return hex;
}

// ImageMagick's "Raw profile type <name>" text: "\n<name>\n<length>\n<hex lines>".
Bytes decode_raw_profile_hex(std::string_view text) {
  std::size_t pos = 0;
  int newlines = 0;
  while (pos < text.size() && newlines < 3) {
    if (text[pos] == '\n') {
      ++newlines;
    }
    ++pos;
  }
  Bytes out;
  int high = -1;
  for (; pos < text.size(); ++pos) {
    const char c = text[pos];
    int value = -1;
    if (c >= '0' && c <= '9') {
      value = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      value = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      value = c - 'A' + 10;
    }
    if (value < 0) {
      continue;
    }
    if (high < 0) {
      high = value;
    } else {
      out.push_back(static_cast<std::uint8_t>((high << 4) | value));
      high = -1;
    }
  }
  return out;
}

Span strip_exif_prefix(Span data) {
  if (starts_with(data, std::string_view("Exif\0\0", 6)) || starts_with(data, std::string_view("Exif\0\xFF", 6))) {
    return data.subspan(6);
  }
  return data;
}

// ---------------------------------------------------------------------- JPEG

std::string jpeg_marker_name(std::uint8_t marker) {
  if (marker >= 0xE0 && marker <= 0xEF) {
    return "APP" + std::to_string(marker - 0xE0);
  }
  switch (marker) {
    case 0xC0:
      return "SOF0 (baseline)";
    case 0xC1:
      return "SOF1 (extended)";
    case 0xC2:
      return "SOF2 (progressive)";
    case 0xC4:
      return "DHT";
    case 0xDB:
      return "DQT";
    case 0xDD:
      return "DRI";
    case 0xDA:
      return "SOS";
    case 0xFE:
      return "COM";
    default:
      return "Marker 0x" + hex_byte(marker);
  }
}

std::string mpf_summary(Span data) {
  // "MPF\0" + TIFF stream with the MP Index IFD (B000 version, B001 count, B002 entries).
  const auto stream = parse_tiff(data.subspan(4));
  if (!stream || stream->ifds.empty()) {
    return {};
  }
  const auto& ifd = stream->ifds.front();
  std::string text;
  if (const auto* count = ifd.find(0xB001)) {
    if (const auto value = stream->unsigned_value(*count)) {
      text = std::to_string(*value) + " images";
    }
  }
  if (const auto* entries = ifd.find(0xB002)) {
    std::string types;
    for (std::size_t i = 0; i + 16 <= entries->data.size() && i < 16 * 8; i += 16) {
      const auto attribute = stream->big_endian ? be32(entries->data, i) : le32(entries->data, i);
      const auto size = stream->big_endian ? be32(entries->data, i + 4) : le32(entries->data, i + 4);
      const auto type = attribute & 0xFFFFFFU;
      std::string name;
      switch (type) {
        case 0x030000:
          name = "primary";
          break;
        case 0x010001:
          name = "large thumbnail (VGA)";
          break;
        case 0x010002:
          name = "large thumbnail (full HD)";
          break;
        case 0x020001:
          name = "panorama frame";
          break;
        case 0x020002:
          name = "disparity (stereo)";
          break;
        case 0x020003:
          name = "multi-angle";
          break;
        default:
          name = "undefined (gain map or other)";
          break;
      }
      types += (types.empty() ? "" : ", ") + name + " " + size_text(size);
    }
    if (!types.empty()) {
      text += (text.empty() ? "" : ": ") + types;
    }
  }
  return text;
}

void extract_jpeg(Span data, Builder& builder, bool embedded = false) {
  std::size_t pos = 2;
  std::map<std::string, std::vector<std::pair<std::uint32_t, Bytes>>> extended_chunks;
  std::map<std::string, std::uint32_t> extended_lengths;
  std::map<std::uint8_t, Bytes> icc_chunks;
  Bytes jumbf;
  std::size_t end_of_image = 0;
  while (pos + 4 <= data.size()) {
    if (data[pos] != 0xFF) {
      break;
    }
    std::uint8_t marker = data[pos + 1];
    if (marker == 0xFF) {
      ++pos;  // fill byte
      continue;
    }
    if (marker == 0xD9) {
      end_of_image = pos + 2;
      builder.segment("EOI", pos, 2);
      break;
    }
    if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
      pos += 2;
      continue;
    }
    const std::size_t length = be16(data, pos + 2);
    if (length < 2 || pos + 2 + length > data.size()) {
      break;
    }
    const auto payload = data.subspan(pos + 4, length - 2);
    std::string name = jpeg_marker_name(marker);
    if (marker == 0xE0 && starts_with(payload, std::string_view("JFIF\0", 5))) {
      name += " JFIF";
      if (payload.size() >= 14 && payload[12] > 0 && payload[13] > 0) {
        builder.observe("JPEG APP0 JFIF", "JFIF thumbnail",
                        std::to_string(payload[12]) + " x " + std::to_string(payload[13]) + " px",
                        static_cast<std::uint64_t>(payload[12]) * payload[13] * 3, MetadataPrivacy::HiddenImage);
      }
    } else if (marker == 0xE0 && starts_with(payload, std::string_view("JFXX\0", 5))) {
      name += " JFXX";
      if (payload.size() > 6 && payload[5] == 0x10) {
        const auto thumbnail = payload.subspan(6);
        builder.observe("JPEG APP0 JFXX", "JFIF extension thumbnail", size_text(thumbnail.size()), thumbnail.size(),
                        MetadataPrivacy::HiddenImage, make_metadata_bytes(to_bytes(thumbnail)), true);
      }
    } else if (marker == 0xE1 && (starts_with(payload, std::string_view("Exif\0\0", 6)) ||
                                  starts_with(payload, std::string_view("Exif\0\xFF", 6)))) {
      name += " Exif";
      builder.set_exif(payload.subspan(6));
    } else if (marker == 0xE1 && starts_with(payload, std::string_view("http://ns.adobe.com/xap/1.0/\0", 29))) {
      name += " XMP";
      builder.set_xmp(payload.subspan(29));
    } else if (marker == 0xE1 && starts_with(payload, std::string_view("http://ns.adobe.com/xmp/extension/\0", 35))) {
      name += " Extended XMP";
      if (payload.size() >= 35 + 40) {
        const std::string guid(reinterpret_cast<const char*>(payload.data() + 35), 32);
        const auto full_length = be32(payload, 67);
        const auto offset = be32(payload, 71);
        extended_lengths[guid] = full_length;
        extended_chunks[guid].emplace_back(offset, to_bytes(payload.subspan(75)));
      }
    } else if (marker == 0xE1 && starts_with(payload, "FLIR")) {
      name += " FLIR";
      builder.observe("JPEG APP1", "FLIR thermal data", size_text(payload.size()), payload.size(),
                      MetadataPrivacy::None);
    } else if (marker == 0xE2 && starts_with(payload, std::string_view("ICC_PROFILE\0", 12))) {
      name += " ICC profile";
      if (payload.size() > 14) {
        icc_chunks[payload[12]] = to_bytes(payload.subspan(14));
      }
    } else if (marker == 0xE2 && starts_with(payload, std::string_view("MPF\0", 4))) {
      name += " MPF";
      builder.observe("JPEG APP2 MPF", "Multi-Picture Format index", mpf_summary(payload), payload.size(),
                      MetadataPrivacy::HiddenImage);
    } else if (marker == 0xEB && starts_with(payload, "JP") && payload.size() > 8) {
      name += " JUMBF";
      // CI 'JP', En (2), Z sequence (4), then the box; continuation segments repeat
      // the 8-byte box header.
      const auto sequence = be32(payload, 4);
      const auto box = payload.subspan(8);
      if (sequence <= 1 || jumbf.empty()) {
        jumbf.insert(jumbf.end(), box.begin(), box.end());
      } else if (box.size() > 8) {
        jumbf.insert(jumbf.end(), box.begin() + 8, box.end());
      }
    } else if (marker == 0xEC && starts_with(payload, "Ducky")) {
      name += " Ducky";
      std::string summary;
      if (payload.size() >= 13 && be16(payload, 5) == 1) {
        summary = "Save for Web quality " + std::to_string(be32(payload, 9));
      }
      builder.observe("JPEG APP12", "Photoshop Save for Web settings (Ducky)", summary, payload.size(),
                      MetadataPrivacy::None);
    } else if (marker == 0xED && starts_with(payload, std::string_view("Photoshop 3.0\0", 14))) {
      name += " Photoshop IRB";
      builder.read_irb(payload.subspan(14), "JPEG APP13");
    } else if (marker == 0xEE && starts_with(payload, "Adobe")) {
      name += " Adobe";
    } else if (marker == 0xFE) {
      builder.info.text.push_back(FileInfoText{FileInfoTextKind::Comment, {}, legacy_text_to_utf8(payload), {}});
    } else if (marker >= 0xE0 && marker <= 0xEF) {
      std::string identifier;
      for (std::size_t i = 0; i < payload.size() && i < 32 && payload[i] >= 0x20 && payload[i] < 0x7F; ++i) {
        identifier.push_back(static_cast<char>(payload[i]));
      }
      if (!identifier.empty()) {
        name += " " + identifier;
      }
      builder.observe("JPEG " + jpeg_marker_name(marker), identifier.empty() ? "Application data" : identifier,
                      size_text(payload.size()), payload.size(), MetadataPrivacy::None);
    }
    builder.segment(name, pos, length + 2);
    pos += 2 + length;
    if (marker == 0xDA) {
      // Entropy-coded data runs to the next marker that is not a stuffed 0xFF00 or a
      // restart marker.
      while (pos + 1 < data.size()) {
        if (data[pos] == 0xFF && data[pos + 1] != 0x00 && !(data[pos + 1] >= 0xD0 && data[pos + 1] <= 0xD7) &&
            data[pos + 1] != 0xFF) {
          break;
        }
        ++pos;
      }
    }
  }
  if (!icc_chunks.empty()) {
    Bytes profile;
    for (const auto& [sequence, chunk] : icc_chunks) {
      profile.insert(profile.end(), chunk.begin(), chunk.end());
    }
    builder.set_icc(profile);
  }
  if (!extended_chunks.empty() && builder.info.xmp) {
    for (auto& [guid, chunks] : extended_chunks) {
      std::sort(chunks.begin(), chunks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      Bytes full;
      for (const auto& [offset, chunk] : chunks) {
        if (offset == full.size()) {
          full.insert(full.end(), chunk.begin(), chunk.end());
        }
      }
      if (full.size() != extended_lengths[guid]) {
        continue;
      }
      // The GUID is the MD5 of the full extended packet; a mismatch means a damaged
      // or mismatched extension, which readers must ignore.
      auto expected = guid;
      std::transform(expected.begin(), expected.end(), expected.begin(),
                     [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
      if (md5_hex(full) != expected) {
        continue;
      }
      builder.info.xmp = make_metadata_bytes(merge_extended_xmp(*builder.info.xmp, full));
      break;
    }
  }
  if (!jumbf.empty()) {
    builder.observe_c2pa("JPEG APP11", jumbf);
  }
  if (!embedded && end_of_image != 0 && end_of_image < data.size()) {
    const auto trailer = data.size() - end_of_image;
    builder.segment("Data after end of image", end_of_image, trailer);
    builder.observe("JPEG trailer", "Data after end of image",
                    "Secondary images, gain maps or motion-photo video appended to the file", trailer,
                    MetadataPrivacy::HiddenImage);
  }
}

// ----------------------------------------------------------------------- PNG

std::string latin1_to_utf8(Span data) {
  std::string out;
  for (const auto byte : data) {
    if (byte < 0x80) {
      out.push_back(static_cast<char>(byte));
    } else {
      out.push_back(static_cast<char>(0xC0U | (byte >> 6)));
      out.push_back(static_cast<char>(0x80U | (byte & 0x3FU)));
    }
  }
  return out;
}

std::string png_chunk_type(Span data, std::size_t at) {
  return std::string(reinterpret_cast<const char*>(data.data() + at), 4);
}

void png_text(Builder& builder, FileInfoTextKind kind, std::string key, std::string value, std::string language) {
  // ImageMagick and exiftool store binary profiles as hex text.
  if (key.starts_with("Raw profile type ")) {
    const auto profile_name = key.substr(17);
    auto bytes = decode_raw_profile_hex(value);
    if (profile_name == "exif" || profile_name == "APP1") {
      builder.set_exif(strip_exif_prefix(bytes));
      return;
    }
    if (profile_name == "xmp") {
      builder.set_xmp(bytes);
      return;
    }
    if (profile_name == "iptc") {
      // Either a bare IIM block or a Photoshop IRB holding one.
      if (parse_iim(bytes)) {
        if (!builder.info.iptc) {
          builder.info.iptc = make_metadata_bytes(std::move(bytes));
        }
      } else {
        builder.read_irb(bytes, "PNG " + key);
      }
      return;
    }
    if (profile_name == "8bim") {
      builder.read_irb(bytes, "PNG " + key);
      return;
    }
    if (profile_name == "icc" || profile_name == "icm") {
      builder.set_icc(bytes);
      return;
    }
  }
  if (key == "XML:com.adobe.xmp") {
    builder.set_xmp(Span(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
    return;
  }
  builder.info.text.push_back(FileInfoText{kind, std::move(key), std::move(value), std::move(language)});
}

void extract_png(Span data, Builder& builder) {
  std::size_t pos = 8;
  while (pos + 12 <= data.size()) {
    const auto length = be32(data, pos);
    if (length > data.size() - pos - 12) {
      break;
    }
    const auto type = png_chunk_type(data, pos + 4);
    const auto payload = data.subspan(pos + 8, length);
    builder.segment(type, pos, static_cast<std::uint64_t>(length) + 12);
    if (type == "tEXt") {
      const auto nul = std::find(payload.begin(), payload.end(), std::uint8_t{0});
      if (nul != payload.end()) {
        const auto key_length = static_cast<std::size_t>(nul - payload.begin());
        png_text(builder, FileInfoTextKind::PngText, latin1_to_utf8(payload.first(key_length)),
                 latin1_to_utf8(payload.subspan(key_length + 1)), {});
      }
    } else if (type == "zTXt") {
      const auto nul = std::find(payload.begin(), payload.end(), std::uint8_t{0});
      if (nul != payload.end() && static_cast<std::size_t>(nul - payload.begin()) + 2 <= payload.size()) {
        const auto key_length = static_cast<std::size_t>(nul - payload.begin());
        const auto inflated = inflate_zlib_capped(payload.subspan(key_length + 2), kMaxInflatedText);
        png_text(builder, FileInfoTextKind::PngCompressedText, latin1_to_utf8(payload.first(key_length)),
                 latin1_to_utf8(inflated), {});
      }
    } else if (type == "iTXt") {
      // keyword \0 compression-flag method language \0 translated-keyword \0 text
      const auto nul = std::find(payload.begin(), payload.end(), std::uint8_t{0});
      std::size_t at = static_cast<std::size_t>(nul - payload.begin());
      if (nul != payload.end() && at + 3 <= payload.size()) {
        const std::string key(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(at));
        const bool compressed = payload[at + 1] != 0;
        at += 3;
        const auto language_end = std::find(payload.begin() + static_cast<std::ptrdiff_t>(at), payload.end(),
                                            std::uint8_t{0});
        std::string language(payload.begin() + static_cast<std::ptrdiff_t>(at), language_end);
        if (language_end != payload.end()) {
          at = static_cast<std::size_t>(language_end - payload.begin()) + 1;
          const auto translated_end = std::find(payload.begin() + static_cast<std::ptrdiff_t>(at), payload.end(),
                                                std::uint8_t{0});
          if (translated_end != payload.end()) {
            at = static_cast<std::size_t>(translated_end - payload.begin()) + 1;
            const auto body = payload.subspan(at);
            const auto text = compressed ? inflate_zlib_capped(body, kMaxInflatedText) : to_bytes(body);
            png_text(builder, FileInfoTextKind::PngUnicodeText, key, std::string(text.begin(), text.end()),
                     std::move(language));
          }
        }
      }
    } else if (type == "eXIf") {
      builder.set_exif(strip_exif_prefix(payload));
    } else if (type == "iCCP") {
      const auto nul = std::find(payload.begin(), payload.end(), std::uint8_t{0});
      const auto at = static_cast<std::size_t>(nul - payload.begin());
      if (nul != payload.end() && at + 2 <= payload.size()) {
        builder.set_icc(inflate_zlib_capped(payload.subspan(at + 2), 64U * 1024U * 1024U));
      }
    } else if (type == "sRGB" || type == "gAMA" || type == "cHRM" || type == "cICP" || type == "mDCV" ||
               type == "cLLI" || type == "sBIT") {
      FileInfoChunk chunk;
      std::copy(type.begin(), type.end(), chunk.type.begin());
      chunk.data = to_bytes(payload);
      builder.info.png_color_chunks.push_back(std::move(chunk));
    } else if (type == "tIME" && payload.size() == 7) {
      char buffer[48];
      std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u:%02u UTC",
                    static_cast<unsigned>(be16(payload, 0)), payload[2], payload[3], payload[4], payload[5],
                    payload[6]);
      builder.observe("PNG tIME", "Last modification time", buffer, payload.size(), MetadataPrivacy::Timestamp);
    } else if (type == "caBX") {
      builder.observe_c2pa("PNG caBX", payload);
    } else if (type == "IEND") {
      break;
    }
    pos += static_cast<std::size_t>(length) + 12;
  }
}

// ---------------------------------------------------------------------- TIFF

// Tags that describe a TIFF file's image data rather than the image. They stay out
// of the EXIF slot, which other formats write back as their EXIF block.
bool is_tiff_structure_tag(std::uint16_t tag) {
  switch (tag) {
    case 254:
    case 255:
    case 256:
    case 257:
    case 258:
    case 259:
    case 262:
    case 263:
    case 264:
    case 265:
    case 266:
    case 273:
    case 277:
    case 278:
    case 279:
    case 280:
    case 281:
    case 284:
    case 292:
    case 293:
    case 317:
    case 320:
    case 322:
    case 323:
    case 324:
    case 325:
    case 330:
    case 338:
    case 339:
    case 340:
    case 341:
    case 347:
    case 529:
    case 530:
    case 532:
    case 700:
    case 33723:
    case 34377:
    case 34675:
    case 37724:
      return true;
    default:
      return false;
  }
}

void extract_tiff(Span data, Builder& builder) {
  TiffParseOptions options;
  options.follow_page_chain = true;
  options.follow_sub_ifds = true;
  const auto stream = parse_tiff(data, options);
  if (!stream) {
    return;
  }
  const auto* ifd0 = stream->find(IfdKind::Ifd0);
  if (ifd0 == nullptr) {
    return;
  }
  builder.segment("TIFF header (" + std::string(stream->big_endian ? "big" : "little") + "-endian)", 0, 8);
  std::size_t pages = 0;
  for (const auto& ifd : stream->ifds) {
    if (ifd.kind == IfdKind::Ifd0 || ifd.kind == IfdKind::Ifd1 || ifd.kind == IfdKind::Chained) {
      ++pages;
    }
  }
  if (pages > 1) {
    builder.segment(std::to_string(pages) + " image directories", 0, 0);
  }
  for (const auto& entry : ifd0->entries) {
    switch (entry.tag) {
      case 700:
        builder.set_xmp(entry.data);
        builder.segment("Tag 700 XMP", 0, entry.data.size());
        break;
      case 33723:
        if (!builder.info.iptc && parse_iim(entry.data)) {
          builder.info.iptc = make_metadata_bytes(entry.data);
        }
        builder.segment("Tag 33723 IPTC", 0, entry.data.size());
        break;
      case 34377:
        builder.read_irb(entry.data, "TIFF tag 34377");
        builder.segment("Tag 34377 Photoshop IRB", 0, entry.data.size());
        break;
      case 34675:
        builder.set_icc(entry.data);
        builder.segment("Tag 34675 ICC profile", 0, entry.data.size());
        break;
      case 37724:
        builder.observe("TIFF tag 37724", "Photoshop layer data (ImageSourceData)", size_text(entry.data.size()),
                        entry.data.size(), MetadataPrivacy::HiddenImage);
        break;
      default:
        break;
    }
  }
  // EXIF slot: IFD0's descriptive tags plus the Exif/GPS/Interop directories.
  TiffStream exif;
  exif.big_endian = stream->big_endian;
  auto& out0 = exif.find_or_add(IfdKind::Ifd0);
  for (const auto& entry : ifd0->entries) {
    if (!is_tiff_structure_tag(entry.tag) && entry.tag != kTagExifIfd && entry.tag != kTagGpsIfd) {
      out0.entries.push_back(entry);
    }
  }
  for (const auto kind : {IfdKind::Exif, IfdKind::Gps, IfdKind::Interop}) {
    if (const auto* ifd = stream->find(kind)) {
      exif.ifds.push_back(*ifd);
    }
  }
  if (exif.ifds.size() > 1 || !out0.entries.empty()) {
    builder.set_exif(write_tiff(exif));
  }
  std::size_t previews = 0;
  for (const auto& ifd : stream->ifds) {
    if (ifd.kind == IfdKind::SubIfd || ifd.kind == IfdKind::Ifd1) {
      ++previews;
    }
  }
  if (previews > 0 && pages <= 2) {
    builder.observe("TIFF", "Preview images", std::to_string(previews) + " reduced-resolution images", 0,
                    MetadataPrivacy::HiddenImage);
  }
}

// ---------------------------------------------------------------------- WebP

void extract_webp(Span data, Builder& builder) {
  std::size_t pos = 12;
  const std::size_t end = std::min<std::size_t>(data.size(), static_cast<std::size_t>(le32(data, 4)) + 8);
  builder.segment("RIFF WEBP header", 0, 12);
  std::size_t frames = 0;
  while (pos + 8 <= end) {
    const auto type = png_chunk_type(data, pos);
    const std::size_t length = le32(data, pos + 4);
    if (length > end - pos - 8) {
      break;
    }
    const auto payload = data.subspan(pos + 8, length);
    if (type == "ANMF") {
      ++frames;
      if (frames <= 64) {
        builder.segment("ANMF frame " + std::to_string(frames), pos, length + 8);
      }
    } else {
      builder.segment(type, pos, length + 8);
    }
    if (type == "ICCP") {
      builder.set_icc(payload);
    } else if (type == "EXIF") {
      builder.set_exif(strip_exif_prefix(payload));
    } else if (type == "XMP ") {
      builder.set_xmp(payload);
    } else if (type == "C2PA") {
      builder.observe_c2pa("WebP C2PA", payload);
    }
    pos += 8 + length + (length % 2);
  }
  if (frames > 64) {
    builder.segment("... " + std::to_string(frames - 64) + " more frames", 0, 0);
  }
}

// ------------------------------------------------------------------- JPEG XL

// libjxl reads the boxes (decompressing brob ones) and the original color profile.
void extract_jxl(Span data, Builder& builder) {
  const auto metadata = jxl::read_metadata(data);
  if (metadata.container) {
    for (const auto& box : metadata.boxes) {
      builder.segment(box.type + " box", box.offset, box.size);
    }
  } else {
    builder.segment("JPEG XL codestream", 0, data.size());
  }
  if (!metadata.icc.empty()) {
    builder.set_icc(metadata.icc);
  }
  if (!metadata.exif.empty()) {
    builder.set_exif(metadata.exif);
  }
  if (!metadata.xmp.empty()) {
    builder.set_xmp(metadata.xmp);
  }
}

// ----------------------------------------------------------------------- GIF

Bytes gif_sub_blocks(Span data, std::size_t& pos) {
  Bytes out;
  while (pos < data.size()) {
    const std::size_t size = data[pos++];
    if (size == 0) {
      break;
    }
    if (size > data.size() - pos) {
      pos = data.size();
      break;
    }
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(pos),
               data.begin() + static_cast<std::ptrdiff_t>(pos + size));
    pos += size;
  }
  return out;
}

void skip_gif_sub_blocks(Span data, std::size_t& pos) {
  while (pos < data.size()) {
    const std::size_t size = data[pos++];
    if (size == 0) {
      return;
    }
    pos += size;
  }
}

void extract_gif(Span data, Builder& builder) {
  if (data.size() < 13) {
    return;
  }
  std::size_t pos = 13;
  builder.segment("Header and logical screen", 0, 13);
  if ((data[10] & 0x80) != 0) {
    const std::size_t table = 3U * (1U << ((data[10] & 0x07) + 1));
    builder.segment("Global color table", pos, table);
    pos += table;
  }
  std::size_t frames = 0;
  while (pos < data.size()) {
    const auto block_start = pos;
    const auto introducer = data[pos++];
    if (introducer == 0x3B) {
      builder.segment("Trailer", block_start, 1);
      break;
    }
    if (introducer == 0x2C) {
      if (pos + 9 > data.size()) {
        break;
      }
      const auto flags = data[pos + 8];
      pos += 9;
      if ((flags & 0x80) != 0) {
        pos += 3U * (1U << ((flags & 0x07) + 1));
      }
      ++pos;  // LZW minimum code size
      skip_gif_sub_blocks(data, pos);
      ++frames;
      if (frames <= 64) {
        builder.segment("Image " + std::to_string(frames), block_start, pos - block_start);
      }
      continue;
    }
    if (introducer != 0x21 || pos >= data.size()) {
      break;
    }
    const auto label = data[pos++];
    if (label == 0xFE) {
      const auto comment = gif_sub_blocks(data, pos);
      builder.info.text.push_back(FileInfoText{FileInfoTextKind::Comment, {}, legacy_text_to_utf8(comment), {}});
      builder.segment("Comment extension", block_start, pos - block_start);
      continue;
    }
    if (label == 0xFF && pos < data.size() && data[pos] == 11 && pos + 12 <= data.size()) {
      const std::string identifier(reinterpret_cast<const char*>(data.data() + pos + 1), 11);
      pos += 12;
      if (identifier == "XMP DataXMP") {
        // Raw XMP followed by a 258-byte "magic trailer" that makes the packet look
        // like sub-blocks to readers that do not know the extension.
        const std::string_view rest(reinterpret_cast<const char*>(data.data() + pos), data.size() - pos);
        const auto end_marker = rest.find("<?xpacket end=");
        const auto close = end_marker == std::string_view::npos ? std::string_view::npos : rest.find("?>", end_marker);
        if (close != std::string_view::npos) {
          builder.set_xmp(data.subspan(pos, close + 2));
          pos += close + 2;
          // Skip the trailer: 0x01, 0xFF..0x00, then the block terminator.
          pos = std::min(data.size(), pos + 258);
        } else {
          skip_gif_sub_blocks(data, pos);
        }
        builder.segment("XMP application extension", block_start, pos - block_start);
        continue;
      }
      if (identifier == "ICCRGBG1012") {
        builder.set_icc(gif_sub_blocks(data, pos));
        builder.segment("ICC profile extension", block_start, pos - block_start);
        continue;
      }
      skip_gif_sub_blocks(data, pos);
      if (identifier != "NETSCAPE2.0" && identifier != "ANIMEXTS1.0") {
        builder.observe("GIF application extension", identifier, {}, pos - block_start, MetadataPrivacy::None);
      }
      builder.segment("Application extension " + identifier, block_start, pos - block_start);
      continue;
    }
    skip_gif_sub_blocks(data, pos);
    if (frames < 64) {
      builder.segment(label == 0xF9 ? "Graphic control extension" : "Extension 0x" + hex_byte(label), block_start,
                      pos - block_start);
    }
  }
  if (frames > 64) {
    builder.segment("... " + std::to_string(frames - 64) + " more frames", 0, 0);
  }
}

// ------------------------------------------------------------------- ISOBMFF

struct Box {
  std::string type;
  Span payload;
  std::size_t offset{0};
  std::size_t size{0};
};

std::vector<Box> parse_boxes(Span data, std::size_t base_offset) {
  std::vector<Box> boxes;
  std::size_t pos = 0;
  while (pos + 8 <= data.size() && boxes.size() < 4096) {
    std::uint64_t size = be32(data, pos);
    const auto type = png_chunk_type(data, pos + 4);
    std::size_t header = 8;
    if (size == 1) {
      if (pos + 16 > data.size()) {
        break;
      }
      size = be_n(data, pos + 8, 8);
      header = 16;
    } else if (size == 0) {
      size = data.size() - pos;
    }
    if (size < header || size > data.size() - pos) {
      break;
    }
    boxes.push_back(Box{type, data.subspan(pos + header, static_cast<std::size_t>(size) - header), base_offset + pos,
                        static_cast<std::size_t>(size)});
    pos += static_cast<std::size_t>(size);
  }
  return boxes;
}

const Box* find_box(const std::vector<Box>& boxes, std::string_view type) {
  for (const auto& box : boxes) {
    if (box.type == type) {
      return &box;
    }
  }
  return nullptr;
}

constexpr std::array<std::uint8_t, 16> kCanonCr3Uuid{0x85, 0xC0, 0xB6, 0x87, 0x82, 0x0F, 0x11, 0xE0,
                                                     0x81, 0x11, 0xF4, 0xCE, 0x46, 0x2B, 0x6A, 0x48};
constexpr std::array<std::uint8_t, 16> kXmpUuid{0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                                                0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};
constexpr std::array<std::uint8_t, 16> kC2paUuid{0xD8, 0xFE, 0xC3, 0xD6, 0x1B, 0x0E, 0x48, 0x3C,
                                                 0x92, 0x97, 0x58, 0x28, 0x87, 0x7E, 0xC4, 0x81};

bool uuid_is(Span payload, const std::array<std::uint8_t, 16>& uuid) {
  return payload.size() >= 16 && std::equal(uuid.begin(), uuid.end(), payload.begin());
}

struct HeifItem {
  std::uint32_t id{0};
  std::string type;
  std::string name;
  std::string content_type;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;  // offset, length
  int construction_method{0};
  std::uint64_t base_offset{0};
  std::vector<std::uint16_t> properties;  // 1-based ipco indices
};

void parse_iinf(Span payload, std::map<std::uint32_t, HeifItem>& items) {
  if (payload.size() < 6) {
    return;
  }
  const auto version = payload[0];
  std::size_t pos = 4 + (version == 0 ? 2 : 4);
  for (const auto& infe : parse_boxes(payload.subspan(std::min(pos, payload.size())), 0)) {
    if (infe.type != "infe" || infe.payload.size() < 4) {
      continue;
    }
    const auto& p = infe.payload;
    const auto infe_version = p[0];
    if (infe_version < 2) {
      continue;
    }
    std::size_t at = 4;
    HeifItem item;
    if (infe_version == 2) {
      if (at + 8 > p.size()) {
        continue;
      }
      item.id = be16(p, at);
      at += 2;
    } else {
      if (at + 10 > p.size()) {
        continue;
      }
      item.id = be32(p, at);
      at += 4;
    }
    at += 2;  // protection index
    item.type = png_chunk_type(p, at);
    at += 4;
    const auto name_end = std::find(p.begin() + static_cast<std::ptrdiff_t>(at), p.end(), std::uint8_t{0});
    item.name.assign(p.begin() + static_cast<std::ptrdiff_t>(at), name_end);
    if (item.type == "mime" && name_end != p.end()) {
      at = static_cast<std::size_t>(name_end - p.begin()) + 1;
      const auto type_end = std::find(p.begin() + static_cast<std::ptrdiff_t>(at), p.end(), std::uint8_t{0});
      item.content_type.assign(p.begin() + static_cast<std::ptrdiff_t>(at), type_end);
    }
    items[item.id] = std::move(item);
  }
}

void parse_iloc(Span p, std::map<std::uint32_t, HeifItem>& items) {
  if (p.size() < 8) {
    return;
  }
  const auto version = p[0];
  std::size_t at = 4;
  const std::size_t offset_size = p[at] >> 4;
  const std::size_t length_size = p[at] & 0x0F;
  const std::size_t base_offset_size = p[at + 1] >> 4;
  const std::size_t index_size = (version == 1 || version == 2) ? (p[at + 1] & 0x0F) : 0;
  at += 2;
  const auto valid = [](std::size_t size) { return size == 0 || size == 4 || size == 8; };
  if (!valid(offset_size) || !valid(length_size) || !valid(base_offset_size) || !valid(index_size)) {
    return;
  }
  std::uint32_t count = 0;
  if (version < 2) {
    count = be16(p, at);
    at += 2;
  } else {
    if (at + 4 > p.size()) {
      return;
    }
    count = be32(p, at);
    at += 4;
  }
  for (std::uint32_t i = 0; i < count && i < 65536; ++i) {
    HeifItem location;
    std::uint32_t id = 0;
    const std::size_t id_size = version < 2 ? 2 : 4;
    if (at + id_size > p.size()) {
      return;
    }
    id = static_cast<std::uint32_t>(be_n(p, at, id_size));
    at += id_size;
    int method = 0;
    if (version == 1 || version == 2) {
      if (at + 2 > p.size()) {
        return;
      }
      method = be16(p, at) & 0x0F;
      at += 2;
    }
    if (at + 2 + base_offset_size + 2 > p.size()) {
      return;
    }
    at += 2;  // data reference index
    const auto base = be_n(p, at, base_offset_size);
    at += base_offset_size;
    const auto extent_count = be16(p, at);
    at += 2;
    auto& item = items[id];
    item.id = id;
    item.construction_method = method;
    item.base_offset = base;
    for (std::uint32_t e = 0; e < extent_count; ++e) {
      if (at + index_size + offset_size + length_size > p.size()) {
        return;
      }
      at += index_size;
      const auto offset = be_n(p, at, offset_size);
      at += offset_size;
      const auto length = be_n(p, at, length_size);
      at += length_size;
      item.extents.emplace_back(offset, length);
    }
  }
}

void parse_ipma(Span p, std::map<std::uint32_t, HeifItem>& items) {
  if (p.size() < 8) {
    return;
  }
  const auto version = p[0];
  const bool wide = (p[3] & 1) != 0;
  std::size_t at = 4;
  const auto count = be32(p, at);
  at += 4;
  for (std::uint32_t i = 0; i < count && i < 65536; ++i) {
    const std::size_t id_size = version < 1 ? 2 : 4;
    if (at + id_size + 1 > p.size()) {
      return;
    }
    const auto id = static_cast<std::uint32_t>(be_n(p, at, id_size));
    at += id_size;
    const auto associations = p[at++];
    for (std::uint32_t a = 0; a < associations; ++a) {
      if (at + (wide ? 2 : 1) > p.size()) {
        return;
      }
      const std::uint16_t index = wide ? static_cast<std::uint16_t>(be16(p, at) & 0x7FFF)
                                       : static_cast<std::uint16_t>(p[at] & 0x7F);
      at += wide ? 2 : 1;
      if (const auto it = items.find(id); it != items.end()) {
        it->second.properties.push_back(index);
      }
    }
  }
}

Bytes heif_item_bytes(Span file, const HeifItem& item, Span idat) {
  Bytes out;
  const Span source = item.construction_method == 1 ? idat : file;
  if (item.construction_method > 1) {
    return out;
  }
  for (const auto& [offset, length] : item.extents) {
    const auto start = item.base_offset + offset;
    const auto size = length == 0 ? (start <= source.size() ? source.size() - start : 0) : length;
    if (start > source.size() || size > source.size() - start || out.size() + size > 256ULL * 1024ULL * 1024ULL) {
      return {};
    }
    out.insert(out.end(), source.begin() + static_cast<std::ptrdiff_t>(start),
               source.begin() + static_cast<std::ptrdiff_t>(start + size));
  }
  return out;
}

std::string nclx_summary(Span colr) {
  // 'nclx' + u16 primaries + u16 transfer + u16 matrix + u8 full-range flag.
  if (colr.size() < 11) {
    return {};
  }
  const auto primaries = be16(colr, 4);
  const auto transfer = be16(colr, 6);
  const auto matrix = be16(colr, 8);
  const bool full_range = (colr[10] & 0x80) != 0;
  std::string text = "CICP primaries " + std::to_string(primaries) + ", transfer " + std::to_string(transfer) +
                     ", matrix " + std::to_string(matrix) + (full_range ? ", full range" : ", limited range");
  if (primaries == 1 && transfer == 13) {
    text += " (sRGB)";
  } else if (primaries == 12) {
    text += " (Display P3)";
  } else if (primaries == 9 && transfer == 16) {
    text += " (Rec. 2100 PQ)";
  } else if (primaries == 9 && transfer == 18) {
    text += " (Rec. 2100 HLG)";
  }
  return text;
}

void merge_cr3_tiff(Span data, IfdKind target, TiffStream& merged) {
  const auto stream = parse_tiff(data);
  if (!stream || stream->ifds.empty()) {
    return;
  }
  if (merged.ifds.empty()) {
    merged.big_endian = stream->big_endian;
  }
  if (merged.big_endian != stream->big_endian) {
    return;  // mixed byte orders cannot share raw value bytes
  }
  auto& ifd = merged.find_or_add(target);
  for (const auto& entry : stream->ifds.front().entries) {
    if (ifd.find(entry.tag) == nullptr && !is_tiff_structure_tag(entry.tag)) {
      ifd.entries.push_back(entry);
    }
  }
}

void extract_bmff(Span data, Builder& builder) {
  const auto top = parse_boxes(data, 0);
  for (const auto& box : top) {
    builder.segment("Box " + box.type, box.offset, box.size);
    if (box.type == "uuid" && uuid_is(box.payload, kXmpUuid)) {
      builder.set_xmp(box.payload.subspan(16));
    } else if (box.type == "uuid" && uuid_is(box.payload, kC2paUuid)) {
      // C2PA BMFF box: uuid, FullBox header, box purpose string, then (for a
      // manifest) a merkle offset and the JUMBF store.
      auto rest = box.payload.subspan(16 + 4);
      const auto purpose_end = std::find(rest.begin(), rest.end(), std::uint8_t{0});
      std::size_t skip = static_cast<std::size_t>(purpose_end - rest.begin()) + 1 + 8;
      if (skip < rest.size()) {
        builder.observe_c2pa("BMFF uuid (C2PA)", rest.subspan(skip));
      }
    } else if (box.type == "moov") {
      // Canon CR3: moov/uuid(85c0b687...) holds CMT1 (IFD0), CMT2 (Exif), CMT3
      // (MakerNote), CMT4 (GPS) as standalone TIFF streams.
      for (const auto& child : parse_boxes(box.payload, box.offset + 8)) {
        if (child.type != "uuid" || !uuid_is(child.payload, kCanonCr3Uuid)) {
          continue;
        }
        TiffStream merged;
        for (const auto& cmt : parse_boxes(child.payload.subspan(16), child.offset + 24)) {
          if (cmt.type == "CMT1") {
            merge_cr3_tiff(cmt.payload, IfdKind::Ifd0, merged);
          } else if (cmt.type == "CMT2") {
            merge_cr3_tiff(cmt.payload, IfdKind::Exif, merged);
          } else if (cmt.type == "CMT4") {
            merge_cr3_tiff(cmt.payload, IfdKind::Gps, merged);
          } else if (cmt.type == "CMT3") {
            builder.observe("CR3 CMT3", "Canon MakerNote", size_text(cmt.payload.size()), cmt.payload.size(),
                            MetadataPrivacy::DeviceIdentity);
          }
        }
        if (!merged.ifds.empty()) {
          builder.set_exif(write_tiff(merged));
        }
      }
    }
  }
  const auto* meta = find_box(top, "meta");
  if (meta == nullptr || meta->payload.size() < 4) {
    return;
  }
  const auto children = parse_boxes(meta->payload.subspan(4), meta->offset + 12);
  std::map<std::uint32_t, HeifItem> items;
  std::uint32_t primary = 0;
  Span idat;
  if (const auto* iinf = find_box(children, "iinf")) {
    parse_iinf(iinf->payload, items);
  }
  if (const auto* iloc = find_box(children, "iloc")) {
    parse_iloc(iloc->payload, items);
  }
  if (const auto* pitm = find_box(children, "pitm"); pitm != nullptr && pitm->payload.size() >= 6) {
    primary = pitm->payload[0] == 0 ? be16(pitm->payload, 4)
                                    : (pitm->payload.size() >= 8 ? be32(pitm->payload, 4) : 0);
  }
  if (const auto* box = find_box(children, "idat")) {
    idat = box->payload;
  }
  std::vector<Box> properties;
  if (const auto* iprp = find_box(children, "iprp")) {
    const auto iprp_children = parse_boxes(iprp->payload, iprp->offset + 8);
    if (const auto* ipco = find_box(iprp_children, "ipco")) {
      properties = parse_boxes(ipco->payload, ipco->offset + 8);
    }
    for (const auto& child : iprp_children) {
      if (child.type == "ipma") {
        parse_ipma(child.payload, items);
      }
    }
  }
  std::map<std::uint32_t, std::vector<std::pair<std::string, std::uint32_t>>> references;  // from -> (type, to)
  if (const auto* iref = find_box(children, "iref"); iref != nullptr && iref->payload.size() >= 4) {
    const bool wide = iref->payload[0] != 0;
    for (const auto& reference : parse_boxes(iref->payload.subspan(4), 0)) {
      const auto& p = reference.payload;
      const std::size_t id_size = wide ? 4 : 2;
      if (p.size() < id_size + 2) {
        continue;
      }
      const auto from = static_cast<std::uint32_t>(be_n(p, 0, id_size));
      const auto count = be16(p, id_size);
      for (std::size_t i = 0; i < count && id_size + 2 + (i + 1) * id_size <= p.size(); ++i) {
        references[from].emplace_back(reference.type,
                                      static_cast<std::uint32_t>(be_n(p, id_size + 2 + i * id_size, id_size)));
      }
    }
  }
  const auto has_reference = [&](std::uint32_t from, std::string_view type) {
    if (const auto it = references.find(from); it != references.end()) {
      return std::any_of(it->second.begin(), it->second.end(),
                         [&](const auto& reference) { return reference.first == type; });
    }
    return false;
  };
  const auto property_box = [&](std::uint16_t index) -> const Box* {
    return index >= 1 && index <= properties.size() ? &properties[index - 1] : nullptr;
  };
  for (const auto& [id, item] : items) {
    if (item.type == "Exif") {
      const auto bytes = heif_item_bytes(data, item, idat);
      if (bytes.size() > 4) {
        const auto offset = be32(bytes, 0);
        if (offset < bytes.size() - 4) {
          builder.set_exif(strip_exif_prefix(Span(bytes).subspan(4 + offset)));
        }
      }
      builder.segment("Item " + std::to_string(id) + " Exif", 0, bytes.size());
    } else if (item.type == "mime") {
      const auto bytes = heif_item_bytes(data, item, idat);
      if (item.content_type == "application/rdf+xml") {
        builder.set_xmp(bytes);
        builder.segment("Item " + std::to_string(id) + " XMP", 0, bytes.size());
      } else {
        builder.observe("HEIF item " + std::to_string(id), "Metadata item (" + item.content_type + ")",
                        size_text(bytes.size()), bytes.size(), MetadataPrivacy::None);
      }
    } else if (id != primary && has_reference(id, "thmb")) {
      builder.observe("HEIF item " + std::to_string(id), "Thumbnail image (" + item.type + ")", {}, 0,
                      MetadataPrivacy::HiddenImage);
    } else if (id != primary && has_reference(id, "auxl")) {
      std::string kind;
      for (const auto index : item.properties) {
        if (const auto* box = property_box(index); box != nullptr && box->type == "auxC" && box->payload.size() > 4) {
          const auto urn = box->payload.subspan(4);
          kind.assign(urn.begin(), std::find(urn.begin(), urn.end(), std::uint8_t{0}));
        }
      }
      std::string label = "Auxiliary image";
      if (kind.find("alpha") != std::string::npos) {
        label = "Alpha plane";
      } else if (kind.find("depth") != std::string::npos) {
        label = "Depth map";
      } else if (kind.find("gainmap") != std::string::npos || kind.find("hdrgainmap") != std::string::npos) {
        label = "HDR gain map";
      }
      builder.observe("HEIF item " + std::to_string(id), label, kind, 0,
                      label == "Alpha plane" ? MetadataPrivacy::None : MetadataPrivacy::HiddenImage);
    }
  }
  // Color and transform properties of the primary image.
  std::vector<std::uint16_t> primary_properties;
  if (const auto it = items.find(primary); it != items.end()) {
    primary_properties = it->second.properties;
  }
  if (primary_properties.empty()) {
    for (std::uint16_t i = 1; i <= properties.size(); ++i) {
      primary_properties.push_back(i);
    }
  }
  for (const auto index : primary_properties) {
    const auto* box = property_box(index);
    if (box == nullptr) {
      continue;
    }
    if (box->type == "colr" && box->payload.size() >= 4) {
      const auto colour_type = png_chunk_type(box->payload, 0);
      if (colour_type == "prof" || colour_type == "rICC") {
        builder.set_icc(box->payload.subspan(4));
      } else if (colour_type == "nclx") {
        builder.observe("HEIF colr", "Color description", nclx_summary(box->payload), box->payload.size(),
                        MetadataPrivacy::None);
      }
    } else if (box->type == "irot" && !box->payload.empty()) {
      builder.observe("HEIF irot", "Rotation", std::to_string((box->payload[0] & 0x03) * 90) + " degrees (counter-clockwise)",
                      1, MetadataPrivacy::None);
    } else if (box->type == "imir" && !box->payload.empty()) {
      builder.observe("HEIF imir", "Mirror", (box->payload[0] & 1) != 0 ? "Horizontal axis" : "Vertical axis", 1,
                      MetadataPrivacy::None);
    }
  }
}

// ------------------------------------------------------------------------ RAF

void extract_raf(Span data, Builder& builder) {
  if (data.size() < 100) {
    return;
  }
  const auto jpeg_offset = be32(data, 84);
  const auto jpeg_length = be32(data, 88);
  builder.segment("RAF header", 0, 100);
  if (jpeg_offset < data.size() && jpeg_length <= data.size() - jpeg_offset && jpeg_length > 4) {
    builder.segment("Embedded JPEG preview", jpeg_offset, jpeg_length);
    Builder inner;
    extract_jpeg(data.subspan(jpeg_offset, jpeg_length), inner, true);
    builder.info.exif = inner.info.exif;
    builder.info.xmp = inner.info.xmp;
    builder.info.source_icc = inner.info.source_icc;
  }
}

// ------------------------------------------------------------------------ BMP

void extract_bmp(Span data, Builder& builder) {
  if (data.size() < 18) {
    return;
  }
  const auto header_size = le32(data, 14);
  builder.segment("File header", 0, 14);
  builder.segment("Info header (" + std::to_string(header_size) + " bytes)", 14, header_size);
  if (header_size < 124 || data.size() < 14 + 124) {
    return;
  }
  const auto cs_type = le32(data, 14 + 56);
  const auto profile_offset = le32(data, 14 + 112);
  const auto profile_size = le32(data, 14 + 116);
  const std::size_t start = 14 + static_cast<std::size_t>(profile_offset);
  if (profile_size == 0 || start > data.size() || profile_size > data.size() - start) {
    return;
  }
  const auto profile = data.subspan(start, profile_size);
  if (cs_type == 0x4D424544U) {  // 'MBED'
    builder.set_icc(profile);
    builder.segment("Embedded ICC profile", start, profile_size);
  } else if (cs_type == 0x4C494E4BU) {  // 'LINK'
    std::string path = legacy_text_to_utf8(profile);
    while (!path.empty() && path.back() == '\0') {
      path.pop_back();
    }
    builder.observe("BMP V5 header", "Linked color profile path", path, profile_size, MetadataPrivacy::MachineInfo);
  }
}

// ------------------------------------------------------------------------ PDF

std::string pdf_text_string(std::string_view raw) {
  if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE && static_cast<unsigned char>(raw[1]) == 0xFF) {
    std::string out;
    for (std::size_t i = 2; i + 1 < raw.size(); i += 2) {
      std::uint32_t unit = (static_cast<unsigned char>(raw[i]) << 8) | static_cast<unsigned char>(raw[i + 1]);
      if (unit >= 0xD800U && unit <= 0xDBFFU && i + 3 < raw.size()) {
        const std::uint32_t low =
            (static_cast<unsigned char>(raw[i + 2]) << 8) | static_cast<unsigned char>(raw[i + 3]);
        unit = 0x10000U + ((unit - 0xD800U) << 10) + (low - 0xDC00U);
        i += 2;
      }
      if (unit < 0x80U) {
        out.push_back(static_cast<char>(unit));
      } else if (unit < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (unit >> 6)));
        out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
      } else if (unit < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (unit >> 12)));
        out.push_back(static_cast<char>(0x80U | ((unit >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
      } else {
        out.push_back(static_cast<char>(0xF0U | (unit >> 18)));
        out.push_back(static_cast<char>(0x80U | ((unit >> 12) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((unit >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
      }
    }
    return out;
  }
  if (raw.size() >= 3 && raw.substr(0, 3) == "\xEF\xBB\xBF") {
    return std::string(raw.substr(3));
  }
  return legacy_text_to_utf8(Span(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size()));
}

void extract_pdf(Span data, Builder& builder) {
  std::vector<std::string> notices;
  auto file = pdf::File::open(to_bytes(data), &notices);
  if (!file || !file->decryption_ok()) {
    return;
  }
  const auto& info = file->get(file->trailer(), "Info");
  if (const auto* dictionary = info.dictionary()) {
    for (const auto& [key, value] : *dictionary) {
      const auto& resolved = file->resolve(value);
      if (resolved.is_string()) {
        builder.info.text.push_back(
            FileInfoText{FileInfoTextKind::PdfInfo, key, pdf_text_string(resolved.string()), {}});
      } else if (resolved.is_name()) {
        builder.info.text.push_back(FileInfoText{FileInfoTextKind::PdfInfo, key, std::string(resolved.name()), {}});
      }
    }
  }
  const auto& metadata = file->get(file->catalog(), "Metadata");
  if (metadata.is_stream()) {
    const auto stream = file->stream_data(metadata);
    if (stream.error.empty()) {
      builder.set_xmp(stream.data);
    }
  }
  builder.segment("PDF " + file->version(), 0, data.size());
}

// ------------------------------------------------------------------------ SVG

std::string attribute_after(std::string_view text, std::string_view attribute) {
  const auto at = text.find(attribute);
  if (at == std::string_view::npos) {
    return {};
  }
  auto pos = at + attribute.size();
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '=')) {
    ++pos;
  }
  if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
    return {};
  }
  const char quote = text[pos];
  const auto end = text.find(quote, pos + 1);
  return end == std::string_view::npos ? std::string{} : std::string(text.substr(pos + 1, end - pos - 1));
}

std::string element_text(std::string_view text, std::string_view element) {
  const std::string open = "<" + std::string(element);
  auto at = text.find(open);
  while (at != std::string_view::npos) {
    const auto after = at + open.size();
    if (after < text.size() && (text[after] == '>' || text[after] == ' ')) {
      break;
    }
    at = text.find(open, after);
  }
  if (at == std::string_view::npos) {
    return {};
  }
  const auto start = text.find('>', at);
  const auto end = text.find("</" + std::string(element) + ">", at);
  if (start == std::string_view::npos || end == std::string_view::npos || end < start) {
    return {};
  }
  std::string value(text.substr(start + 1, end - start - 1));
  // Collapse whitespace; entities stay as written (display only).
  std::string out;
  bool space = false;
  for (const char c : value) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      space = !out.empty();
      continue;
    }
    if (space) {
      out.push_back(' ');
      space = false;
    }
    out.push_back(c);
  }
  return out;
}

void extract_svg(Span data, Builder& builder) {
  const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
  if (auto title = element_text(text, "title"); !title.empty()) {
    builder.info.text.push_back(FileInfoText{FileInfoTextKind::SvgText, "title", std::move(title), {}});
  }
  if (auto desc = element_text(text, "desc"); !desc.empty()) {
    builder.info.text.push_back(FileInfoText{FileInfoTextKind::SvgText, "desc", std::move(desc), {}});
  }
  const auto metadata_start = text.find("<metadata");
  if (metadata_start != std::string_view::npos) {
    const auto metadata_end = text.find("</metadata>", metadata_start);
    if (metadata_end != std::string_view::npos) {
      // The RDF inside <metadata> is XMP-shaped (Inkscape writes cc:Work with dc:*);
      // its namespace declarations may sit on the root, so re-declare the usual ones.
      std::string wrapped = "<x xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                            "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                            "xmlns:cc=\"http://creativecommons.org/ns#\">";
      const auto body_start = text.find('>', metadata_start);
      if (body_start != std::string_view::npos && body_start < metadata_end) {
        wrapped += text.substr(body_start + 1, metadata_end - body_start - 1);
      }
      wrapped += "</x>";
      if (const auto packet =
              parse_xmp(Span(reinterpret_cast<const std::uint8_t*>(wrapped.data()), wrapped.size()));
          packet && !packet->empty()) {
        const auto serialized = serialize_xmp(*packet, 0);
        builder.set_xmp(Span(reinterpret_cast<const std::uint8_t*>(serialized.data()), serialized.size()));
        builder.segment("<metadata> RDF", metadata_start, metadata_end - metadata_start);
      }
    }
  }
  for (const auto& [attribute, label] :
       {std::pair<std::string_view, std::string_view>{"sodipodi:docname", "Source document name"},
        {"inkscape:export-filename", "Export file path"},
        {"sodipodi:absref", "Absolute image reference"},
        {"inkscape:version", "Inkscape version"}}) {
    if (auto value = attribute_after(text, attribute); !value.empty()) {
      builder.observe("SVG attribute " + std::string(attribute), std::string(label), value, value.size(),
                      MetadataPrivacy::MachineInfo);
    }
  }
}

}  // namespace

std::vector<std::uint8_t> inflate_zlib_capped(std::span<const std::uint8_t> data, std::size_t max_output) {
  std::vector<std::uint8_t> out;
  mz_stream stream{};
  stream.next_in = data.data();
  stream.avail_in = static_cast<unsigned int>(std::min<std::size_t>(data.size(), 0xFFFFFFFFU));
  if (mz_inflateInit(&stream) != MZ_OK) {
    return {};
  }
  std::array<std::uint8_t, 65536> chunk{};
  bool ok = false;
  while (out.size() < max_output) {
    stream.next_out = chunk.data();
    stream.avail_out = static_cast<unsigned int>(chunk.size());
    const auto status = mz_inflate(&stream, MZ_NO_FLUSH);
    const auto produced = chunk.size() - stream.avail_out;
    out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
    if (status == MZ_STREAM_END) {
      ok = true;
      break;
    }
    if (status != MZ_OK) {
      // A truncated stream still yields what was decoded.
      ok = status == MZ_BUF_ERROR && !out.empty();
      break;
    }
  }
  mz_inflateEnd(&stream);
  if (out.size() > max_output) {
    out.resize(max_output);
  }
  if (!ok && out.size() < max_output) {
    return {};
  }
  return out;
}

bool icc_profile_is_rgb(std::span<const std::uint8_t> profile) noexcept {
  return profile.size() >= 128 && profile[16] == 'R' && profile[17] == 'G' && profile[18] == 'B' && profile[19] == ' ';
}

std::string icc_color_space(std::span<const std::uint8_t> profile) {
  if (profile.size() < 128) {
    return {};
  }
  std::string space(reinterpret_cast<const char*>(profile.data() + 16), 4);
  while (!space.empty() && space.back() == ' ') {
    space.pop_back();
  }
  return space;
}

std::string sniff_metadata_format(std::span<const std::uint8_t> bytes) {
  if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
    return "JPEG";
  }
  if (starts_with(bytes, "\x89PNG\r\n\x1A\n")) {
    return "PNG";
  }
  if (starts_with(bytes, "GIF87a") || starts_with(bytes, "GIF89a")) {
    return "GIF";
  }
  if (starts_with(bytes, "RIFF") && starts_with(bytes, "WEBP", 8)) {
    return "WebP";
  }
  if (starts_with(bytes, "8BPS")) {
    return "PSD";
  }
  if (jxl::sniff(bytes)) {
    return "JXL";
  }
  if (starts_with(bytes, "%PDF")) {
    return "PDF";
  }
  if (starts_with(bytes, "FUJIFILMCCD-RAW")) {
    return "RAF";
  }
  if (starts_with(bytes, "BM") && bytes.size() >= 18) {
    return "BMP";
  }
  if (bytes.size() >= 8 && ((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M'))) {
    if (parse_tiff(bytes.first(std::min<std::size_t>(bytes.size(), 64)))) {
      return "TIFF";
    }
    const bool big = bytes[0] == 'M';
    const auto magic = big ? be16(bytes, 2) : static_cast<std::uint16_t>(bytes[2] | (bytes[3] << 8));
    if (magic == 42 || magic == 0x55 || magic == 0x4F52 || magic == 0x5352) {
      return "TIFF";
    }
  }
  if (starts_with(bytes, "ftyp", 4) && bytes.size() >= 12) {
    const std::string brand(reinterpret_cast<const char*>(bytes.data() + 8), 4);
    if (brand == "crx ") {
      return "CR3";
    }
    if (brand == "avif" || brand == "avis") {
      return "AVIF";
    }
    return "HEIF";
  }
  const auto head = std::string_view(reinterpret_cast<const char*>(bytes.data()), std::min<std::size_t>(bytes.size(), 4096));
  if (head.find("<svg") != std::string_view::npos) {
    return "SVG";
  }
  return {};
}

DocumentFileInfo extract_file_info(std::span<const std::uint8_t> bytes) {
  Builder builder;
  const auto format = sniff_metadata_format(bytes);
  builder.info.source_format = format;
  builder.info.source_file_size = bytes.size();
  try {
    if (format == "JPEG") {
      builder.segment("SOI", 0, 2);
      extract_jpeg(bytes, builder);
    } else if (format == "PNG") {
      builder.segment("Signature", 0, 8);
      extract_png(bytes, builder);
    } else if (format == "TIFF") {
      extract_tiff(bytes, builder);
    } else if (format == "WebP") {
      extract_webp(bytes, builder);
    } else if (format == "GIF") {
      extract_gif(bytes, builder);
    } else if (format == "JXL") {
      extract_jxl(bytes, builder);
    } else if (format == "HEIF" || format == "AVIF" || format == "CR3") {
      extract_bmff(bytes, builder);
    } else if (format == "RAF") {
      extract_raf(bytes, builder);
    } else if (format == "BMP") {
      extract_bmp(bytes, builder);
    } else if (format == "PDF") {
      extract_pdf(bytes, builder);
    } else if (format == "SVG") {
      extract_svg(bytes, builder);
    }
  } catch (const std::exception&) {
    // Damaged input: keep whatever was read before the failure.
  }
  return std::move(builder.info);
}

DocumentFileInfo extract_file_info_from_path(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return {};
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return {};
  }
  const auto to_read = size <= kFullReadCap ? static_cast<std::size_t>(size) : kPartialReadBytes;
  std::vector<std::uint8_t> bytes(to_read);
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(to_read));
  bytes.resize(static_cast<std::size_t>(stream.gcount()));
  auto info = extract_file_info(bytes);
  info.source_file_size = size;
  return info;
}

}  // namespace patchy::metadata
