#include "formats/webp_symbols.hpp"
#include "formats/metadata/metadata_inject.hpp"

#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/metadata/xmp.hpp"
#include "formats/miniz/miniz.h"
#include "formats/pdf_crypt.hpp"

#include "webp/mux.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace patchy::metadata {
namespace {

using Bytes = std::vector<std::uint8_t>;
using Span = std::span<const std::uint8_t>;

constexpr std::size_t kMaxJpegPayload = 65533;  // segment length field covers itself
constexpr std::string_view kXmpNamespaceHeader("http://ns.adobe.com/xap/1.0/\0", 29);
constexpr std::string_view kExtendedXmpHeader("http://ns.adobe.com/xmp/extension/\0", 35);
constexpr std::size_t kExtendedXmpChunk = 65000;

void append(Bytes& out, std::string_view text) {
  out.insert(out.end(), text.begin(), text.end());
}
void append(Bytes& out, Span data) {
  out.insert(out.end(), data.begin(), data.end());
}
void put_be16(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}
void put_be32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}
std::uint16_t be16(Span data, std::size_t at) {
  return static_cast<std::uint16_t>((data[at] << 8) | data[at + 1]);
}
std::uint32_t be32(Span data, std::size_t at) {
  return (static_cast<std::uint32_t>(data[at]) << 24) | (static_cast<std::uint32_t>(data[at + 1]) << 16) |
         (static_cast<std::uint32_t>(data[at + 2]) << 8) | data[at + 3];
}
bool starts_with(Span data, std::string_view prefix) {
  return data.size() >= prefix.size() && std::memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}

Bytes zlib(Span data) {
  mz_ulong size = mz_compressBound(static_cast<mz_ulong>(data.size()));
  Bytes out(size);
  if (mz_compress2(out.data(), &size, data.data(), static_cast<mz_ulong>(data.size()), MZ_BEST_COMPRESSION) != MZ_OK) {
    return {};
  }
  out.resize(size);
  return out;
}

std::string md5_hex(Span data) {
  std::string hex;
  for (const auto byte : pdf::md5(data)) {
    char buffer[3];
    std::snprintf(buffer, sizeof(buffer), "%02X", static_cast<unsigned>(byte));
    hex += buffer;
  }
  return hex;
}

// The IRB that carries IPTC (JPEG APP13, TIFF 34377).
Bytes iptc_irb(const SaveMetadata& metadata) {
  std::vector<IrbResource> resources;
  resources.push_back(IrbResource{0x3842494DU, psd_resource::kIptc, {}, metadata.iptc});
  const auto digest = metadata.iptc_digest ? *metadata.iptc_digest : iim_digest(metadata.iptc);
  resources.push_back(IrbResource{0x3842494DU, psd_resource::kCaptionDigest, {}, Bytes(digest.begin(), digest.end())});
  return write_irb(resources);
}

// EXIF that fits one JPEG APP1 segment; a MakerNote is the first thing to go when it
// does not (the camera's private blob, often most of the size).
Bytes fitting_exif(const Bytes& exif, std::size_t limit) {
  if (exif.size() <= limit) {
    return exif;
  }
  if (auto stream = parse_tiff(exif)) {
    if (auto* ifd = stream->find(IfdKind::Exif)) {
      ifd->erase(37500);
    }
    auto smaller = write_tiff(*stream);
    if (smaller.size() <= limit) {
      return smaller;
    }
  }
  return {};
}

std::string strip_xpacket_wrapper(std::string text) {
  if (text.starts_with("<?xpacket")) {
    const auto end = text.find("?>");
    text.erase(0, end == std::string::npos ? 0 : end + 2);
  }
  const auto trailer = text.rfind("<?xpacket end=");
  if (trailer != std::string::npos) {
    text.erase(trailer);
  }
  while (!text.empty() && (text.front() == '\n' || text.front() == ' ')) {
    text.erase(0, 1);
  }
  while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
    text.pop_back();
  }
  return text;
}

// ---------------------------------------------------------------------- JPEG

Bytes jpeg_segment(std::uint8_t marker, Span payload) {
  Bytes out{0xFF, marker};
  put_be16(out, static_cast<std::uint32_t>(payload.size() + 2));
  append(out, payload);
  return out;
}

void append_xmp_segments(Bytes& out, const std::string& xmp) {
  if (kXmpNamespaceHeader.size() + xmp.size() <= kMaxJpegPayload) {
    Bytes payload;
    append(payload, kXmpNamespaceHeader);
    append(payload, xmp);
    append(out, jpeg_segment(0xE1, payload));
    return;
  }
  // Extended XMP (XMP Part 3): the standard packet keeps what fits and names the
  // extension by the MD5 of its serialization; the rest travels in 65000-byte chunks.
  auto packet = parse_xmp(Span(reinterpret_cast<const std::uint8_t*>(xmp.data()), xmp.size()));
  if (!packet) {
    return;
  }
  XmpPacket standard;
  XmpPacket extended;
  standard.prefixes = packet->prefixes;
  extended.prefixes = packet->prefixes;
  constexpr std::size_t kStandardBudget = 60000;
  for (auto& property : packet->properties) {
    standard.properties.push_back(property);
    if (serialize_xmp(standard, 0).size() > kStandardBudget) {
      standard.properties.pop_back();
      extended.properties.push_back(std::move(property));
    }
  }
  const auto extended_text = strip_xpacket_wrapper(serialize_xmp(extended, 0));
  const Span extended_bytes(reinterpret_cast<const std::uint8_t*>(extended_text.data()), extended_text.size());
  const auto guid = md5_hex(extended_bytes);
  standard.set_simple(xmp_ns::kXmpNote, "HasExtendedXMP", guid);
  Bytes payload;
  append(payload, kXmpNamespaceHeader);
  append(payload, serialize_xmp(standard, 0));
  append(out, jpeg_segment(0xE1, payload));
  for (std::size_t offset = 0; offset < extended_bytes.size(); offset += kExtendedXmpChunk) {
    Bytes chunk;
    append(chunk, kExtendedXmpHeader);
    append(chunk, guid);
    put_be32(chunk, static_cast<std::uint32_t>(extended_bytes.size()));
    put_be32(chunk, static_cast<std::uint32_t>(offset));
    append(chunk, extended_bytes.subspan(offset, std::min(kExtendedXmpChunk, extended_bytes.size() - offset)));
    append(out, jpeg_segment(0xE1, chunk));
  }
}

Bytes jpeg_metadata_segments(const SaveMetadata& metadata) {
  Bytes out;
  if (!metadata.exif.empty()) {
    const auto exif = fitting_exif(metadata.exif, kMaxJpegPayload - 6);
    if (!exif.empty()) {
      Bytes payload;
      append(payload, std::string_view("Exif\0\0", 6));
      append(payload, exif);
      append(out, jpeg_segment(0xE1, payload));
    }
  }
  if (!metadata.xmp.empty()) {
    append_xmp_segments(out, metadata.xmp);
  }
  if (!metadata.icc.empty()) {
    constexpr std::size_t kChunk = kMaxJpegPayload - 14;
    const auto count = (metadata.icc.size() + kChunk - 1) / kChunk;
    if (count <= 255) {
      for (std::size_t index = 0; index < count; ++index) {
        Bytes payload;
        append(payload, std::string_view("ICC_PROFILE\0", 12));
        payload.push_back(static_cast<std::uint8_t>(index + 1));
        payload.push_back(static_cast<std::uint8_t>(count));
        const Span icc = metadata.icc;
        append(payload, icc.subspan(index * kChunk, std::min(kChunk, icc.size() - index * kChunk)));
        append(out, jpeg_segment(0xE2, payload));
      }
    }
  }
  if (!metadata.iptc.empty()) {
    Bytes payload;
    append(payload, std::string_view("Photoshop 3.0\0", 14));
    append(payload, iptc_irb(metadata));
    if (payload.size() <= kMaxJpegPayload) {
      append(out, jpeg_segment(0xED, payload));
    }
  }
  for (const auto& text : metadata.text) {
    if (text.kind != FileInfoTextKind::Comment || text.value.empty()) {
      continue;
    }
    const Span value(reinterpret_cast<const std::uint8_t*>(text.value.data()),
                     std::min(text.value.size(), kMaxJpegPayload));
    append(out, jpeg_segment(0xFE, value));
  }
  return out;
}

bool is_replaced_jpeg_segment(std::uint8_t marker, Span payload) {
  return (marker == 0xE1 && (starts_with(payload, std::string_view("Exif\0", 5)) ||
                             starts_with(payload, kXmpNamespaceHeader) || starts_with(payload, kExtendedXmpHeader))) ||
         (marker == 0xE2 && starts_with(payload, std::string_view("ICC_PROFILE\0", 12))) ||
         (marker == 0xED && starts_with(payload, std::string_view("Photoshop 3.0\0", 14))) || marker == 0xFE;
}

Bytes inject_jpeg(const Bytes& encoded, const SaveMetadata& metadata) {
  if (encoded.size() < 4 || encoded[0] != 0xFF || encoded[1] != 0xD8) {
    return encoded;
  }
  Bytes head;   // leading APP0 (JFIF/JFXX) segments stay first
  Bytes rest;   // remaining header segments
  std::size_t pos = 2;
  bool leading = true;
  while (pos + 4 <= encoded.size() && encoded[pos] == 0xFF) {
    const auto marker = encoded[pos + 1];
    if (marker == 0xDA || marker == 0xD9) {
      break;
    }
    const std::size_t length = be16(encoded, pos + 2);
    if (length < 2 || pos + 2 + length > encoded.size()) {
      return encoded;
    }
    const Span segment(encoded.data() + pos, length + 2);
    const auto payload = segment.subspan(4);
    if (marker != 0xE0) {
      leading = false;
    }
    if (!is_replaced_jpeg_segment(marker, payload)) {
      append(leading ? head : rest, segment);
    }
    pos += 2 + length;
  }
  Bytes out{0xFF, 0xD8};
  append(out, head);
  append(out, jpeg_metadata_segments(metadata));
  append(out, rest);
  out.insert(out.end(), encoded.begin() + static_cast<std::ptrdiff_t>(pos), encoded.end());
  return out;
}

// ----------------------------------------------------------------------- PNG

Bytes png_chunk(std::string_view type, Span payload) {
  Bytes out;
  put_be32(out, static_cast<std::uint32_t>(payload.size()));
  Bytes typed;
  append(typed, type);
  append(typed, payload);
  append(out, typed);
  put_be32(out, static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, typed.data(), typed.size())));
  return out;
}

bool latin1_encodable(const std::string& utf8, std::string& latin1) {
  latin1.clear();
  for (std::size_t i = 0; i < utf8.size(); ++i) {
    const auto c = static_cast<unsigned char>(utf8[i]);
    if (c < 0x80) {
      latin1.push_back(static_cast<char>(c));
    } else if ((c == 0xC2 || c == 0xC3) && i + 1 < utf8.size()) {
      const auto next = static_cast<unsigned char>(utf8[++i]);
      latin1.push_back(static_cast<char>(((c & 0x03) << 6) | (next & 0x3F)));
    } else {
      return false;
    }
  }
  return true;
}

Bytes png_text_chunk(const FileInfoText& text) {
  // PNG keywords are 1-79 Latin-1 characters.
  std::string key = text.kind == FileInfoTextKind::Comment && text.key.empty() ? std::string("Comment") : text.key;
  std::string latin1_key;
  if (key.empty() || !latin1_encodable(key, latin1_key)) {
    return {};
  }
  latin1_key.resize(std::min<std::size_t>(latin1_key.size(), 79));
  std::string latin1_value;
  const bool latin1 = text.language.empty() && latin1_encodable(text.value, latin1_value);
  Bytes payload;
  append(payload, latin1_key);
  payload.push_back(0);
  const Span value(reinterpret_cast<const std::uint8_t*>(text.value.data()), text.value.size());
  if (latin1 && text.kind != FileInfoTextKind::PngUnicodeText) {
    const Span latin1_bytes(reinterpret_cast<const std::uint8_t*>(latin1_value.data()), latin1_value.size());
    if (text.kind == FileInfoTextKind::PngCompressedText) {
      payload.push_back(0);
      append(payload, zlib(latin1_bytes));
      return png_chunk("zTXt", payload);
    }
    append(payload, latin1_bytes);
    return png_chunk("tEXt", payload);
  }
  const bool compress = text.kind == FileInfoTextKind::PngCompressedText || value.size() > 1024;
  payload.push_back(compress ? 1 : 0);
  payload.push_back(0);
  append(payload, text.language);
  payload.push_back(0);
  payload.push_back(0);  // translated keyword
  if (compress) {
    append(payload, zlib(value));
  } else {
    append(payload, value);
  }
  return png_chunk("iTXt", payload);
}

std::string png_text_key(Span payload) {
  const auto nul = std::find(payload.begin(), payload.end(), std::uint8_t{0});
  return std::string(payload.begin(), nul);
}

Bytes inject_png(const Bytes& encoded, const SaveMetadata& metadata) {
  static constexpr std::string_view kSignature("\x89PNG\r\n\x1A\n", 8);
  if (!starts_with(encoded, kSignature) || encoded.size() < 8 + 25) {
    return encoded;
  }
  const bool writes_color = !metadata.icc.empty() || !metadata.png_color_chunks.empty();
  std::set<std::string> written_keys;
  for (const auto& text : metadata.text) {
    written_keys.insert(text.kind == FileInfoTextKind::Comment && text.key.empty() ? std::string("Comment") : text.key);
  }
  Bytes inserted;
  if (!metadata.icc.empty()) {
    Bytes payload;
    append(payload, std::string_view("ICC Profile\0", 12));
    payload.push_back(0);
    append(payload, zlib(metadata.icc));
    append(inserted, png_chunk("iCCP", payload));
  }
  for (const auto& chunk : metadata.png_color_chunks) {
    const std::string type(chunk.type.begin(), chunk.type.end());
    // sBIT describes the source's sample depth, not this encoding; sRGB, gAMA and cHRM
    // give way to an embedded profile (PNG 3rd edition precedence).
    if (type == "sBIT" || (!metadata.icc.empty() && (type == "sRGB" || type == "gAMA" || type == "cHRM"))) {
      continue;
    }
    append(inserted, png_chunk(type, chunk.data));
  }
  if (!metadata.exif.empty()) {
    append(inserted, png_chunk("eXIf", metadata.exif));
  }
  if (!metadata.xmp.empty()) {
    Bytes payload;
    append(payload, std::string_view("XML:com.adobe.xmp\0\0\0\0\0", 22));
    append(payload, metadata.xmp);
    append(inserted, png_chunk("iTXt", payload));
  }
  for (const auto& text : metadata.text) {
    append(inserted, png_text_chunk(text));
  }

  Bytes out(encoded.begin(), encoded.begin() + 8);
  std::size_t pos = 8;
  bool inserted_done = false;
  while (pos + 12 <= encoded.size()) {
    const auto length = be32(encoded, pos);
    if (length > encoded.size() - pos - 12) {
      return encoded;
    }
    const std::string type(reinterpret_cast<const char*>(encoded.data() + pos + 4), 4);
    const Span chunk(encoded.data() + pos, static_cast<std::size_t>(length) + 12);
    const auto payload = chunk.subspan(8, length);
    bool drop = false;
    if (writes_color && (type == "iCCP" || type == "sRGB" || type == "gAMA" || type == "cHRM" || type == "cICP" ||
                         type == "mDCV" || type == "cLLI")) {
      drop = true;
    } else if (!metadata.exif.empty() && type == "eXIf") {
      drop = true;
    } else if (type == "tEXt" || type == "zTXt" || type == "iTXt") {
      const auto key = png_text_key(payload);
      drop = (key == "XML:com.adobe.xmp" && !metadata.xmp.empty()) || written_keys.contains(key);
    }
    if (!drop) {
      append(out, chunk);
    }
    if (type == "IHDR" && !inserted_done) {
      append(out, inserted);
      inserted_done = true;
    }
    pos += static_cast<std::size_t>(length) + 12;
  }
  return inserted_done ? out : encoded;
}

// ---------------------------------------------------------------------- TIFF

// Re-encodes multi-byte values when the EXIF stream and the file disagree on byte order.
void convert_byte_order(TiffEntry& entry, bool from_big, bool to_big) {
  if (from_big == to_big) {
    return;
  }
  const auto type = static_cast<TiffType>(entry.type);
  std::size_t unit = tiff_type_size(entry.type);
  if (type == TiffType::Rational || type == TiffType::SRational) {
    unit = 4;
  }
  if (unit <= 1) {
    return;
  }
  for (std::size_t i = 0; i + unit <= entry.data.size(); i += unit) {
    std::reverse(entry.data.begin() + static_cast<std::ptrdiff_t>(i),
                 entry.data.begin() + static_cast<std::ptrdiff_t>(i + unit));
  }
}

struct TiffFileWriter {
  Bytes& out;
  bool big;

  void put(std::size_t at, std::uint64_t value, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
      const auto shift = 8 * (big ? size - 1 - i : i);
      out[at + i] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
    }
  }
  void append_value(std::uint64_t value, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
      const auto shift = 8 * (big ? size - 1 - i : i);
      out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
  }
  [[nodiscard]] std::uint64_t read(std::size_t at, std::size_t size) const {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
      value = (value << 8) | out[at + (big ? i : size - 1 - i)];
    }
    return value;
  }

  // A directory record: either raw bytes copied from the existing IFD (its value
  // offsets stay valid because the file only grows) or a new entry whose value is
  // appended before the directory.
  struct Record {
    std::uint16_t tag{0};
    std::array<std::uint8_t, 12> raw{};
  };

  void encode(std::array<std::uint8_t, 12>& raw, std::size_t at, std::uint64_t value, std::size_t size) const {
    for (std::size_t i = 0; i < size; ++i) {
      const auto shift = 8 * (big ? size - 1 - i : i);
      raw[at + i] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
    }
  }

  Record place(const TiffEntry& entry) {
    Record record;
    record.tag = entry.tag;
    encode(record.raw, 0, entry.tag, 2);
    encode(record.raw, 2, entry.type, 2);
    encode(record.raw, 4, entry.count, 4);
    if (entry.data.size() <= 4) {
      std::copy(entry.data.begin(), entry.data.end(), record.raw.begin() + 8);
    } else {
      if (out.size() % 2 != 0) {
        out.push_back(0);
      }
      const auto offset = out.size();
      append(out, entry.data);
      encode(record.raw, 8, offset, 4);
    }
    return record;
  }

  std::size_t write_directory(std::vector<Record> records, std::uint32_t next) {
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.tag < b.tag; });
    if (out.size() % 2 != 0) {
      out.push_back(0);
    }
    const auto offset = out.size();
    append_value(records.size(), 2);
    for (const auto& record : records) {
      out.insert(out.end(), record.raw.begin(), record.raw.end());
    }
    append_value(next, 4);
    return offset;
  }

  std::size_t write_entries(const std::vector<TiffEntry>& entries) {
    std::vector<Record> records;
    for (const auto& entry : entries) {
      records.push_back(place(entry));
    }
    return write_directory(std::move(records), 0);
  }
};

bool is_tiff_descriptive_tag(std::uint16_t tag) {
  switch (tag) {
    case 269:  // DocumentName
    case 270:  // ImageDescription
    case 271:  // Make
    case 272:  // Model
    case 285:  // PageName
    case 305:  // Software
    case 306:  // DateTime
    case 315:  // Artist
    case 316:  // HostComputer
    case 18246:  // Rating
    case 33432:  // Copyright
    case 40091:
    case 40092:
    case 40093:
    case 40094:
    case 40095:
      return true;
    default:
      return false;
  }
}

Bytes inject_tiff(Bytes encoded, const SaveMetadata& metadata) {
  if (encoded.size() < 8 || !((encoded[0] == 'I' && encoded[1] == 'I') || (encoded[0] == 'M' && encoded[1] == 'M'))) {
    return encoded;
  }
  const Bytes original = encoded;
  TiffFileWriter writer{encoded, encoded[0] == 'M'};
  if (writer.read(2, 2) != 42) {
    return original;
  }
  const auto ifd0 = static_cast<std::size_t>(writer.read(4, 4));
  if (ifd0 + 2 > encoded.size()) {
    return original;
  }
  const auto count = static_cast<std::size_t>(writer.read(ifd0, 2));
  if (ifd0 + 2 + count * 12 + 4 > encoded.size()) {
    return original;
  }
  const auto next = static_cast<std::uint32_t>(writer.read(ifd0 + 2 + count * 12, 4));

  std::optional<TiffStream> exif;
  if (!metadata.exif.empty()) {
    exif = parse_tiff(metadata.exif);
  }
  // 34377 is replaced only when IPTC is written (below): a layered TIFF's Photoshop image
  // resources live there too (docs/tiff.md).
  std::set<std::uint16_t> replaced{700, 33723, 34675, kTagExifIfd, kTagGpsIfd};
  std::vector<TiffEntry> additions;
  const auto add = [&](TiffEntry entry, bool from_big) {
    convert_byte_order(entry, from_big, writer.big);
    replaced.insert(entry.tag);
    additions.push_back(std::move(entry));
  };
  if (exif) {
    if (const auto* exif_ifd0 = exif->find(IfdKind::Ifd0)) {
      for (const auto& entry : exif_ifd0->entries) {
        if (is_tiff_descriptive_tag(entry.tag)) {
          add(entry, exif->big_endian);
        }
      }
    }
  }
  if (!metadata.xmp.empty()) {
    add(TiffEntry{700, static_cast<std::uint16_t>(TiffType::Byte), static_cast<std::uint32_t>(metadata.xmp.size()),
                  Bytes(metadata.xmp.begin(), metadata.xmp.end())},
        writer.big);
  }
  if (!metadata.iptc.empty()) {
    add(TiffEntry{33723, static_cast<std::uint16_t>(TiffType::Undefined),
                  static_cast<std::uint32_t>(metadata.iptc.size()), metadata.iptc},
        writer.big);
    auto irb = iptc_irb(metadata);
    // Resources already in the file (a layered TIFF's) stay; only the IPTC record and its
    // digest are replaced.
    for (std::size_t i = 0; i < count; ++i) {
      const auto at = ifd0 + 2 + i * 12;
      if (writer.read(at, 2) != 34377) {
        continue;
      }
      const auto length = static_cast<std::size_t>(writer.read(at + 4, 4));
      const auto offset = length <= 4 ? at + 8 : static_cast<std::size_t>(writer.read(at + 8, 4));
      if (offset > encoded.size() || length > encoded.size() - offset) {
        break;
      }
      auto merged = parse_irb(Span(encoded.data() + offset, length));
      std::erase_if(merged, [](const IrbResource& resource) {
        return resource.id == psd_resource::kIptc || resource.id == psd_resource::kCaptionDigest;
      });
      for (auto& resource : parse_irb(irb)) {
        merged.push_back(std::move(resource));
      }
      irb = write_irb(merged);
      break;
    }
    add(TiffEntry{34377, static_cast<std::uint16_t>(TiffType::Byte), static_cast<std::uint32_t>(irb.size()), irb},
        writer.big);
  }
  if (!metadata.icc.empty()) {
    add(TiffEntry{34675, static_cast<std::uint16_t>(TiffType::Undefined),
                  static_cast<std::uint32_t>(metadata.icc.size()), metadata.icc},
        writer.big);
  }

  std::vector<TiffFileWriter::Record> records;
  for (std::size_t i = 0; i < count; ++i) {
    TiffFileWriter::Record record;
    const auto at = ifd0 + 2 + i * 12;
    std::copy(encoded.begin() + static_cast<std::ptrdiff_t>(at), encoded.begin() + static_cast<std::ptrdiff_t>(at + 12),
              record.raw.begin());
    record.tag = static_cast<std::uint16_t>(writer.read(at, 2));
    if (!replaced.contains(record.tag)) {
      records.push_back(record);
    }
  }
  for (const auto& entry : additions) {
    records.push_back(writer.place(entry));
  }
  // Exif and GPS sub-directories.
  const auto sub_directory = [&](IfdKind kind, std::uint16_t pointer_tag) {
    if (!exif) {
      return;
    }
    const auto* ifd = exif->find(kind);
    if (ifd == nullptr) {
      return;
    }
    std::vector<TiffEntry> entries;
    for (auto entry : ifd->entries) {
      if (entry.tag == kTagInteropIfd || tiff_type_size(entry.type) == 0) {
        continue;
      }
      convert_byte_order(entry, exif->big_endian, writer.big);
      entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
      return;
    }
    const auto offset = writer.write_entries(entries);
    TiffEntry pointer{pointer_tag, static_cast<std::uint16_t>(TiffType::Long), 1, {}};
    for (std::size_t i = 0; i < 4; ++i) {
      const auto shift = 8 * (writer.big ? 3 - i : i);
      pointer.data.push_back(static_cast<std::uint8_t>((offset >> shift) & 0xFF));
    }
    records.push_back(writer.place(pointer));
  };
  sub_directory(IfdKind::Exif, kTagExifIfd);
  sub_directory(IfdKind::Gps, kTagGpsIfd);
  if (encoded.size() > 0xFFFFFFF0ULL) {
    return original;  // classic TIFF offsets are 32-bit
  }
  const auto new_ifd0 = writer.write_directory(std::move(records), next);
  writer.put(4, new_ifd0, 4);
  return encoded;
}

// ---------------------------------------------------------------------- WebP

Bytes inject_webp(const Bytes& encoded, const SaveMetadata& metadata) {
  WebPData input{encoded.data(), encoded.size()};
  WebPMux* mux = WebPMuxCreate(&input, 1);
  if (mux == nullptr) {
    return encoded;
  }
  const auto set_chunk = [&](const char* fourcc, Span data) {
    WebPMuxDeleteChunk(mux, fourcc);
    if (data.empty()) {
      return true;
    }
    WebPData chunk{data.data(), data.size()};
    return WebPMuxSetChunk(mux, fourcc, &chunk, 1) == WEBP_MUX_OK;
  };
  bool ok = true;
  if (!metadata.icc.empty()) {
    ok = set_chunk("ICCP", metadata.icc) && ok;
  }
  if (!metadata.exif.empty()) {
    ok = set_chunk("EXIF", metadata.exif) && ok;
  }
  if (!metadata.xmp.empty()) {
    ok = set_chunk("XMP ", Span(reinterpret_cast<const std::uint8_t*>(metadata.xmp.data()), metadata.xmp.size())) &&
         ok;
  }
  WebPData assembled{};
  Bytes out = encoded;
  if (ok && WebPMuxAssemble(mux, &assembled) == WEBP_MUX_OK) {
    out.assign(assembled.bytes, assembled.bytes + assembled.size);
  }
  WebPDataClear(&assembled);
  WebPMuxDelete(mux);
  return out;
}

// ----------------------------------------------------------------------- GIF

Bytes inject_gif(const Bytes& encoded, const SaveMetadata& metadata) {
  if (encoded.size() < 13 || !starts_with(encoded, "GIF8")) {
    return encoded;
  }
  std::size_t header = 13;
  if ((encoded[10] & 0x80) != 0) {
    header += 3U * (1U << ((encoded[10] & 0x07) + 1));
  }
  if (header > encoded.size()) {
    return encoded;
  }
  Bytes extensions;
  for (const auto& text : metadata.text) {
    if (text.kind != FileInfoTextKind::Comment || text.value.empty()) {
      continue;
    }
    extensions.insert(extensions.end(), {0x21, 0xFE});
    for (std::size_t offset = 0; offset < text.value.size(); offset += 255) {
      const auto size = std::min<std::size_t>(255, text.value.size() - offset);
      extensions.push_back(static_cast<std::uint8_t>(size));
      append(extensions, std::string_view(text.value).substr(offset, size));
    }
    extensions.push_back(0);
  }
  if (!metadata.xmp.empty()) {
    extensions.insert(extensions.end(), {0x21, 0xFF, 11});
    append(extensions, "XMP DataXMP");
    append(extensions, metadata.xmp);
    // The "magic trailer": readers that walk sub-blocks land on the terminator.
    extensions.push_back(1);
    for (int value = 255; value >= 0; --value) {
      extensions.push_back(static_cast<std::uint8_t>(value));
    }
    extensions.push_back(0);
  }
  if (extensions.empty()) {
    return encoded;
  }
  Bytes out(encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(header));
  out[4] = '9';  // extensions need GIF89a
  out[5] = 'a';
  append(out, extensions);
  out.insert(out.end(), encoded.begin() + static_cast<std::ptrdiff_t>(header), encoded.end());
  return out;
}

}  // namespace

std::vector<std::uint8_t> inject_metadata(std::vector<std::uint8_t> encoded, std::string_view format,
                                          const SaveMetadata& metadata) {
  if (metadata.empty()) {
    return encoded;
  }
  if (format == "jpg" || format == "jpeg" || format == "jpe" || format == "jfif") {
    return inject_jpeg(encoded, metadata);
  }
  if (format == "png") {
    return inject_png(encoded, metadata);
  }
  if (format == "tif" || format == "tiff") {
    return inject_tiff(std::move(encoded), metadata);
  }
  if (format == "webp") {
    return inject_webp(encoded, metadata);
  }
  if (format == "gif") {
    return inject_gif(encoded, metadata);
  }
  return encoded;
}

}  // namespace patchy::metadata
