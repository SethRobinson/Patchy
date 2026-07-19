#include "formats/af_document_io.hpp"

#include "formats/binary_le.hpp"
#include "formats/format_file_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/zstd/zstd.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

// Container knowledge: verified against documents authored with Affinity 3.2.3
// (container versions 11/12, document tree versions 20-32) plus the MIT-licensed
// afread project's record of the 2020-era layout. All integers little-endian.
//
//   u32 magic 0x414BFF00, u16 container version, u16 flags, u32 class tag
//   ("Prsn" for documents), "#Inf" block (stream-table offset, thumbnail offset,
//   sizes, unix timestamp, counters), "Prot" + u32 protocol revision (version>7).
//   The stream table ("#FAT"/"#FT2"/"#FT3"/"#FT4" chain) names streams (doc.dat,
//   d/<hex> raster tiles, edc/<n> embedded documents); each stream sits at its
//   own offset behind a "#Fil" tag, compressed with zlib or zstd plus optional
//   byte/u16 delta predictors, with a CRC32 of the decoded bytes. At the
//   thumbnail offset: u32 0xFFFFFFFF + "Thmb", then a PNG (8-bit RGBA, never
//   interlaced, at most ~512 px) of the flattened document.

namespace patchy::af {

namespace {

constexpr std::uint32_t kMagic = 0x414BFF00U;
constexpr std::uint32_t kTagInf = 0x666E4923U;   // "#Inf"
constexpr std::uint32_t kTagProt = 0x746F7250U;  // "Prot"
constexpr std::uint32_t kTagFil = 0x6C694623U;   // "#Fil"
constexpr std::uint32_t kTagThmb = 0x626D6854U;  // "Thmb"
constexpr std::uint32_t kTagDoc = 0x534BFF00U;   // doc.dat stream header
constexpr std::array<std::uint32_t, 4> kFatTags = {
    0x54414623U,  // "#FAT"
    0x32544623U,  // "#FT2"
    0x33544623U,  // "#FT3"
    0x34544623U,  // "#FT4"
};

// The newest container version this reader has been verified against. Newer
// files still get a best-effort preview (the walk is version-stable so far);
// a notice records the mismatch.
constexpr std::uint16_t kNewestVerifiedContainerVersion = 12;

// Hostile-input ceilings. Real documents: one or two table links, tens to a few
// thousand streams (one per 64 KiB raster tile), stream names under 32 bytes.
constexpr std::size_t kMaxFatChain = 64;
constexpr std::uint32_t kMaxStreamsPerFat = 1U << 20U;
constexpr std::uint16_t kMaxNameLength = 4096;
constexpr std::uint64_t kMaxStreamBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::int32_t kMaxThumbnailSide = 8192;

[[nodiscard]] LittleEndianReader af_reader(std::span<const std::uint8_t> bytes) {
  return LittleEndianReader(bytes, "Affinity document is truncated");
}

struct StreamRecord {
  std::uint64_t data_offset{0};
  std::uint64_t size{0};
  std::uint64_t compressed_size{0};
  std::uint32_t crc32{0};
  std::uint8_t compression{0};
};

struct Container {
  std::uint16_t version{0};
  std::uint16_t flags{0};
  std::uint32_t class_tag{0};
  std::uint64_t thumbnail_offset{0};
  std::uint32_t protocol{0};
  // Head revision per stream name (the newest table entry wins, as in the app).
  std::unordered_map<std::string, StreamRecord> streams;
};

[[nodiscard]] Container parse_container(std::span<const std::uint8_t> bytes) {
  Container container;
  auto reader = af_reader(bytes);
  if (reader.read_u32() != kMagic) {
    throw std::runtime_error("Not an Affinity document");
  }
  container.version = reader.read_u16();
  container.flags = reader.read_u16();
  container.class_tag = reader.read_u32();
  if (reader.read_u32() != kTagInf) {
    throw std::runtime_error("Affinity document info block is missing");
  }
  const std::uint64_t fat_offset = reader.read_u64();
  container.thumbnail_offset = reader.read_u64();
  reader.skip(8 + 8 + 8);  // stored length, reserved, creation date
  reader.skip(4 + 4);      // revision counters
  if (container.version > 7) {
    if (reader.read_u32() != kTagProt) {
      throw std::runtime_error("Affinity document protocol block is missing");
    }
    container.protocol = reader.read_u32();
  }

  // Stream-table chain. Entries repeat across links (save revisions); walking
  // oldest-to-newest with later links overwriting matches head-revision picks.
  std::unordered_map<std::uint32_t, std::string> names_by_id;
  std::uint64_t next_offset = fat_offset;
  std::size_t chain_length = 0;
  while (next_offset != 0) {
    if (++chain_length > kMaxFatChain) {
      throw std::runtime_error("Affinity document stream table recurses");
    }
    if (next_offset > bytes.size()) {
      throw std::runtime_error("Affinity document stream table is out of range");
    }
    reader.seek(static_cast<std::size_t>(next_offset));
    const std::uint32_t fat_tag = reader.read_u32();
    if (std::find(kFatTags.begin(), kFatTags.end(), fat_tag) == kFatTags.end()) {
      throw std::runtime_error("Affinity document stream table is corrupt");
    }
    const bool oldest_layout = fat_tag == kFatTags[0];
    const bool ft4_layout = fat_tag == kFatTags[3];
    next_offset = reader.read_u64();
    reader.skip(8 + 8 + 8 + 8);  // creation date, offsets, lengths
    const std::uint32_t files_count = reader.read_u32();
    reader.skip(4 + 4);  // reserved counters
    const std::uint16_t dirs_count = reader.read_u16();
    reader.skip(1);
    if (files_count > kMaxStreamsPerFat) {
      throw std::runtime_error("Affinity document stream table is implausible");
    }
    for (std::uint32_t i = 0; i < files_count; ++i) {
      const std::uint32_t id = reader.read_u32();
      const std::uint8_t flag = reader.read_u8();
      if (flag > 2) {
        throw std::runtime_error("Affinity document stream entry is corrupt");
      }
      StreamRecord record;
      if (flag == 0 || flag == 1) {
        record.data_offset = reader.read_u64();
        record.size = reader.read_u64();
        record.compressed_size = reader.read_u64();
        record.crc32 = reader.read_u32();
        record.compression = reader.read_u8();
        if (!oldest_layout) {
          reader.skip(4);
        }
        if (ft4_layout) {
          reader.skip(4);
        }
        if (oldest_layout || fat_tag == kFatTags[1]) {
          // The two oldest layouts store an indirect compression code.
          switch (record.compression) {
            case 1: record.compression = 0x01; break;
            case 2: record.compression = 0x41; break;
            case 3: record.compression = 0x81; break;
            case 4: record.compression = 0xC1; break;
            default: record.compression = 0; break;
          }
        }
      }
      if (flag == 0) {
        const std::uint16_t name_length = reader.read_u16();
        if (name_length > kMaxNameLength) {
          throw std::runtime_error("Affinity document stream name is implausible");
        }
        std::string name(name_length, '\0');
        for (std::uint16_t c = 0; c < name_length; ++c) {
          name[c] = static_cast<char>(reader.read_u8());
        }
        names_by_id[id] = name;
      }
      if (flag == 0 || flag == 1) {
        auto found = names_by_id.find(id);
        if (found != names_by_id.end()) {
          container.streams[found->second] = record;
        }
      }
    }
    for (std::uint16_t d = 0; d < dirs_count; ++d) {
      const std::uint16_t name_length = reader.read_u16();
      reader.skip(2);  // secondary length, zero in every observed file
      reader.skip(8);  // member count
      if (name_length > kMaxNameLength) {
        throw std::runtime_error("Affinity document directory name is implausible");
      }
      reader.skip(name_length);
    }
  }
  return container;
}

// Cumulative byte delta (predictor over 8-bit stream bytes).
void undo_byte_delta(std::vector<std::uint8_t>& bytes) {
  std::uint32_t accumulator = 0;
  for (auto& value : bytes) {
    accumulator = (accumulator + value) & 0xFFU;
    value = static_cast<std::uint8_t>(accumulator);
  }
}

// Cumulative little-endian u16 delta.
void undo_u16_delta(std::vector<std::uint8_t>& bytes) {
  std::uint32_t accumulator = 0;
  for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
    const std::uint32_t value =
        static_cast<std::uint32_t>(bytes[i]) | (static_cast<std::uint32_t>(bytes[i + 1]) << 8U);
    accumulator = (accumulator + value) & 0xFFFFU;
    bytes[i] = static_cast<std::uint8_t>(accumulator & 0xFFU);
    bytes[i + 1] = static_cast<std::uint8_t>(accumulator >> 8U);
  }
}

// De-interleave a 64 KiB tile stored as two half-planes of byte pairs (16-bit
// tile layout; applies only to exactly 0x10000-byte streams).
void undo_tile_interleave(std::vector<std::uint8_t>& bytes) {
  std::vector<std::uint8_t> output(bytes.size());
  for (std::size_t i = 0; i < 0x4000U; ++i) {
    output[4 * i + 1] = bytes[2 * i];
    output[4 * i + 3] = bytes[2 * i + 1];
    output[4 * i + 0] = bytes[0x8000U + 2 * i];
    output[4 * i + 2] = bytes[0x8000U + 2 * i + 1];
  }
  bytes = std::move(output);
}

[[nodiscard]] std::vector<std::uint8_t> extract_stream(std::span<const std::uint8_t> bytes,
                                                       const StreamRecord& record,
                                                       const std::string& name,
                                                       std::vector<std::string>* notices) {
  if (record.size == 0 || record.size > kMaxStreamBytes) {
    throw std::runtime_error("Affinity stream '" + name + "' has an implausible size");
  }
  if (record.data_offset + 4 > bytes.size()) {
    throw std::runtime_error("Affinity stream '" + name + "' is out of range");
  }
  auto reader = af_reader(bytes);
  reader.seek(static_cast<std::size_t>(record.data_offset));
  if (reader.read_u32() != kTagFil) {
    throw std::runtime_error("Affinity stream '" + name + "' is corrupt");
  }

  const std::uint32_t algorithm = record.compression & 0x03U;
  bool alternate_predictor = ((record.compression >> 5U) & 1U) != 0;
  std::uint32_t predictor = record.compression & 0xC0U;
  switch (predictor) {
    case 0x40: predictor = 1; alternate_predictor = false; break;
    case 0x80: predictor = 2; break;
    case 0xC0: predictor = 3; break;
    default: predictor = 0; alternate_predictor = false; break;
  }

  const std::uint64_t stored_size = algorithm == 0 ? record.size : record.compressed_size;
  if (stored_size > reader.remaining()) {
    throw std::runtime_error("Affinity stream '" + name + "' is truncated");
  }
  const auto* stored = bytes.data() + reader.position();

  std::vector<std::uint8_t> decoded(static_cast<std::size_t>(record.size));
  if (algorithm == 1) {
    mz_ulong out_length = static_cast<mz_ulong>(record.size);
    if (mz_uncompress(decoded.data(), &out_length, stored, static_cast<mz_ulong>(stored_size)) !=
            MZ_OK ||
        out_length != record.size) {
      throw std::runtime_error("Affinity stream '" + name + "' failed to decompress");
    }
  } else if (algorithm == 2) {
    const std::size_t produced =
        ZSTD_decompress(decoded.data(), decoded.size(), stored, static_cast<std::size_t>(stored_size));
    if (ZSTD_isError(produced) != 0U || produced != decoded.size()) {
      throw std::runtime_error("Affinity stream '" + name + "' failed to decompress");
    }
  } else {
    std::memcpy(decoded.data(), stored, static_cast<std::size_t>(stored_size));
  }

  if (!alternate_predictor && predictor == 1) {
    undo_byte_delta(decoded);
  } else if (!alternate_predictor && predictor == 2) {
    undo_u16_delta(decoded);
  } else if (alternate_predictor && predictor == 2) {
    undo_byte_delta(decoded);
    if (decoded.size() == 0x10000U) {
      undo_tile_interleave(decoded);
    }
  }

  const auto checksum = static_cast<std::uint32_t>(
      mz_crc32(MZ_CRC32_INIT, decoded.data(), decoded.size()));
  if (checksum != record.crc32 && notices != nullptr) {
    notices->push_back("Affinity stream '" + name + "' failed its checksum; the file may be damaged");
  }
  return decoded;
}

// ---------------------------------------------------------------- PNG preview

// Minimal decoder for the one PNG producer that matters here: Affinity writes
// 8-bit, non-interlaced previews (RGBA in every observed file; RGB and
// grayscale accepted defensively). Not a general PNG codec on purpose.
[[nodiscard]] PixelBuffer decode_preview_png(std::span<const std::uint8_t> png) {
  static constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 0x50, 0x4E, 0x47,
                                                             0x0D, 0x0A, 0x1A, 0x0A};
  auto reader = LittleEndianReader(png, "Affinity preview image is truncated");
  for (const auto expected : kSignature) {
    if (reader.read_u8() != expected) {
      throw std::runtime_error("Affinity preview image is not a PNG");
    }
  }
  const auto read_be32 = [&reader]() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      value = (value << 8U) | reader.read_u8();
    }
    return value;
  };

  std::int32_t width = 0;
  std::int32_t height = 0;
  int channels = 0;
  std::vector<std::uint8_t> compressed;
  bool saw_end = false;
  while (!saw_end) {
    const std::uint32_t length = read_be32();
    const std::uint32_t type = read_be32();
    if (length > png.size()) {
      throw std::runtime_error("Affinity preview image is corrupt");
    }
    const std::size_t data_start = reader.position();
    switch (type) {
      case 0x49484452U: {  // IHDR
        width = static_cast<std::int32_t>(read_be32());
        height = static_cast<std::int32_t>(read_be32());
        const std::uint8_t bit_depth = reader.read_u8();
        const std::uint8_t color_type = reader.read_u8();
        reader.skip(1);  // compression method (0 is the only defined value)
        reader.skip(1);  // filter method
        const std::uint8_t interlace = reader.read_u8();
        if (width <= 0 || height <= 0 || width > kMaxThumbnailSide || height > kMaxThumbnailSide) {
          throw std::runtime_error("Affinity preview image has implausible dimensions");
        }
        if (bit_depth != 8 || interlace != 0) {
          throw std::runtime_error("Affinity preview image uses an unsupported PNG variant");
        }
        switch (color_type) {
          case 0: channels = 1; break;
          case 2: channels = 3; break;
          case 6: channels = 4; break;
          default:
            throw std::runtime_error("Affinity preview image uses an unsupported PNG variant");
        }
        break;
      }
      case 0x49444154U: {  // IDAT
        for (std::uint32_t i = 0; i < length; ++i) {
          compressed.push_back(reader.read_u8());
        }
        break;
      }
      case 0x49454E44U:  // IEND
        saw_end = true;
        break;
      default:
        reader.skip(length);
        break;
    }
    reader.seek(data_start + length);
    reader.skip(4);  // chunk CRC (already covered by the stream checksum)
  }
  if (channels == 0 || compressed.empty()) {
    throw std::runtime_error("Affinity preview image is incomplete");
  }

  const std::size_t row_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
  const std::size_t raw_size = (row_bytes + 1) * static_cast<std::size_t>(height);
  std::vector<std::uint8_t> raw(raw_size);
  mz_ulong out_length = static_cast<mz_ulong>(raw_size);
  if (mz_uncompress(raw.data(), &out_length, compressed.data(),
                    static_cast<mz_ulong>(compressed.size())) != MZ_OK ||
      out_length != raw_size) {
    throw std::runtime_error("Affinity preview image failed to decompress");
  }

  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  std::vector<std::uint8_t> previous_row(row_bytes, 0);
  const auto paeth = [](int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
      return a;
    }
    return pb <= pc ? b : c;
  };
  for (std::int32_t y = 0; y < height; ++y) {
    const std::uint8_t filter = raw[static_cast<std::size_t>(y) * (row_bytes + 1)];
    std::uint8_t* row = raw.data() + static_cast<std::size_t>(y) * (row_bytes + 1) + 1;
    if (filter > 4) {
      throw std::runtime_error("Affinity preview image is corrupt");
    }
    for (std::size_t i = 0; i < row_bytes; ++i) {
      const int left = i >= static_cast<std::size_t>(channels) ? row[i - channels] : 0;
      const int up = previous_row[i];
      const int up_left = i >= static_cast<std::size_t>(channels) ? previous_row[i - channels] : 0;
      int reconstructed = row[i];
      switch (filter) {
        case 1: reconstructed += left; break;
        case 2: reconstructed += up; break;
        case 3: reconstructed += (left + up) / 2; break;
        case 4: reconstructed += paeth(left, up, up_left); break;
        default: break;
      }
      row[i] = static_cast<std::uint8_t>(reconstructed & 0xFF);
    }
    std::memcpy(previous_row.data(), row, row_bytes);
    auto destination = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      const std::uint8_t* source = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
      std::uint8_t* out = destination.data() + static_cast<std::size_t>(x) * 4U;
      if (channels == 1) {
        out[0] = out[1] = out[2] = source[0];
        out[3] = 0xFF;
      } else {
        out[0] = source[0];
        out[1] = source[1];
        out[2] = source[2];
        out[3] = channels == 4 ? source[3] : 0xFF;
      }
    }
  }
  return pixels;
}

[[nodiscard]] PixelBuffer extract_preview(std::span<const std::uint8_t> bytes,
                                          const Container& container) {
  const std::uint64_t offset = container.thumbnail_offset;
  if (offset == 0 || offset + 8 > bytes.size()) {
    throw std::runtime_error("Affinity document has no embedded preview");
  }
  auto reader = af_reader(bytes);
  reader.seek(static_cast<std::size_t>(offset));
  if (reader.read_u32() != 0xFFFFFFFFU || reader.read_u32() != kTagThmb) {
    throw std::runtime_error("Affinity document has no embedded preview");
  }
  // A short header precedes the PNG; scan a bounded window for its signature.
  static constexpr std::array<std::uint8_t, 4> kPngStart = {0x89, 0x50, 0x4E, 0x47};
  const std::size_t scan_start = reader.position();
  const std::size_t scan_end = std::min(bytes.size(), scan_start + 256);
  for (std::size_t at = scan_start; at + kPngStart.size() <= scan_end; ++at) {
    if (std::equal(kPngStart.begin(), kPngStart.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at))) {
      return decode_preview_png(bytes.subspan(at));
    }
  }
  throw std::runtime_error("Affinity document has no embedded preview");
}

// The document tree stream opens with its own tagged header; only the version is
// read here (tier 0 does not walk the tree). Returns 0 when the stream is absent
// or unrecognizable, which downgrades to a notice rather than a failure.
[[nodiscard]] std::uint32_t read_document_version(std::span<const std::uint8_t> bytes,
                                                  const Container& container,
                                                  std::vector<std::string>* notices) {
  const auto found = container.streams.find("doc.dat");
  if (found == container.streams.end()) {
    return 0;
  }
  try {
    const auto document_stream = extract_stream(bytes, found->second, "doc.dat", notices);
    auto reader = LittleEndianReader(std::span<const std::uint8_t>(document_stream),
                                     "Affinity document tree is truncated");
    if (reader.read_u32() != kTagDoc) {
      return 0;
    }
    const std::uint16_t file_version = reader.read_u16();
    reader.skip(4 + 2);  // root class tag + tag version
    return file_version >= 2 ? reader.read_u32() : 0;
  } catch (const std::exception&) {
    return 0;
  }
}

}  // namespace

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 0xFF && bytes[2] == 0x4B &&
         bytes[3] == 0x41;
}

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return sniff(bytes);
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  if (!sniff(bytes)) {
    throw std::runtime_error("Not an Affinity document");
  }
  const Container container = parse_container(bytes);
  std::vector<std::string> pending;
  if (container.version > kNewestVerifiedContainerVersion) {
    pending.push_back("This file was saved by a newer Affinity (container version " +
                      std::to_string(container.version) + "; Patchy has been verified up to " +
                      std::to_string(kNewestVerifiedContainerVersion) +
                      "); the preview import may be incomplete");
  }
  const std::uint32_t document_version = read_document_version(bytes, container, &pending);

  PixelBuffer preview = extract_preview(bytes, container);
  Document document(preview.width(), preview.height(), PixelFormat::rgba8());
  document.add_pixel_layer("Affinity preview", std::move(preview));

  if (notices != nullptr) {
    std::string summary = "Imported the embedded Affinity preview only (" +
                          std::to_string(document.width()) + "x" +
                          std::to_string(document.height()) +
                          "; the document's full-resolution layers are not decoded yet)";
    if (document_version != 0) {
      summary += "; document format version " + std::to_string(document_version);
    }
    notices->push_back(std::move(summary));
    for (auto& notice : pending) {
      notices->push_back(std::move(notice));
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path,
                               std::vector<std::string>* notices) {
  const auto bytes = formats::read_file_bytes(path, "Affinity");
  return read(std::span<const std::uint8_t>(bytes), notices);
}

}  // namespace patchy::af
