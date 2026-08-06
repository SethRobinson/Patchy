#include "formats/af_document_io.hpp"

#include "core/environment.hpp"

#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/rect_utils.hpp"
#include "core/smart_object.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_smart_objects.hpp"
#include "psd/psd_text_runs.hpp"
#include "color/color_management.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/af_tree.hpp"
#include "formats/binary_le.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/stb/stb_image.h"
#include "formats/zstd/zstd.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <tuple>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
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

// Read-time-only marker for a Gaussian-blur layer effect awaiting its bake
// (resolved and ERASED by bake_pending_blur_effects before read() returns, so
// it never rides a saved document). Value: "<radius> <opacity> <preserve>".
constexpr const char* kAfPendingBlurKey = "patchy.af.pending_blur";

constexpr std::uint32_t kMagic = 0x414BFF00U;
constexpr std::uint32_t kTagInf = 0x666E4923U;   // "#Inf"
constexpr std::uint32_t kTagProt = 0x746F7250U;  // "Prot"
constexpr std::uint32_t kTagFil = 0x6C694623U;   // "#Fil"
constexpr std::uint32_t kTagThmb = 0x626D6854U;  // "Thmb"
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

  // Stream-table chain. The #Inf offset points at the NEWEST link and each
  // link's next_offset leads to an OLDER save revision, so the head link's
  // records are the live ones. A stream's name may be declared (flag 0) only
  // in an older link than its newest data record (flag 1), so resolution is
  // two-phase: walk the chain collecting every link, then apply the links
  // oldest-to-newest with newer records overwriting. One pass that overwrote
  // in walk order imported an incrementally-saved document's OLDEST doc.dat
  // (stale text styling, missing effects).
  std::unordered_map<std::uint32_t, std::string> names_by_id;
  struct PendingStream {
    std::uint32_t id{};
    StreamRecord record;
  };
  std::vector<std::vector<PendingStream>> chain_links;
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
    chain_links.emplace_back();
    auto& link = chain_links.back();
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
        // First-seen wins: the walk goes newest-to-oldest, so a rename in a
        // newer revision keeps the newer name.
        names_by_id.try_emplace(id, std::move(name));
      }
      if (flag == 0 || flag == 1) {
        link.push_back(PendingStream{id, record});
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
  // Apply oldest link first so newer revisions overwrite: the head (newest)
  // revision of every stream wins.
  for (auto link = chain_links.rbegin(); link != chain_links.rend(); ++link) {
    for (const auto& pending : *link) {
      const auto found = names_by_id.find(pending.id);
      if (found != names_by_id.end()) {
        container.streams[found->second] = pending.record;
      }
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

// ---------------------------------------------------------------- tier 1

constexpr std::int32_t kMaxLayerSide = 300000;      // matches the PSB dimension cap
constexpr int kTileSize = 256;

// Runtime 4CC (for channel-indexed tags like "Idx1"); matches af::tag4's layout.
[[nodiscard]] std::uint32_t tag_of(const std::string& s) {
  if (s.size() != 4) {
    return 0;
  }
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0])) << 24U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 16U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])) << 8U) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[3]));
}

// DyBm bitmap format ids (the app's RasterFormat enum). M8/M16 are the
// single-channel mask planes; CMYK converts through the embedded ICC profile
// when the bitmap carries one, else approximately. LabA16 stores the ICC v4
// Lab16 PCS encoding on the wire (pinned July 2026 against a saturated
// calibration doc; the old "compressed a/b scale" mystery was a desaturated
// probe document) and converts through lcms2's built-in Lab profile.
enum class RasterFormat {
  RGBA8,
  RGBA16,
  Gray8,
  Gray16,
  CMYKA8,
  LabA16,
  Mask8,
  Mask16,
  RGBAFloat,
  Unsupported
};

[[nodiscard]] RasterFormat classify_format(std::int64_t id) {
  switch (id) {
    case 0: return RasterFormat::RGBA8;
    case 1: return RasterFormat::RGBA16;
    case 2: return RasterFormat::Gray8;
    case 3: return RasterFormat::Gray16;
    case 4: return RasterFormat::CMYKA8;
    case 5: return RasterFormat::LabA16;
    case 6: return RasterFormat::Mask8;
    case 7: return RasterFormat::Mask16;
    case 9: return RasterFormat::RGBAFloat;
    default: return RasterFormat::Unsupported;
  }
}

[[nodiscard]] std::uint8_t linear_to_srgb8(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  const float srgb = value <= 0.0031308F ? value * 12.92F
                                         : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
  return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}

[[nodiscard]] float srgb_to_linear(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

// ------------------------------------------------------------- channel planes

// One decoded channel plane. Tiles are always 256 bytes wide by 256 rows
// regardless of sample depth, so the tile-grid width fields count 256-BYTE
// columns (a 16-bit channel's row spans width*2 bytes) while the height fields
// count plain 256-row bands; the plane's horizontal axis is bytes, not samples.
struct ChannelPlane {
  std::vector<std::uint8_t> bytes;
  std::size_t width_bytes{0};
  std::size_t rows{0};
};

// Sta tile codes: 0/1 empty, 2 fill max, 3 fill float 1.0, 4 stored tile,
// 5 base pixels come from the layer's placed-original image (the Bckg stream).
constexpr std::int64_t kTileFillMax = 2;
constexpr std::int64_t kTileFillFloatOne = 3;
constexpr std::int64_t kTileStored = 4;
constexpr std::int64_t kTileFromOriginal = 5;

// Per-channel tile-grid field tags. The base level uses TWi<n>/THi<n>/Idx<n>/
// Sta<n>; mip levels 1..7 use 'M','W'|'H'|'I'|'T',<raw level byte>,<digit>.
struct PlaneTags {
  std::uint32_t tiles_w{0};
  std::uint32_t tiles_h{0};
  std::uint32_t index{0};
  std::uint32_t status{0};
};

[[nodiscard]] PlaneTags base_plane_tags(int channel) {
  return {tag_of("TWi" + std::to_string(channel)), tag_of("THi" + std::to_string(channel)),
          tag_of("Idx" + std::to_string(channel)), tag_of("Sta" + std::to_string(channel))};
}

[[nodiscard]] std::uint32_t mip_tag(char kind, int level, int channel) {
  return (static_cast<std::uint32_t>('M') << 24U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(kind)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(level)) << 8U) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>('0' + channel));
}

[[nodiscard]] PlaneTags mip_plane_tags(int level, int channel) {
  return {mip_tag('W', level, channel), mip_tag('H', level, channel),
          mip_tag('I', level, channel), mip_tag('T', level, channel)};
}

// Fill a run of samples with the format's "full" value: 0xFF bytes for the
// integer formats (255 / 65535), 1.0f for float planes (0xFF bytes would be NaN).
void fill_full_samples(std::uint8_t* dst, std::size_t sample_count, std::size_t sample_bytes,
                       bool is_float) {
  if (!is_float) {
    std::memset(dst, 0xFF, sample_count * sample_bytes);
    return;
  }
  const float one = 1.0F;
  for (std::size_t i = 0; i < sample_count; ++i) {
    std::memcpy(dst + i * 4U, &one, sizeof(one));
  }
}

enum class PlaneStatus { Ok, NeedsOriginal, UnknownCode, Invalid };

// Decode one channel plane from its tile streams. `source` (same plane layout)
// supplies the pixels of code-5 tiles; without one a code-5 plane reports
// NeedsOriginal so the caller can build a source (original image or mip
// fallback) and decode again. `out` keeps its layout dimensions either way.
// `plane_width`/`plane_height` are the plane's pixel dimensions (base bitmap
// or mip level): old-generation (1.x, doc version <= 9) DyBms store no
// TWi/THi tile-grid fields at all, so when both are absent the grid is
// derived from those dimensions (256-byte columns by 256-row bands; the
// derived count matches every Sta list across the dbacchet v9 schematic).
[[nodiscard]] PlaneStatus decode_channel_plane(std::span<const std::uint8_t> bytes,
                                               const Container& container, const af::AfClass& dybm,
                                               const PlaneTags& tags, std::size_t sample_bytes,
                                               bool is_float, std::int64_t plane_width,
                                               std::int64_t plane_height,
                                               const ChannelPlane* source, ChannelPlane& out) {
  std::int64_t tiles_w = dybm.int_field(tags.tiles_w, 1);
  std::int64_t tiles_h = dybm.int_field(tags.tiles_h, 1);
  if (dybm.field(tags.tiles_w) == nullptr && dybm.field(tags.tiles_h) == nullptr) {
    const std::int64_t derived_w = std::max<std::int64_t>(
        1, (plane_width * static_cast<std::int64_t>(sample_bytes) + kTileSize - 1) / kTileSize);
    const std::int64_t derived_h =
        std::max<std::int64_t>(1, (plane_height + kTileSize - 1) / kTileSize);
    // Only trust the derivation when the plane carries a Sta list of exactly
    // that many codes. A plane with NO status field must keep the 1x1
    // default: 2.x DyBms whose pixels come from a placed original (code-5
    // tiles) often store no mip fields at all, and an oversized empty mip
    // plane would otherwise pass mip_source_planes' size check and feed the
    // code-5 tiles a BLANK source instead of failing to the honest
    // placeholder (paulgessinger screens regressed this way).
    const auto* derived_sta = dybm.field(tags.status);
    const auto* derived_codes =
        derived_sta != nullptr ? std::get_if<std::vector<std::int64_t>>(&derived_sta->value)
                               : nullptr;
    if (derived_codes != nullptr &&
        static_cast<std::int64_t>(derived_codes->size()) == derived_w * derived_h) {
      tiles_w = derived_w;
      tiles_h = derived_h;
    }
  }
  if (tiles_w <= 0 || tiles_h <= 0 || tiles_w > 8192 || tiles_h > 8192 ||
      tiles_w * tiles_h > (1LL << 20U)) {
    return PlaneStatus::Invalid;
  }
  out.width_bytes = static_cast<std::size_t>(tiles_w) * kTileSize;
  out.rows = static_cast<std::size_t>(tiles_h) * kTileSize;
  out.bytes.assign(out.width_bytes * out.rows, 0);

  const auto* sta = dybm.field(tags.status);
  if (sta == nullptr) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af]   plane tag %08x: no status field\n", tags.status);
    }
    return PlaneStatus::Ok;
  }
  const auto* codes = std::get_if<std::vector<std::int64_t>>(&sta->value);
  if (codes == nullptr) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af]   plane tag %08x: status has variant index %zu\n", tags.status,
                   sta->value.index());
    }
    return PlaneStatus::Ok;
  }
  const auto* idx = dybm.field(tags.index);
  const auto* blocks =
      idx != nullptr ? std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&idx->value) : nullptr;
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::array<int, 8> histogram{};
    for (const auto code : *codes) {
      if (code >= 0 && code < 8) {
        ++histogram[static_cast<std::size_t>(code)];
      }
    }
    std::fprintf(stderr,
                 "[af]   plane %08x: %zu codes [0]=%d [2]=%d [4]=%d [5]=%d blocks=%zu src=%d\n",
                 tags.status, codes->size(), histogram[0], histogram[2], histogram[4], histogram[5],
                 blocks != nullptr ? blocks->size() : 0U, source != nullptr ? 1 : 0);
  }
  std::size_t block_index = 0;
  for (std::size_t t = 0; t < codes->size(); ++t) {
    const std::size_t tx = (t % static_cast<std::size_t>(tiles_w)) * kTileSize;
    const std::size_t ty = (t / static_cast<std::size_t>(tiles_w)) * kTileSize;
    if (ty >= out.rows) {
      break;  // more status codes than grid slots; ignore the excess
    }
    const std::int64_t code = (*codes)[t];
    switch (code) {
      case 0:
      case 1:
        break;
      case kTileFillMax:
      case kTileFillFloatOne:
        for (std::size_t row = 0; row < static_cast<std::size_t>(kTileSize); ++row) {
          fill_full_samples(out.bytes.data() + (ty + row) * out.width_bytes + tx,
                            static_cast<std::size_t>(kTileSize) / sample_bytes, sample_bytes,
                            is_float);
        }
        break;
      case kTileFromOriginal: {
        if (source == nullptr) {
          return PlaneStatus::NeedsOriginal;
        }
        if (source->width_bytes != out.width_bytes || source->rows != out.rows) {
          return PlaneStatus::Invalid;
        }
        for (std::size_t row = 0; row < static_cast<std::size_t>(kTileSize); ++row) {
          const std::size_t at = (ty + row) * out.width_bytes + tx;
          std::memcpy(out.bytes.data() + at, source->bytes.data() + at,
                      static_cast<std::size_t>(kTileSize));
        }
        break;
      }
      case kTileStored: {
        if (blocks == nullptr || block_index >= blocks->size() ||
            (*blocks)[block_index] == nullptr) {
          ++block_index;
          break;
        }
        const af::AfClass& block = *(*blocks)[block_index];
        ++block_index;
        const auto* data_field = block.field(af::tag4("Data"));
        if (data_field == nullptr) {
          break;
        }
        const auto* embedded = std::get_if<af::AfEmbedded>(&data_field->value);
        if (embedded == nullptr) {
          break;
        }
        const auto stream = container.streams.find(embedded->data);
        if (stream == container.streams.end()) {
          break;
        }
        std::vector<std::uint8_t> tile;
        try {
          tile = extract_stream(bytes, stream->second, embedded->data, nullptr);
        } catch (const std::exception& error) {
          if (environment_variable_is_set("PATCHY_AF_TRACE")) {
            std::fprintf(stderr, "[af]   tile '%s': %s\n", embedded->data.c_str(), error.what());
          }
          break;
        }
        if (tile.size() == static_cast<std::size_t>(kTileSize) * kTileSize) {
          for (std::size_t row = 0; row < static_cast<std::size_t>(kTileSize); ++row) {
            std::memcpy(out.bytes.data() + (ty + row) * out.width_bytes + tx,
                        tile.data() + row * static_cast<std::size_t>(kTileSize),
                        static_cast<std::size_t>(kTileSize));
          }
          break;
        }
        // Short stream: a partial tile carrying only a sub-rect, described by
        // the block's optional Rect (byte-pitch x like the tile grid). Accept
        // either [x, y, w, h] or [x0, y0, x1, y1], validated by the stream
        // size; anything that does not line up stays skipped (transparent).
        {
          const auto rect = block.vec_field(af::tag4("Rect"));
          if (rect.size() != 4) {
            break;
          }
          const auto rx = static_cast<std::int64_t>(rect[0]);
          const auto ry = static_cast<std::int64_t>(rect[1]);
          std::int64_t rw = static_cast<std::int64_t>(rect[2]);
          std::int64_t rh = static_cast<std::int64_t>(rect[3]);
          if (rw > rx && rh > ry &&
              static_cast<std::uint64_t>(rw - rx) * static_cast<std::uint64_t>(rh - ry) ==
                  tile.size()) {
            rw -= rx;  // [x0, y0, x1, y1]
            rh -= ry;
          }
          if (rx < 0 || ry < 0 || rw <= 0 || rh <= 0 || rx + rw > kTileSize ||
              ry + rh > kTileSize ||
              static_cast<std::uint64_t>(rw) * static_cast<std::uint64_t>(rh) != tile.size()) {
            break;
          }
          for (std::int64_t row = 0; row < rh; ++row) {
            std::memcpy(out.bytes.data() +
                            (ty + static_cast<std::size_t>(ry + row)) * out.width_bytes + tx +
                            static_cast<std::size_t>(rx),
                        tile.data() + static_cast<std::size_t>(row * rw),
                        static_cast<std::size_t>(rw));
          }
        }
        break;
      }
      default:
        return PlaneStatus::UnknownCode;
    }
  }
  return PlaneStatus::Ok;
}

// Sample scale matches the compose step below: 8/16-bit samples map onto the
// 0..255 range (value/257 for 16-bit), float planes return their raw
// linear-light value.
[[nodiscard]] float sample_plane(const ChannelPlane& plane, std::size_t sample_bytes, bool is_float,
                                 std::int32_t x, std::int32_t y) {
  const std::uint8_t* p = plane.bytes.data() + static_cast<std::size_t>(y) * plane.width_bytes +
                          static_cast<std::size_t>(x) * sample_bytes;
  if (is_float) {
    float v = 0.0F;
    std::memcpy(&v, p, sizeof(v));
    return v;
  }
  if (sample_bytes == 2) {
    const std::uint16_t v =
        static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8U));
    return static_cast<float>(v) / 257.0F;
  }
  return static_cast<float>(p[0]);
}

// ---------------------------------------------------- placed-original sources

// A DyBm whose base tiles carry code 5 stores its full-resolution pixels as the
// untouched original image file in the c/<n> stream named by its `Bckg` field:
// a serialized Blck tree (same wire grammar as doc.dat) with Data = the file
// bytes, TifO = the EXIF orientation, DSrc/Filn = the source path. The vendored
// stb_image decodes the JPEG/PNG originals; other embedded formats fall back to
// the stored mip pyramid at half resolution.
[[nodiscard]] std::optional<PixelBuffer> decode_original_file_bytes(
    const std::vector<std::uint8_t>& file, int orientation) {
  if (file.empty() || file.size() > kMaxStreamBytes) {
    return std::nullopt;
  }
  int width = 0;
  int height = 0;
  int components = 0;
  if (stbi_info_from_memory(file.data(), static_cast<int>(file.size()), &width, &height,
                            &components) == 0) {
    return std::nullopt;
  }
  if (width <= 0 || height <= 0 || width > kMaxLayerSide || height > kMaxLayerSide ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > (1ULL << 28U)) {
    return std::nullopt;  // keep decoded RGBA under 1 GiB
  }
  stbi_uc* decoded = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &width,
                                           &height, &components, 4);
  if (decoded == nullptr) {
    return std::nullopt;
  }
  const std::span<const std::uint8_t> rgba(
      decoded, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  PixelBuffer pixels;
  if (orientation >= 2 && orientation <= 8) {
    const heif::OrientedImage oriented =
        heif::apply_exif_orientation(rgba, width, height, orientation);
    pixels = PixelBuffer(oriented.width, oriented.height, PixelFormat::rgba8());
    for (std::int32_t y = 0; y < oriented.height; ++y) {
      std::memcpy(pixels.row(y).data(),
                  oriented.rgba.data() + static_cast<std::size_t>(y) * oriented.width * 4U,
                  static_cast<std::size_t>(oriented.width) * 4U);
    }
  } else {
    pixels = PixelBuffer(width, height, PixelFormat::rgba8());
    for (int y = 0; y < height; ++y) {
      std::memcpy(pixels.row(y).data(),
                  rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U,
                  static_cast<std::size_t>(width) * 4U);
    }
  }
  stbi_image_free(decoded);
  return pixels;
}

struct EmbeddedOriginal {
  PixelBuffer pixels;  // decoded, orientation-applied
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;  // the untouched file
  std::string name;                                        // source path basename
};

[[nodiscard]] std::optional<EmbeddedOriginal> decode_embedded_original(
    std::span<const std::uint8_t> bytes, const Container& container, const af::AfClass& dybm) {
  const auto trace = [&](const char* what) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af] placed original: %s\n", what);
    }
  };
  const auto* field = dybm.field(af::tag4("Bckg"));
  if (field == nullptr) {
    trace("no Bckg field");
    return std::nullopt;
  }
  const auto* embedded = std::get_if<af::AfEmbedded>(&field->value);
  if (embedded == nullptr || embedded->data.empty()) {
    trace("Bckg is not an embedded stream reference");
    return std::nullopt;
  }
  const auto stream = container.streams.find(embedded->data);
  if (stream == container.streams.end()) {
    trace("Bckg stream is missing from the container");
    return std::nullopt;
  }
  try {
    const auto blob = extract_stream(bytes, stream->second, embedded->data, nullptr);
    const af::AfDocument block = af::parse_tree(std::span<const std::uint8_t>(blob));
    if (block.root == nullptr) {
      trace("Bckg block tree has no root");
      return std::nullopt;
    }
    const auto* data_field = block.root->field(af::tag4("Data"));
    const auto* data = data_field != nullptr
                           ? std::get_if<std::vector<std::uint8_t>>(&data_field->value)
                           : nullptr;
    if (data == nullptr) {
      trace("Bckg block tree has no Data bytes");
      return std::nullopt;
    }
    const int orientation = static_cast<int>(block.root->int_field(af::tag4("TifO"), 1));
    auto pixels = decode_original_file_bytes(*data, orientation);
    if (!pixels) {
      trace("original file bytes did not decode (not JPEG/PNG?)");
      return std::nullopt;
    }
    EmbeddedOriginal original;
    original.pixels = std::move(*pixels);
    original.bytes = std::make_shared<const std::vector<std::uint8_t>>(*data);
    std::string path;
    if (const af::AfClass* source = block.root->child_class(af::tag4("DSrc"))) {
      path = source->string_field(af::tag4("Filn"));
    }
    const auto separator = path.find_last_of("/\\");
    original.name = separator == std::string::npos ? path : path.substr(separator + 1);
    return original;
  } catch (const std::exception& error) {
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af] placed original: threw '%s'\n", error.what());
    }
    return std::nullopt;
  }
}

// Build the code-5 source plane for one channel in `layout`'s byte layout.
// Channels 1..4 map onto R,G,B,A of the decoded original; deep formats
// re-encode per sample (16-bit value*257, float linear light).
[[nodiscard]] ChannelPlane source_plane_from_original(const PixelBuffer& original,
                                                      const ChannelPlane& layout, int channel,
                                                      std::size_t sample_bytes, bool is_float) {
  ChannelPlane plane;
  plane.width_bytes = layout.width_bytes;
  plane.rows = layout.rows;
  plane.bytes.assign(plane.width_bytes * plane.rows, 0);
  const int component = std::clamp(channel - 1, 0, 3);
  for (std::int32_t y = 0; y < original.height() && static_cast<std::size_t>(y) < plane.rows; ++y) {
    std::uint8_t* dst_row = plane.bytes.data() + static_cast<std::size_t>(y) * plane.width_bytes;
    for (std::int32_t x = 0; x < original.width(); ++x) {
      const std::size_t at = static_cast<std::size_t>(x) * sample_bytes;
      if (at + sample_bytes > plane.width_bytes) {
        break;
      }
      const std::uint8_t value = original.pixel(x, y)[component];
      if (is_float) {
        const float f = component < 3 ? srgb_to_linear(static_cast<float>(value) / 255.0F)
                                      : static_cast<float>(value) / 255.0F;
        std::memcpy(dst_row + at, &f, sizeof(f));
      } else if (sample_bytes == 2) {
        const std::uint16_t wide = static_cast<std::uint16_t>(value * 257U);
        dst_row[at] = static_cast<std::uint8_t>(wide & 0xFFU);
        dst_row[at + 1] = static_cast<std::uint8_t>(wide >> 8U);
      } else {
        dst_row[at] = value;
      }
    }
  }
  return plane;
}

// Half-resolution fallback: decode each channel's mip level-1 plane (stored as
// plain tiles) and upsample it 2x into the base layouts with bilinear
// filtering. Deterministic plain-float math (cross-toolchain rule).
[[nodiscard]] bool mip_source_planes(std::span<const std::uint8_t> bytes, const Container& container,
                                     const af::AfClass& dybm, int channel_count,
                                     std::size_t sample_bytes, bool is_float, std::int64_t width,
                                     std::int64_t height, const std::vector<ChannelPlane>& layouts,
                                     std::vector<ChannelPlane>& out) {
  const std::int64_t mip_width = (width + 1) / 2;
  const std::int64_t mip_height = (height + 1) / 2;
  for (int channel = 1; channel <= channel_count; ++channel) {
    ChannelPlane mip;
    const PlaneStatus status =
        decode_channel_plane(bytes, container, dybm, mip_plane_tags(1, channel), sample_bytes,
                             is_float, mip_width, mip_height, nullptr, mip);
    if (status != PlaneStatus::Ok ||
        static_cast<std::size_t>(mip_width) * sample_bytes > mip.width_bytes ||
        static_cast<std::size_t>(mip_height) > mip.rows) {
      return false;
    }
    const ChannelPlane& layout = layouts[static_cast<std::size_t>(channel - 1)];
    ChannelPlane scaled;
    scaled.width_bytes = layout.width_bytes;
    scaled.rows = layout.rows;
    scaled.bytes.assign(scaled.width_bytes * scaled.rows, 0);
    for (std::int64_t y = 0; y < height && static_cast<std::size_t>(y) < scaled.rows; ++y) {
      for (std::int64_t x = 0; x < width; ++x) {
        const std::size_t at = static_cast<std::size_t>(x) * sample_bytes;
        if (at + sample_bytes > scaled.width_bytes) {
          break;
        }
        const float sx = std::clamp((static_cast<float>(x) + 0.5F) * 0.5F - 0.5F, 0.0F,
                                    static_cast<float>(mip_width - 1));
        const float sy = std::clamp((static_cast<float>(y) + 0.5F) * 0.5F - 0.5F, 0.0F,
                                    static_cast<float>(mip_height - 1));
        const auto x0 = static_cast<std::int32_t>(sx);
        const auto y0 = static_cast<std::int32_t>(sy);
        const std::int32_t x1 =
            std::min<std::int32_t>(x0 + 1, static_cast<std::int32_t>(mip_width - 1));
        const std::int32_t y1 =
            std::min<std::int32_t>(y0 + 1, static_cast<std::int32_t>(mip_height - 1));
        const float fx = sx - static_cast<float>(x0);
        const float fy = sy - static_cast<float>(y0);
        const float top = sample_plane(mip, sample_bytes, is_float, x0, y0) * (1.0F - fx) +
                          sample_plane(mip, sample_bytes, is_float, x1, y0) * fx;
        const float bottom = sample_plane(mip, sample_bytes, is_float, x0, y1) * (1.0F - fx) +
                             sample_plane(mip, sample_bytes, is_float, x1, y1) * fx;
        const float value = top * (1.0F - fy) + bottom * fy;
        std::uint8_t* dst =
            scaled.bytes.data() + static_cast<std::size_t>(y) * scaled.width_bytes + at;
        if (is_float) {
          std::memcpy(dst, &value, sizeof(value));
        } else if (sample_bytes == 2) {
          const auto wide = static_cast<std::uint16_t>(
              std::lround(std::clamp(value, 0.0F, 255.0F) * 257.0F));
          dst[0] = static_cast<std::uint8_t>(wide & 0xFFU);
          dst[1] = static_cast<std::uint8_t>(wide >> 8U);
        } else {
          dst[0] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 255.0F)));
        }
      }
    }
    out.push_back(std::move(scaled));
  }
  return true;
}

struct DecodedBitmap {
  PixelBuffer rgba;          // straight-alpha RGBA8 (empty for mask formats)
  PixelBuffer mask;          // gray8 plane (mask formats only)
  bool approximate_color{false};  // CMYK/Lab converted without an ICC profile
  // Set when the base plane came ENTIRELY from the embedded placed original
  // (every base tile code 5/fill, none hand-painted): the untouched file and
  // its source filename, for the smart-object wrapper.
  std::shared_ptr<const std::vector<std::uint8_t>> original_bytes;
  std::string original_name;
};

// Decode a DyBm bitmap. RGBA/gray/float/CMYK produce `rgba`; the mask formats
// (M8/M16) produce `mask`. Returns nullopt for empty/unknown/undecodable
// bitmaps, with a notice-ready reason in `fail_reason` when one is known.
[[nodiscard]] std::optional<DecodedBitmap> decode_bitmap(std::span<const std::uint8_t> bytes,
                                                         const Container& container,
                                                         const af::AfClass& dybm,
                                                         const std::string& layer_name,
                                                         std::vector<std::string>* notices,
                                                         std::string* fail_reason) {
  const auto set_reason = [&](const char* reason) {
    if (fail_reason != nullptr) {
      *fail_reason = reason;
    }
  };
  const auto* frmt_field = dybm.field(af::tag4("Frmt"));
  std::int64_t format_id = -1;
  if (frmt_field != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&frmt_field->value)) {
      format_id = e->id;
    }
  }
  const RasterFormat kind = classify_format(format_id);
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::fprintf(stderr, "[af] raster '%s': format %lld %s %lldx%lld\n", layer_name.c_str(),
                 static_cast<long long>(format_id),
                 kind == RasterFormat::Unsupported ? "(unsupported)" : "",
                 static_cast<long long>(dybm.int_field(af::tag4("BmpW"), 0)),
                 static_cast<long long>(dybm.int_field(af::tag4("BmpH"), 0)));
  }
  if (kind == RasterFormat::Unsupported) {
    return std::nullopt;
  }
  const std::int64_t width = dybm.int_field(af::tag4("BmpW"), 0);
  const std::int64_t height = dybm.int_field(af::tag4("BmpH"), 0);
  if (width <= 0 || height <= 0 || width > kMaxLayerSide || height > kMaxLayerSide) {
    return std::nullopt;
  }
  const bool is16 = kind == RasterFormat::RGBA16 || kind == RasterFormat::Gray16 ||
                    kind == RasterFormat::LabA16 || kind == RasterFormat::Mask16;
  const bool is_float = kind == RasterFormat::RGBAFloat;
  const std::size_t sample_bytes = is_float ? 4U : (is16 ? 2U : 1U);
  int channel_count = 4;
  switch (kind) {
    case RasterFormat::Gray8:
    case RasterFormat::Gray16: channel_count = 2; break;
    case RasterFormat::CMYKA8: channel_count = 5; break;
    case RasterFormat::Mask8:
    case RasterFormat::Mask16: channel_count = 1; break;
    default: break;
  }

  // Lazy placed-image DyBms (3.x saves of untouched placed images) store NO
  // base-plane fields at all - no TWi/THi tile grid, no Idx blocks, no Sta
  // codes on any channel - only the Bckg original plus a mip pyramid that
  // starts several levels down. The base is implicitly "every tile code 5":
  // size each plane's layout from the derived tile grid and fill it entirely
  // from the decoded original (the esdreika wild file holds 33 of these,
  // which previously imported as empty placeholders).
  const bool base_planes_absent = [&] {
    if (dybm.field(af::tag4("Bckg")) == nullptr) {
      return false;
    }
    for (int channel = 1; channel <= channel_count; ++channel) {
      const PlaneTags tags = base_plane_tags(channel);
      if (dybm.field(tags.status) != nullptr || dybm.field(tags.tiles_w) != nullptr ||
          dybm.field(tags.tiles_h) != nullptr || dybm.field(tags.index) != nullptr) {
        return false;
      }
    }
    return true;
  }();

  // First pass without a code-5 source; when some plane needs the placed
  // original, build per-channel sources (original image, else mip fallback)
  // and decode those planes again.
  std::vector<ChannelPlane> planes(static_cast<std::size_t>(channel_count));
  bool needs_original = false;
  if (base_planes_absent) {
    needs_original = true;
    const std::size_t tiles_w =
        (static_cast<std::size_t>(width) * sample_bytes + kTileSize - 1) / kTileSize;
    const std::size_t tiles_h = (static_cast<std::size_t>(height) + kTileSize - 1) / kTileSize;
    for (auto& plane : planes) {
      plane.width_bytes = tiles_w * kTileSize;
      plane.rows = tiles_h * kTileSize;
      plane.bytes.assign(plane.width_bytes * plane.rows, 0);
    }
  }
  for (int channel = 1; !base_planes_absent && channel <= channel_count; ++channel) {
    const PlaneStatus status = decode_channel_plane(
        bytes, container, dybm, base_plane_tags(channel), sample_bytes, is_float, width, height,
        nullptr, planes[static_cast<std::size_t>(channel - 1)]);
    if (status == PlaneStatus::NeedsOriginal) {
      needs_original = true;
      continue;
    }
    if (status != PlaneStatus::Ok) {
      set_reason(status == PlaneStatus::UnknownCode ? "uses an unknown tile encoding"
                                                    : "has an invalid tile layout");
      return std::nullopt;
    }
  }
  std::shared_ptr<const std::vector<std::uint8_t>> pristine_original_bytes;
  std::string pristine_original_name;
  if (needs_original) {
    std::vector<ChannelPlane> sources;
    bool have_sources = false;
    const bool rgba_kind = kind == RasterFormat::RGBA8 || kind == RasterFormat::RGBA16 ||
                           kind == RasterFormat::RGBAFloat;
    if (rgba_kind) {
      const auto original = decode_embedded_original(bytes, container, dybm);
      if (original && environment_variable_is_set("PATCHY_AF_TRACE") &&
          (original->pixels.width() != static_cast<std::int32_t>(width) ||
           original->pixels.height() != static_cast<std::int32_t>(height))) {
        std::fprintf(stderr, "[af] placed original: decoded %dx%d but DyBm is %lldx%lld\n",
                     original->pixels.width(), original->pixels.height(),
                     static_cast<long long>(width), static_cast<long long>(height));
      }
      if (original && original->pixels.width() == static_cast<std::int32_t>(width) &&
          original->pixels.height() == static_cast<std::int32_t>(height)) {
        for (int channel = 1; channel <= channel_count; ++channel) {
          sources.push_back(source_plane_from_original(
              original->pixels, planes[static_cast<std::size_t>(channel - 1)], channel,
              sample_bytes, is_float));
        }
        have_sources = true;
        // Pristine when no base tile was hand-painted (no code-4 anywhere):
        // the layer IS the placed original, eligible for a smart-object wrap.
        bool any_stored = false;
        for (int channel = 1; channel <= channel_count && !any_stored; ++channel) {
          const auto* status = dybm.field(base_plane_tags(channel).status);
          const auto* codes = status != nullptr
                                  ? std::get_if<std::vector<std::int64_t>>(&status->value)
                                  : nullptr;
          if (codes != nullptr) {
            for (const auto code : *codes) {
              if (code == kTileStored) {
                any_stored = true;
                break;
              }
            }
          }
        }
        if (!any_stored) {
          pristine_original_bytes = original->bytes;
          pristine_original_name = original->name;
        }
      }
    }
    if (!have_sources) {
      sources.clear();
      if (mip_source_planes(bytes, container, dybm, channel_count, sample_bytes, is_float, width,
                            height, planes, sources)) {
        have_sources = true;
        if (notices != nullptr) {
          notices->push_back("Layer '" + layer_name +
                             "': its placed original image could not be decoded; imported at "
                             "half resolution from the stored preview");
        }
      }
    }
    if (!have_sources) {
      set_reason("references a placed original image that could not be decoded");
      return std::nullopt;
    }
    if (base_planes_absent) {
      // No Sta codes to walk: the whole base level comes from the source.
      planes = std::move(sources);
    } else {
      for (int channel = 1; channel <= channel_count; ++channel) {
        const PlaneStatus status = decode_channel_plane(
            bytes, container, dybm, base_plane_tags(channel), sample_bytes, is_float, width,
            height, &sources[static_cast<std::size_t>(channel - 1)],
            planes[static_cast<std::size_t>(channel - 1)]);
        if (status != PlaneStatus::Ok) {
          set_reason("has an invalid tile layout");
          return std::nullopt;
        }
      }
    }
  }
  for (const auto& plane : planes) {
    if (static_cast<std::size_t>(width) * sample_bytes > plane.width_bytes ||
        static_cast<std::size_t>(height) > plane.rows) {
      set_reason("has an invalid tile layout");
      return std::nullopt;
    }
  }

  const auto sample = [&](int ch, std::int32_t x, std::int32_t y) -> float {
    return sample_plane(planes[static_cast<std::size_t>(ch)], sample_bytes, is_float, x, y);
  };
  const auto to_byte = [](float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 255.0F)));
  };

  // New-format bitmaps can embed real ICC profile bytes (Prof -> ICCP with a
  // per-space child; CMYP is the CMYK space). When present, convert CMYK
  // through it (the PSD path's lcms2 transform; inputs there are INVERTED ink,
  // .af stores straight ink, so invert on the way in).
  std::optional<CmykToRgbTransform> cmyk_transform;
  if (kind == RasterFormat::CMYKA8) {
    if (const af::AfClass* profiles = dybm.child_class(af::tag4("Prof"))) {
      if (const af::AfClass* cmyk_profile = profiles->child_class(af::tag4("CMYP"))) {
        if (const auto* data_field = cmyk_profile->field(af::tag4("Data"))) {
          if (const auto* data = std::get_if<std::vector<std::uint8_t>>(&data_field->value)) {
            cmyk_transform =
                CmykToRgbTransform::from_icc_profile(std::span<const std::uint8_t>(*data));
          }
        }
      }
    }
  }

  DecodedBitmap decoded;
  decoded.original_bytes = std::move(pristine_original_bytes);
  decoded.original_name = std::move(pristine_original_name);
  if (kind == RasterFormat::Mask8 || kind == RasterFormat::Mask16) {
    PixelBuffer mask(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                     PixelFormat::gray8());
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
      auto row = mask.row(y);
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        row[static_cast<std::size_t>(x)] = to_byte(sample(0, x, y));
      }
    }
    decoded.mask = std::move(mask);
    return decoded;
  }

  PixelBuffer buffer(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                     PixelFormat::rgba8());
  if (kind == RasterFormat::LabA16) {
    // The wire is the ICC v4 Lab16 PCS encoding; feed lcms TYPE_Lab_16 rows.
    const auto lab_transform = LabToRgbTransform::create();
    if (!lab_transform) {
      set_reason("could not build the Lab color transform");
      return std::nullopt;
    }
    const auto raw_u16 = [&](int channel, std::int32_t x, std::int32_t y) {
      const ChannelPlane& plane = planes[static_cast<std::size_t>(channel)];
      const std::uint8_t* p = plane.bytes.data() +
                              static_cast<std::size_t>(y) * plane.width_bytes +
                              static_cast<std::size_t>(x) * 2U;
      return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8U));
    };
    std::vector<std::uint16_t> lab(static_cast<std::size_t>(width) * 3U);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * 3U);
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          lab[static_cast<std::size_t>(x) * 3U + static_cast<std::size_t>(channel)] =
              raw_u16(channel, x, y);
        }
      }
      lab_transform->convert(lab.data(), rgb.data(), static_cast<std::size_t>(width));
      auto row = buffer.row(y);
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
        std::memcpy(out, rgb.data() + static_cast<std::size_t>(x) * 3U, 3U);
        out[3] = to_byte(sample(3, x, y));
      }
    }
    decoded.rgba = std::move(buffer);
    return decoded;
  }
  if (cmyk_transform) {
    std::vector<std::uint8_t> inverted(static_cast<std::size_t>(width) * 4U);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * 3U);
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        for (int channel = 0; channel < 4; ++channel) {
          inverted[static_cast<std::size_t>(x) * 4U + static_cast<std::size_t>(channel)] =
              static_cast<std::uint8_t>(255 - to_byte(sample(channel, x, y)));
        }
      }
      cmyk_transform->convert(inverted.data(), rgb.data(), static_cast<std::size_t>(width));
      auto row = buffer.row(y);
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
        std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
        std::memcpy(out, rgb.data() + static_cast<std::size_t>(x) * 3U, 3U);
        out[3] = to_byte(sample(4, x, y));
      }
    }
    decoded.rgba = std::move(buffer);
    return decoded;
  }
  for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
    auto row = buffer.row(y);
    for (std::int32_t x = 0; x < static_cast<std::int32_t>(width); ++x) {
      std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
      switch (kind) {
        case RasterFormat::Gray8:
        case RasterFormat::Gray16: {
          const std::uint8_t g = to_byte(sample(0, x, y));
          out[0] = out[1] = out[2] = g;
          out[3] = to_byte(sample(1, x, y));
          break;
        }
        case RasterFormat::RGBAFloat:
          out[0] = linear_to_srgb8(sample(0, x, y));
          out[1] = linear_to_srgb8(sample(1, x, y));
          out[2] = linear_to_srgb8(sample(2, x, y));
          out[3] = to_byte(sample(3, x, y) * 255.0F);
          break;
        case RasterFormat::CMYKA8: {
          // No ICC bytes in the file: the naive ink mix (same fallback the PSD
          // reader uses for profile-less CMYK). Channels are straight ink.
          const float c = sample(0, x, y) / 255.0F;
          const float m = sample(1, x, y) / 255.0F;
          const float ye = sample(2, x, y) / 255.0F;
          const float k = sample(3, x, y) / 255.0F;
          out[0] = to_byte(255.0F * (1.0F - c) * (1.0F - k));
          out[1] = to_byte(255.0F * (1.0F - m) * (1.0F - k));
          out[2] = to_byte(255.0F * (1.0F - ye) * (1.0F - k));
          out[3] = to_byte(sample(4, x, y));
          break;
        }
        default:
          for (int c = 0; c < 4; ++c) {
            out[c] = to_byte(sample(c, x, y));
          }
          break;
      }
    }
  }
  decoded.rgba = std::move(buffer);
  decoded.approximate_color = kind == RasterFormat::CMYKA8;
  return decoded;
}

// Affinity blend wire enum (id, enum version) -> Patchy BlendMode. Layer Blnd
// and effect BlnM share ONE versioned enum space: a version-0 base table plus
// modes added later that REUSE ids under an enum-version bump. Pinned twice
// over: the fx-blend-sweep corpus renders (effects) and blend-sweep-v0.afphoto
// (a Photo 2.6 doc with one layer per blend-dropdown entry; the wire values
// pair 1:1 with the labels). The JS-facing BlendMode table (Pigment=1,
// Darken=2, Multiply=4, ...) is the VERSION-6 numbering only - the importer
// originally fed wire ids through it and misread every non-Normal layer
// written with version-0 ids (2.x documents, and 3.x PSD conversions like the
// deko corpus doc, whose Screen layer imported as Darken until this table).
// Unmapped modes return nullopt (caller uses Normal + a notice); Passthrough
// is a group concept, handled apart.
[[nodiscard]] std::optional<BlendMode> map_blend_mode(const af::AfEnum& value) {
  if (value.version == 0) {
    switch (value.id) {
      case 0: return BlendMode::Normal;
      case 1: return BlendMode::Darken;
      case 2: return BlendMode::Multiply;
      case 3: return BlendMode::ColorBurn;
      case 4: return BlendMode::Lighten;
      case 5: return BlendMode::Screen;
      case 6: return BlendMode::ColorDodge;
      case 7: return BlendMode::LinearDodge;  // "Add"
      case 8: return BlendMode::Overlay;
      case 9: return BlendMode::SoftLight;
      case 10: return BlendMode::HardLight;
      case 11: return BlendMode::VividLight;
      case 12: return BlendMode::PinLight;
      case 13: return BlendMode::HardMix;
      case 14: return BlendMode::Difference;
      case 15: return BlendMode::Exclusion;
      case 16: return BlendMode::Subtract;
      case 17: return BlendMode::Hue;
      case 18: return BlendMode::Saturation;
      case 19: return BlendMode::Luminosity;
      case 20: return BlendMode::Color;
      // 21 Average, 22 Negation, 23 Reflect, 24 Glow, 25 Erase: no exact
      // Patchy equivalent (approximate_blend_mode has the best-fit remaps).
      // The original FINDINGS pairing recorded 23 as Glow with Reflect at
      // 24/v2; the 2026-07-29 JS probe (BlendMode.Reflect -> wire 23/v0,
      // BlendMode.Glow -> 24/v0) and the dropdown order of the
      // blend-sweep-v0 layers pin the correct assignment.
      default: return std::nullopt;
    }
  }
  if (value.version == 1 && value.id == 2) {
    return BlendMode::DarkerColor;
  }
  if (value.version == 1 && value.id == 6) {
    return BlendMode::LighterColor;
  }
  if (value.version == 1 && value.id == 15) {
    return BlendMode::LinearLight;
  }
  if (value.version == 3 && value.id == 5) {
    return BlendMode::LinearBurn;
  }
  if (value.version == 4 && value.id == 21) {
    return BlendMode::Divide;
  }
  // 28/v2 Contrast Negate has no Patchy equivalent (and no adopted best fit).
  if (value.version >= 6) {
    // Version 6 renumbers the space to match the JS-facing BlendMode table
    // (Pigment inserted at 1, the darken/lighten groups reordered); seen on
    // layers painted with the unified app's new modes (CyanisePixel's Pigment
    // strokes are 1/v6).
    switch (value.id) {
      case 0: return BlendMode::Normal;
      // 1 Pigment: no Patchy equivalent.
      case 2: return BlendMode::Darken;
      case 3: return BlendMode::DarkerColor;
      case 4: return BlendMode::Multiply;
      case 5: return BlendMode::ColorBurn;
      case 6: return BlendMode::LinearBurn;
      case 7: return BlendMode::Lighten;
      case 8: return BlendMode::LighterColor;
      case 9: return BlendMode::Screen;
      case 10: return BlendMode::ColorDodge;
      case 11: return BlendMode::LinearDodge;  // "Add"
      case 12: return BlendMode::Overlay;
      case 13: return BlendMode::SoftLight;
      case 14: return BlendMode::HardLight;
      case 15: return BlendMode::VividLight;
      case 16: return BlendMode::PinLight;
      case 17: return BlendMode::LinearLight;
      case 18: return BlendMode::HardMix;
      case 19: return BlendMode::Difference;
      case 20: return BlendMode::Exclusion;
      case 21: return BlendMode::Subtract;
      case 22: return BlendMode::Divide;
      case 23: return BlendMode::Hue;
      case 24: return BlendMode::Saturation;
      case 25: return BlendMode::Luminosity;
      case 26: return BlendMode::Color;
      // 27 Average, 28 Negation, 29 Reflect, 30 Glow, 31 ContrastInvert,
      // 32 Erase: no Patchy equivalent.
      default: return std::nullopt;
    }
  }
  return std::nullopt;
}

// Affinity's Erase blend mode: an alpha-removal operator, not a color blend.
// Wire (25, v0) and (32, v6+). There is no BlendMode counterpart and there
// never will be (every import must stay PSD-savable), so fold_erase_layers
// rewrites an Erase carrier into an inverse-alpha MASK on a new isolated
// group holding the sibling layers beneath it.
[[nodiscard]] bool is_erase_blend(const af::AfEnum& value) {
  return (value.version == 0 && value.id == 25) || (value.version >= 6 && value.id == 32);
}

// Best-fit approximations for the Affinity-only blend modes onto modes Patchy
// (and PSD) already store - never new BlendMode values. Chosen by RMSE over a
// full-gamut probe (af-spike blend_probes, 2026-07-29: every (s,d) byte pair
// once in the red channel, scored against Affinity's own render; the harness
// reproduces Normal at 0.0 and Exclusion at 0.3):
//   Average  = (s+d)/2 exactly -> Normal with opacity folded x0.5
//              (algebraically identical; plain Normal scores 54)
//   Negation = 1-|1-s-d| exactly -> Exclusion (60 vs Normal's 109)
//   Reflect  = d^2/(1-s) exactly -> Overlay (37 vs 101)
//   Glow     = s^2/(1-d) exactly -> Linear Light (24 vs 51)
//   Pigment  (no classical formula fits) -> Overlay (45 vs 109)
// ContrastNegate/ContrastInvert stays Normal + notice: its best candidate
// (Exclusion) still scores 84 of Normal's 128 - too wrong to render as if
// right. Erase stays Normal + notice: it is an alpha-removal operator, not a
// color blend (the compositor has no such primitive, and PSD could not store
// it either).
struct BlendApproximation {
  BlendMode mode;
  float opacity_scale;        // folded into the carrier's opacity (Average)
  const char* affinity_name;  // both names ride the notice
  const char* patchy_name;
};

[[nodiscard]] std::optional<BlendApproximation> approximate_blend_mode(const af::AfEnum& value) {
  const bool v6 = value.version >= 6;  // the renumbered JS-facing space
  if ((value.version == 0 && value.id == 21) || (v6 && value.id == 27)) {
    return BlendApproximation{BlendMode::Normal, 0.5F, "Average", "Normal at half opacity"};
  }
  if ((value.version == 0 && value.id == 22) || (v6 && value.id == 28)) {
    return BlendApproximation{BlendMode::Exclusion, 1.0F, "Negation", "Exclusion"};
  }
  if ((value.version == 0 && value.id == 23) || (v6 && value.id == 29)) {
    return BlendApproximation{BlendMode::Overlay, 1.0F, "Reflect", "Overlay"};
  }
  if ((value.version == 0 && value.id == 24) || (v6 && value.id == 30)) {
    return BlendApproximation{BlendMode::LinearLight, 1.0F, "Glow", "Linear Light"};
  }
  if (v6 && value.id == 1) {
    return BlendApproximation{BlendMode::Overlay, 1.0F, "Pigment", "Overlay"};
  }
  return std::nullopt;
}

struct LayerBuildContext {
  std::span<const std::uint8_t> bytes;
  const Container& container;
  Document& document;
  std::vector<std::string>& notices;
  int embed_depth{0};
  int layer_count{0};
  // The doc.dat grammar version: the 1.x era writes 3..9, the 2.x/3.x
  // generation 20+. Embedded-document anchoring differs between them.
  std::uint32_t document_version{0};
  // The ancestor transform in wire order [a, b, tx, c, d, ty]: children of a
  // transformed container (artboards, vector shapes, groups) live in their
  // parent's LOCAL space, so the parent affine composes onto every descendant
  // (ns-logo's artboard grid and arkanis-discord's scaled template rects are
  // the wild pins). Raster-base clipped children keep uncomposed coordinates
  // (the corpus places them in spread space).
  std::array<double, 6> transform{1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
  static constexpr int kMaxLayers = 20000;   // runaway guard
  static constexpr int kMaxEmbedDepth = 3;   // embedded-document recursion cap
};

[[nodiscard]] bool is_identity_transform(const std::array<double, 6>& m) {
  return m[0] == 1.0 && m[1] == 0.0 && m[2] == 0.0 && m[3] == 0.0 && m[4] == 1.0 && m[5] == 0.0;
}

// parent-then-child composition in the wire layout [a, b, tx, c, d, ty]
// (dest = [a b; c d] * src + [tx, ty]).
[[nodiscard]] std::array<double, 6> compose_transforms(const std::array<double, 6>& parent,
                                                       const std::array<double, 6>& child) {
  return {parent[0] * child[0] + parent[1] * child[3],
          parent[0] * child[1] + parent[1] * child[4],
          parent[0] * child[2] + parent[1] * child[5] + parent[2],
          parent[3] * child[0] + parent[4] * child[3],
          parent[3] * child[1] + parent[4] * child[4],
          parent[3] * child[2] + parent[4] * child[5] + parent[5]};
}

// A node's effective transform: its wire Xfrm composed under the ancestor
// transform. Empty when both are identity (callers treat empty as identity,
// keeping the historical fast paths byte-identical).
[[nodiscard]] std::vector<double> node_xfrm(const LayerBuildContext& ctx, const af::AfClass& node) {
  auto xfrm = node.vec_field(af::tag4("Xfrm"));
  if (is_identity_transform(ctx.transform)) {
    return xfrm;
  }
  std::array<double, 6> child{1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
  if (xfrm.size() == 6) {
    std::copy(xfrm.begin(), xfrm.end(), child.begin());
  }
  const auto composed = compose_transforms(ctx.transform, child);
  return std::vector<double>(composed.begin(), composed.end());
}

// Adjustment and live-filter node families (JSLib: *AdjustmentRasterNode /
// *FilterRasterNode). Their Bitm is the adjustment's mask plane, not content.
[[nodiscard]] bool is_adjustment_or_filter(std::uint32_t type_tag) {
  // Adjustments follow the *RA suffix convention (CrRA curves, HsRA HSL, ...;
  // ancestor chain *RA/AdjR). Live filters all share the ONE concrete tag FlRN
  // (FilterRasterNode; the filter kind rides in its Filt child class - pinned
  // by tiny-live-filter.af's Gaussian Blur). A mask-format bitmap on any other
  // unknown node lands in the same honest placeholder path anyway.
  const std::uint32_t suffix = type_tag & 0xFFFFU;
  return suffix == (static_cast<std::uint32_t>('R') << 8U | 'A') ||
         type_tag == af::tag4("FlRN");
}

// Read the [tx, ty] integer origin for a layer from a pure-translation
// effective transform (node_xfrm); untransformed nodes sit at the document
// origin. Returns nullopt for a non-trivial affine (scale/rotate), which
// rasterizes through resample_affine. BitI is deliberately NOT a placement
// source: it is the bitmap's used/dirty sub-rect (akiko's full-canvas photo
// carries BitI y0=635 yet belongs at 0,0; every corpus doc that placed
// correctly did so through Xfrm or at origin).
[[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> layer_origin(
    const std::vector<double>& xfrm) {
  if (xfrm.size() == 6) {
    const double a = xfrm[0];
    const double b = xfrm[1];
    const double c = xfrm[3];
    const double d = xfrm[4];
    if (std::abs(a - 1.0) < 1e-6 && std::abs(d - 1.0) < 1e-6 && std::abs(b) < 1e-6 &&
        std::abs(c) < 1e-6) {
      return std::pair<std::int32_t, std::int32_t>{static_cast<std::int32_t>(std::lround(xfrm[2])),
                                                   static_cast<std::int32_t>(std::lround(xfrm[5]))};
    }
    return std::nullopt;
  }
  return std::pair<std::int32_t, std::int32_t>{0, 0};
}

struct PlacedRaster {
  PixelBuffer pixels;
  std::int32_t x{0};
  std::int32_t y{0};
};

// Halve `source` along one axis with a box filter: RGBA averages alpha-weighted
// (transparent pixels contribute no color), gray masks average plainly. An odd
// trailing row/column keeps its single pixel. Deterministic double math.
[[nodiscard]] PixelBuffer box_halve_axis(const PixelBuffer& source, bool horizontal, bool gray) {
  const std::int32_t out_w = horizontal ? (source.width() + 1) / 2 : source.width();
  const std::int32_t out_h = horizontal ? source.height() : (source.height() + 1) / 2;
  PixelBuffer out(out_w, out_h, gray ? PixelFormat::gray8() : PixelFormat::rgba8());
  for (std::int32_t y = 0; y < out_h; ++y) {
    auto row = out.row(y);
    for (std::int32_t x = 0; x < out_w; ++x) {
      const std::int32_t x0 = horizontal ? x * 2 : x;
      const std::int32_t y0 = horizontal ? y : y * 2;
      const std::int32_t x1 = horizontal ? std::min(x0 + 1, source.width() - 1) : x0;
      const std::int32_t y1 = horizontal ? y0 : std::min(y0 + 1, source.height() - 1);
      const std::uint8_t* p0 = source.pixel(x0, y0);
      const std::uint8_t* p1 = source.pixel(x1, y1);
      if (gray) {
        row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(
            std::lround((static_cast<double>(p0[0]) + static_cast<double>(p1[0])) / 2.0));
        continue;
      }
      const double a0 = static_cast<double>(p0[3]);
      const double a1 = static_cast<double>(p1[3]);
      std::uint8_t* dst = row.data() + static_cast<std::size_t>(x) * 4U;
      dst[3] = static_cast<std::uint8_t>(std::lround((a0 + a1) / 2.0));
      if (a0 + a1 > 0.0) {
        for (int channel = 0; channel < 3; ++channel) {
          dst[channel] = static_cast<std::uint8_t>(std::lround(
              (static_cast<double>(p0[channel]) * a0 + static_cast<double>(p1[channel]) * a1) /
              (a0 + a1)));
        }
      } else {
        dst[0] = dst[1] = dst[2] = 0;
      }
    }
  }
  return out;
}

[[nodiscard]] PixelBuffer box_reduce(const PixelBuffer& source, int halvings_x, int halvings_y,
                                     bool gray) {
  PixelBuffer reduced = box_halve_axis(source, halvings_x > 0, gray);
  if (halvings_x > 0) {
    --halvings_x;
  } else {
    --halvings_y;
  }
  while (halvings_x > 0) {
    reduced = box_halve_axis(reduced, true, gray);
    --halvings_x;
  }
  while (halvings_y > 0) {
    reduced = box_halve_axis(reduced, false, gray);
    --halvings_y;
  }
  return reduced;
}

// Rasterize `source` through the affine Xfrm [a, b, tx, c, d, ty]
// (dest = [a b; c d] * src + [tx, ty]; the convention and the bilinear
// premultiplied-accumulation edges are pinned against Affinity's own PNG
// export of a rotated+scaled raster, RMSE 0.01) into an axis-aligned buffer
// at the returned origin. `gray_mask` sources sample edge-clamped single-byte
// planes (layer masks). Returns nullopt for degenerate or implausible results.
[[nodiscard]] std::optional<PlacedRaster> resample_affine(const PixelBuffer& source,
                                                          const std::vector<double>& xfrm,
                                                          bool gray_mask) {
  if (xfrm.size() != 6 || source.width() <= 0 || source.height() <= 0) {
    return std::nullopt;
  }
  const double a = xfrm[0];
  const double b = xfrm[1];
  const double tx = xfrm[2];
  const double c = xfrm[3];
  const double d = xfrm[4];
  const double ty = xfrm[5];
  const double det = a * d - b * c;
  if (!std::isfinite(det) || std::abs(det) < 1e-9) {
    return std::nullopt;
  }
  const auto source_w = static_cast<double>(source.width());
  const auto source_h = static_cast<double>(source.height());
  double min_x = tx;
  double max_x = tx;
  double min_y = ty;
  double max_y = ty;
  const std::array<std::pair<double, double>, 3> corners = {
      std::pair<double, double>{source_w, 0.0}, {0.0, source_h}, {source_w, source_h}};
  for (const auto& [sx, sy] : corners) {
    const double px = a * sx + b * sy + tx;
    const double py = c * sx + d * sy + ty;
    min_x = std::min(min_x, px);
    max_x = std::max(max_x, px);
    min_y = std::min(min_y, py);
    max_y = std::max(max_y, py);
  }
  if (!std::isfinite(min_x) || !std::isfinite(max_x) || !std::isfinite(min_y) ||
      !std::isfinite(max_y)) {
    return std::nullopt;
  }
  const double origin_x = std::floor(min_x);
  const double origin_y = std::floor(min_y);
  const double extent_w = std::ceil(max_x) - origin_x;
  const double extent_h = std::ceil(max_y) - origin_y;
  if (extent_w < 1.0 || extent_h < 1.0 || extent_w > kMaxLayerSide || extent_h > kMaxLayerSide ||
      extent_w * extent_h > 268435456.0) {  // cap the buffer at 1 GiB of RGBA
    return std::nullopt;
  }
  const auto out_w = static_cast<std::int32_t>(extent_w);
  const auto out_h = static_cast<std::int32_t>(extent_h);
  const double inv_a = d / det;
  const double inv_b = -b / det;
  const double inv_c = -c / det;
  const double inv_d = a / det;

  // Minification quality: bilinear alone reads a 2x2 neighborhood, so a
  // strongly downscaled placement (esdreika places 4096px texture originals at
  // 1/32 scale) shimmers with aliasing that Affinity's own mip-filtered render
  // does not show. Box-reduce the source by powers of two per axis until the
  // remaining scale is above one half, then bilinear as before. Alpha-weighted
  // averaging keeps transparent padding from bleeding dark fringes in.
  const double scale_x = std::hypot(a, c);
  const double scale_y = std::hypot(b, d);
  int halvings_x = 0;
  int halvings_y = 0;
  while (scale_x * static_cast<double>(1 << (halvings_x + 1)) <= 1.0 &&
         (source.width() >> (halvings_x + 1)) >= 1) {
    ++halvings_x;
  }
  while (scale_y * static_cast<double>(1 << (halvings_y + 1)) <= 1.0 &&
         (source.height() >> (halvings_y + 1)) >= 1) {
    ++halvings_y;
  }
  PixelBuffer reduced_storage;
  const PixelBuffer* sampled = &source;
  if (halvings_x > 0 || halvings_y > 0) {
    reduced_storage = box_reduce(source, halvings_x, halvings_y, gray_mask);
    sampled = &reduced_storage;
  }
  const double factor_x = static_cast<double>(1 << halvings_x);
  const double factor_y = static_cast<double>(1 << halvings_y);

  PlacedRaster placed;
  placed.x = static_cast<std::int32_t>(origin_x);
  placed.y = static_cast<std::int32_t>(origin_y);
  placed.pixels = PixelBuffer(out_w, out_h, gray_mask ? PixelFormat::gray8() : PixelFormat::rgba8());
  for (std::int32_t y = 0; y < out_h; ++y) {
    auto row = placed.pixels.row(y);
    for (std::int32_t x = 0; x < out_w; ++x) {
      const double rel_x = origin_x + x + 0.5 - tx;
      const double rel_y = origin_y + y + 0.5 - ty;
      const double sx = (inv_a * rel_x + inv_b * rel_y) / factor_x - 0.5;
      const double sy = (inv_c * rel_x + inv_d * rel_y) / factor_y - 0.5;
      const double fx0 = std::floor(sx);
      const double fy0 = std::floor(sy);
      const auto x0 = static_cast<std::int32_t>(fx0);
      const auto y0 = static_cast<std::int32_t>(fy0);
      const double fx = sx - fx0;
      const double fy = sy - fy0;
      const std::array<std::tuple<std::int32_t, std::int32_t, double>, 4> taps = {
          std::tuple<std::int32_t, std::int32_t, double>{x0, y0, (1.0 - fx) * (1.0 - fy)},
          {x0 + 1, y0, fx * (1.0 - fy)},
          {x0, y0 + 1, (1.0 - fx) * fy},
          {x0 + 1, y0 + 1, fx * fy}};
      if (gray_mask) {
        double accumulated = 0.0;
        for (const auto& [nx, ny, weight] : taps) {
          const std::int32_t cx = std::clamp(nx, 0, sampled->width() - 1);
          const std::int32_t cy = std::clamp(ny, 0, sampled->height() - 1);
          accumulated += static_cast<double>(sampled->pixel(cx, cy)[0]) * weight;
        }
        row[static_cast<std::size_t>(x)] =
            static_cast<std::uint8_t>(std::lround(std::clamp(accumulated, 0.0, 255.0)));
        continue;
      }
      double acc_r = 0.0;
      double acc_g = 0.0;
      double acc_b = 0.0;
      double acc_a = 0.0;
      for (const auto& [nx, ny, weight] : taps) {
        if (nx < 0 || ny < 0 || nx >= sampled->width() || ny >= sampled->height()) {
          continue;  // outside the source: transparent
        }
        const std::uint8_t* p = sampled->pixel(nx, ny);
        const double alpha_weight = static_cast<double>(p[3]) / 255.0 * weight;
        acc_r += static_cast<double>(p[0]) * alpha_weight;
        acc_g += static_cast<double>(p[1]) * alpha_weight;
        acc_b += static_cast<double>(p[2]) * alpha_weight;
        acc_a += static_cast<double>(p[3]) * weight;
      }
      std::uint8_t* out = row.data() + static_cast<std::size_t>(x) * 4U;
      if (acc_a > 0.5) {
        const double scale = 255.0 / acc_a;
        out[0] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(acc_r * scale), 0, 255));
        out[1] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(acc_g * scale), 0, 255));
        out[2] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(acc_b * scale), 0, 255));
        out[3] = static_cast<std::uint8_t>(std::lround(std::min(255.0, acc_a)));
      } else {
        out[0] = out[1] = out[2] = out[3] = 0;
      }
    }
  }
  return placed;
}

// Whether build_layers may fold Erase-blend carriers into group masks at this
// nesting level. Disabled inside clipping runs: a wrapper group emitted into a
// clipped-sibling list would silently escape its base's matte.
enum class EraseFolding : std::uint8_t { Enabled, Disabled };

void build_layers(LayerBuildContext& ctx, const std::vector<std::shared_ptr<af::AfClass>>& children,
                  std::vector<Layer>& out, EraseFolding folding = EraseFolding::Enabled);
[[nodiscard]] Document read_container(std::span<const std::uint8_t> bytes,
                                      std::vector<std::string>& notices, int embed_depth);

[[nodiscard]] const std::vector<std::shared_ptr<af::AfClass>>* class_list(const af::AfClass& node,
                                                                          std::uint32_t tag) {
  const auto* field = node.field(tag);
  if (field == nullptr) {
    return nullptr;
  }
  return std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&field->value);
}

// Defined with the shape-path builders below (it needs vector_path_from_node
// and parametric_shape_path).
void append_vector_mask_paths(const af::AfClass& node, const std::array<double, 6>& base,
                              int depth, VectorPath& into, std::int32_t& group, int& appended,
                              int& unsupported);

// Attach the node's mask (an M8/M16 raster in the AdCh "enclosure" list, or a
// vector layer subtree acting as the owner's clip mask - the latter becomes a
// native LayerVectorMask) to the built layer. Extra or undecodable masks
// degrade to a notice.
void apply_mask_children(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                         const std::string& name) {
  const auto* adjuncts = class_list(node, af::tag4("AdCh"));
  if (adjuncts == nullptr) {
    return;
  }
  bool applied = false;
  bool vector_applied = false;
  for (const auto& adjunct : *adjuncts) {
    if (adjunct == nullptr) {
      continue;
    }
    if (is_adjustment_or_filter(adjunct->type_tag)) {
      // An adjustment/live-filter riding the AdCh list is clipped TO the
      // owner, and its plane is the ADJUSTMENT's mask, never the owner's
      // (esdreika attaches Brightness/Contrast + HSL to a metal texture this
      // way; reading their all-opaque planes as owner masks dropped the
      // adjustments and raised a bogus duplicate-mask notice).
      // emit_adjunct_adjustments imports them after the owner layer.
      continue;
    }
    const af::AfClass* dybm = adjunct->child_class(af::tag4("Bitm"));
    if (dybm == nullptr) {
      // No raster plane: the adjunct may be a vector layer subtree (a Grup of
      // shapes, a PCrv, a ShpN, or a baked Comp) acting as the owner's vector
      // mask (the steam-logo wild construct). Same owner-space rule as raster
      // masks: the adjunct is a CHILD of its owner, so the owner's transform
      // composes before the subtree's own.
      auto base = ctx.transform;
      if (const auto owner = node.vec_field(af::tag4("Xfrm")); owner.size() == 6) {
        std::array<double, 6> own{};
        std::copy(owner.begin(), owner.end(), own.begin());
        base = compose_transforms(ctx.transform, own);
      }
      VectorPath path;
      std::int32_t group = 0;
      int contributors = 0;
      int unsupported = 0;
      append_vector_mask_paths(*adjunct, base, 0, path, group, contributors, unsupported);
      if (contributors == 0 && unsupported == 0) {
        continue;  // not vector geometry (an adjunct kind Patchy does not know)
      }
      if (unsupported > 0 || path.subpaths.empty()) {
        // A partially-decoded mask would clip wrongly; drop the whole mask
        // honestly instead.
        ctx.notices.push_back("Layer '" + name +
                              "': a vector mask could not be imported and was dropped");
        continue;
      }
      if (vector_applied) {
        ctx.notices.push_back("Layer '" + name +
                              "': has more than one vector mask; only the first was imported");
        continue;
      }
      LayerVectorMask mask;
      mask.path = std::move(path);
      layer.set_vector_mask(std::move(mask));
      update_vector_mask_raster(layer, Rect{0, 0, ctx.document.width(), ctx.document.height()});
      vector_applied = true;
      continue;
    }
    auto decoded = decode_bitmap(ctx.bytes, ctx.container, *dybm, name, &ctx.notices, nullptr);
    if (!decoded || decoded->mask.empty()) {
      ctx.notices.push_back("Layer '" + name + "': a mask could not be decoded and was dropped");
      continue;
    }
    if (applied) {
      ctx.notices.push_back("Layer '" + name +
                            "': has more than one mask; only the first was imported");
      break;
    }
    // The adjunct is a CHILD of its owner node, so its plane lives in the
    // owner's LOCAL space (the scene-graph rule): the owner's transform
    // composes before the adjunct's own. 2.x-generation masks store no
    // adjunct Xfrm at all (the plane anchors at the owner's origin - the
    // wild phone mockup's screen mask), while 3.x masks carry an
    // inverse-of-owner Xfrm that lands the plane in spread space
    // (restaurant-menu's image masks pin the pairing).
    const auto saved_transform = ctx.transform;
    if (const auto owner = node.vec_field(af::tag4("Xfrm")); owner.size() == 6) {
      std::array<double, 6> own{};
      std::copy(owner.begin(), owner.end(), own.begin());
      ctx.transform = compose_transforms(ctx.transform, own);
    }
    const auto mask_xfrm = node_xfrm(ctx, *adjunct);
    ctx.transform = saved_transform;
    const auto origin = layer_origin(mask_xfrm);
    std::int32_t mask_x = 0;
    std::int32_t mask_y = 0;
    if (origin) {
      mask_x = origin->first;
      mask_y = origin->second;
    } else {
      // Scale/rotate transform: rasterize the mask plane through the affine.
      auto placed = resample_affine(decoded->mask, mask_xfrm, true);
      if (placed) {
        decoded->mask = std::move(placed->pixels);
        mask_x = placed->x;
        mask_y = placed->y;
      } else {
        ctx.notices.push_back("Layer '" + name +
                              "': mask has a degenerate transform; its position is approximate");
        if (mask_xfrm.size() == 6) {
          mask_x = static_cast<std::int32_t>(std::lround(mask_xfrm[2]));
          mask_y = static_cast<std::int32_t>(std::lround(mask_xfrm[5]));
        }
      }
    }
    LayerMask mask;
    mask.bounds = Rect{mask_x, mask_y, decoded->mask.width(), decoded->mask.height()};
    mask.pixels = std::move(decoded->mask);
    mask.default_color = 255;  // beyond the stored plane the layer stays visible
    layer.set_mask(std::move(mask));
    applied = true;
  }
}

void apply_layer_effects(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                         const std::string& name);

[[nodiscard]] Layer build_group(LayerBuildContext& ctx, const af::AfClass& node,
                                const std::string& name) {
  Layer group(ctx.document.allocate_layer_id(), name.empty() ? "Group" : name, LayerKind::Group);
  group.set_visible(node.bool_field(af::tag4("Visi"), true));
  group.set_opacity(static_cast<float>(std::clamp(node.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0)));
  // Affinity groups blend as pass-through unless an explicit mode is set (the
  // Blnd field is absent for the default), which is exactly Patchy's semantics.
  group.set_blend_mode(BlendMode::PassThrough);
  const auto* blnd = node.field(af::tag4("Blnd"));
  if (blnd != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&blnd->value)) {
      if (const auto mapped = map_blend_mode(*e)) {
        group.set_blend_mode(*mapped);
      } else if (const auto approx = approximate_blend_mode(*e)) {
        group.set_blend_mode(approx->mode);
        group.set_opacity(group.opacity() * approx->opacity_scale);
        ctx.notices.push_back("Layer '" + group.name() + "': Affinity blend mode '" +
                              approx->affinity_name + "' approximated as " + approx->patchy_name);
      } else if (is_erase_blend(*e)) {
        // A Group carrier has no rasterized alpha at build time, so the fold
        // declines it (with a notice); do not keep pass-through meanwhile.
        group.set_blend_mode(BlendMode::Normal);
      } else {
        // An explicit-but-unmapped mode must not keep pass-through: that
        // renders the children as if the group did not exist at all.
        group.set_blend_mode(BlendMode::Normal);
        ctx.notices.push_back("Layer '" + group.name() +
                              "': blend mode not supported by Patchy; shown as Normal");
      }
    }
  }
  const auto* kids = class_list(node, af::tag4("Chld"));
  if (kids != nullptr) {
    // Children live in the group's local space (Affinity is a scene graph:
    // arkanis-discord's scaled containers hold children whose transforms
    // exactly invert the parent scale), so the group transform composes.
    const auto saved = ctx.transform;
    if (const auto xfrm = node.vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
      std::array<double, 6> child{};
      std::copy(xfrm.begin(), xfrm.end(), child.begin());
      ctx.transform = compose_transforms(ctx.transform, child);
    }
    std::vector<Layer> child_layers;
    build_layers(ctx, *kids, child_layers);
    ctx.transform = saved;
    // Affinity scopes a group-member adjustment to the group's own content
    // (the esdreika wild file: a -180 degree HSL shift inside the "Gravel"
    // group darkens only the gravel in Affinity's render); a pass-through
    // Patchy group would leak it over everything below the group. Isolate the
    // group (PS semantics scope adjustments to isolated groups) - only for
    // the pass-through default, an explicit wire blend stays authoritative.
    if (std::as_const(group).blend_mode() == BlendMode::PassThrough) {
      for (const auto& child : child_layers) {
        if (child.kind() == LayerKind::Adjustment && !child.clipped()) {
          group.set_blend_mode(BlendMode::Normal);
          break;
        }
      }
    }
    for (auto& child : child_layers) {
      group.add_child(std::move(child));
    }
  }
  apply_mask_children(ctx, node, group, name.empty() ? "Group" : name);
  apply_layer_effects(ctx, node, group, name.empty() ? "Group" : name);
  return group;
}

// A placed embedded document (EmbN): its Bitm is an EmbR that references the
// nested container stream (edc/<n>); import by recursing and flattening.
[[nodiscard]] std::optional<PixelBuffer> flatten_embedded(LayerBuildContext& ctx,
                                                          const af::AfClass& embr,
                                                          const std::string& name) {
  if (ctx.embed_depth >= LayerBuildContext::kMaxEmbedDepth) {
    ctx.notices.push_back("Layer '" + name + "': embedded documents nest too deeply; not rendered");
    return std::nullopt;
  }
  const af::AfClass* embc = embr.child_class(af::tag4("EmCn"));
  if (embc == nullptr) {
    return std::nullopt;
  }
  const auto* stream_field = embc->field(af::tag4("EmbC"));
  if (stream_field == nullptr) {
    return std::nullopt;
  }
  const auto* embedded_ref = std::get_if<af::AfEmbedded>(&stream_field->value);
  if (embedded_ref == nullptr || embedded_ref->data.empty()) {
    return std::nullopt;
  }
  const auto stream = ctx.container.streams.find(embedded_ref->data);
  if (stream == ctx.container.streams.end()) {
    return std::nullopt;
  }
  try {
    const auto nested_bytes = extract_stream(ctx.bytes, stream->second, embedded_ref->data, nullptr);
    std::span<const std::uint8_t> nested_span(nested_bytes);
    // edc/<n> streams wrap the nested container in an 8-byte "EmDc" tag + u32
    // prefix - in every generation observed (v9-era wild files AND current
    // corpus documents), so this flatten never succeeded before the unwrap
    // (2026-07-28 wild sweep). Tolerate a bare container anyway.
    if (nested_span.size() > 8 && nested_span[0] == 'E' && nested_span[1] == 'm' &&
        nested_span[2] == 'D' && nested_span[3] == 'c') {
      nested_span = nested_span.subspan(8);
    }
    std::vector<std::string> nested_notices;  // inner notices stay summarized
    const Document nested = read_container(nested_span, nested_notices, ctx.embed_depth + 1);
    ctx.notices.push_back("Layer '" + name + "': embedded document flattened on import");
    return flatten_document_rgba8(nested);
  } catch (const std::exception& error) {
    ctx.notices.push_back("Layer '" + name + "': embedded document could not be read (" +
                          error.what() + ")");
    return std::nullopt;
  }
}

void apply_common(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                  const std::string& name) {
  layer.set_visible(node.bool_field(af::tag4("Visi"), true));
  layer.set_opacity(static_cast<float>(std::clamp(node.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0)));
  layer.set_fill_opacity(
      static_cast<float>(std::clamp(node.double_field(af::tag4("FOpc"), 1.0), 0.0, 1.0)));
  const auto* blnd = node.field(af::tag4("Blnd"));
  if (blnd != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&blnd->value)) {
      const auto mapped = map_blend_mode(*e);
      if (mapped) {
        layer.set_blend_mode(*mapped);
      } else if (const auto approx = approximate_blend_mode(*e)) {
        layer.set_blend_mode(approx->mode);
        layer.set_opacity(layer.opacity() * approx->opacity_scale);
        ctx.notices.push_back("Layer '" + name + "': Affinity blend mode '" +
                              approx->affinity_name + "' approximated as " + approx->patchy_name);
      } else if (is_erase_blend(*e)) {
        // Carrier keeps Normal here; fold_erase_layers rewrites the sibling
        // list once the rasterized alpha exists and owns every Erase notice.
      } else {
        ctx.notices.push_back("Layer '" + name +
                              "': blend mode not supported by Patchy; shown as Normal");
      }
    }
  }
  apply_layer_effects(ctx, node, layer, name);
}

[[nodiscard]] std::optional<std::array<float, 4>> read_rgba_color(const af::AfClass* color_class);

// ---- Layer effects (the node's FiEf list of FilE-derived classes) ----
//
// Shared header on every effect: Enab (only enabled ones import), BlnM (an
// enum space of its own, DIFFERENT from the layer Blnd enum), Opac 0..1, SclO
// scale-with-object. Radii/offsets are document pixels; angles are radians.
// Colors are the same RGBA class shape as elsewhere (possibly shared-class
// refs; the tree parser resolves those). All mappings below are pinned by the
// authored one-toggle docs in local-test-fixtures/af-spike/corpus/fx-*.af
// (blend sweep, alignment/bevel-type sweeps, angle-direction renders).

// Effect-blend wire (id, enum_version) -> BlendMode. Effect BlnM uses the SAME
// versioned wire enum as the layer Blnd field (the blend-sweep-v0.afphoto
// pairing matches the fx-blend-sweep effect pinning value for value), so the
// shared table above does the lookup.
[[nodiscard]] std::optional<BlendMode> map_effect_blend_mode(const af::AfEnum& value) {
  return map_blend_mode(value);
}

[[nodiscard]] double degrees_from_radians(double radians) {
  double degrees = radians * 180.0 / 3.14159265358979323846;
  degrees = std::fmod(degrees, 360.0);
  return degrees < 0.0 ? degrees + 360.0 : degrees;
}

[[nodiscard]] std::int64_t enum_field_id(const af::AfClass& cls, std::uint32_t tag,
                                         std::int64_t fallback) {
  const auto* f = cls.field(tag);
  if (f != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&f->value)) {
      return e->id;
    }
  }
  return fallback;
}

// Effect color + alpha (alpha folds into the effect opacity).
struct EffectColor {
  RgbColor color{0, 0, 0};
  float alpha{1.0F};
};

[[nodiscard]] std::optional<EffectColor> effect_color(const af::AfClass& effect, std::uint32_t tag) {
  const auto stored = read_rgba_color(effect.child_class(tag));
  if (!stored.has_value()) {
    return std::nullopt;
  }
  const auto to_channel = [](float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
  };
  return EffectColor{RgbColor{to_channel((*stored)[0]), to_channel((*stored)[1]), to_channel((*stored)[2])},
                     std::clamp((*stored)[3], 0.0F, 1.0F)};
}

// GrFl -> FDsc -> FDeF (FilG/Fill) -> Grad: Posn = [position, midpoint] float2
// pairs and Cols = the RGBA stop colors. FilG Type (GradientFillType): 0
// linear, 1 elliptical, 2 radial, 3 conical. The BASE gradient runs along +x
// (left -> right; pinned by the default-overlay render), and the descriptor's
// optional FDeX [a,b,tx,c,d,ty] transform rotates/scales it: the direction
// vector is (a, c) in screen (y-down) space, so the PS light-convention angle
// is atan2(-c, a); hypot(a, c) is the span scale.
[[nodiscard]] std::optional<LayerStyleGradient> style_gradient_from_fill(const af::AfClass* descriptor) {
  if (descriptor == nullptr) {
    return std::nullopt;
  }
  const af::AfClass* fill = descriptor->child_class(af::tag4("FDeF"));
  if (fill == nullptr) {
    return std::nullopt;
  }
  const af::AfClass* gradient = fill->child_class(af::tag4("Grad"));
  if (gradient == nullptr) {
    return std::nullopt;
  }
  const auto positions = gradient->vec_field(af::tag4("Posn"));
  const auto* colors = class_list(*gradient, af::tag4("Cols"));
  if (colors == nullptr || positions.size() != colors->size() * 2 || colors->size() < 2) {
    return std::nullopt;
  }
  LayerStyleGradient result;
  switch (enum_field_id(*fill, af::tag4("Type"), 0)) {
    case 1:
    case 2: result.type = LayerStyleGradientType::Radial; break;
    case 3: result.type = LayerStyleGradientType::Angle; break;
    default: result.type = LayerStyleGradientType::Linear; break;
  }
  result.angle_degrees = 0.0F;  // the wire base direction is left -> right
  if (const auto xfrm = descriptor->vec_field(af::tag4("FDeX")); xfrm.size() == 6) {
    const double a = xfrm[0];
    const double c = xfrm[3];
    const double span = std::hypot(a, c);
    if (span > 0.01) {
      result.angle_degrees = static_cast<float>(degrees_from_radians(std::atan2(-c, a)));
      result.scale = static_cast<float>(std::clamp(span, 0.1, 1.5));
    }
  }
  result.color_stops.reserve(colors->size());
  result.alpha_stops.reserve(colors->size());
  for (std::size_t i = 0; i < colors->size(); ++i) {
    const auto stored = read_rgba_color((*colors)[i].get());
    if (!stored.has_value()) {
      return std::nullopt;
    }
    const auto to_channel = [](float value) {
      return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    const auto location = static_cast<float>(std::clamp(positions[i * 2], 0.0, 1.0));
    // The wire midpoint rides with the DEPARTING stop; Patchy stores it on the
    // destination stop, so stop i+1 takes stop i's midpoint.
    const auto midpoint =
        i == 0 ? 0.5F : static_cast<float>(std::clamp(positions[(i - 1) * 2 + 1], 0.05, 0.95));
    result.color_stops.push_back(GradientColorStop{
        location,
        RgbColor{to_channel((*stored)[0]), to_channel((*stored)[1]), to_channel((*stored)[2])},
        midpoint});
    result.alpha_stops.push_back(GradientAlphaStop{location, std::clamp((*stored)[3], 0.0F, 1.0F), midpoint});
  }
  return result;
}

[[nodiscard]] std::string effect_tag_text(std::uint32_t tag) {
  std::string text(4, '?');
  for (int i = 0; i < 4; ++i) {
    const auto byte = static_cast<char>((tag >> (24 - i * 8)) & 0xFFU);
    text[static_cast<std::size_t>(i)] = (byte >= 0x20 && byte < 0x7F) ? byte : '?';
  }
  return text;
}

// Map the node's FiEf effects onto layer_style(). Kinds Patchy has no model
// for (Gaussian blur, ...) skip with a notice; the Phong 3D bevel (PhgB)
// notice-approximates as a smooth inner Bevel/Emboss. Angle conventions and
// the enum ids below are pinned by the authored one-toggle fixture docs.
void apply_layer_effects(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                         const std::string& name) {
  const auto* effects = class_list(node, af::tag4("FiEf"));
  if (effects == nullptr || effects->empty()) {
    return;
  }
  auto& style = layer.layer_style();
  for (const auto& entry : *effects) {
    if (entry == nullptr || !entry->bool_field(af::tag4("Enab"), false)) {
      continue;
    }
    const af::AfClass& effect = *entry;
    float opacity =
        static_cast<float>(std::clamp(effect.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0));
    // Every effect kind reads blend() BEFORE opacity, so the Average
    // approximation can fold its half-opacity into the shared local.
    const auto blend = [&](BlendMode fallback) {
      const auto* field = effect.field(af::tag4("BlnM"));
      if (field != nullptr) {
        if (const auto* e = std::get_if<af::AfEnum>(&field->value)) {
          if (const auto mapped = map_effect_blend_mode(*e)) {
            return *mapped;
          }
          if (const auto approx = approximate_blend_mode(*e)) {
            opacity *= approx->opacity_scale;
            ctx.notices.push_back("Layer '" + name + "': effect blend mode '" +
                                  approx->affinity_name + "' approximated as " +
                                  approx->patchy_name);
            return approx->mode;
          }
          ctx.notices.push_back("Layer '" + name + "': effect blend mode approximated");
        }
      }
      return fallback;
    };
    const std::uint32_t kind = effect.type_tag;

    // Shadow Angl is the direction the shadow FALLS, screen-clockwise from +x
    // (0 = right, pi/2 = down; pinned by the fx-shadow-a0/a90 renders).
    // Patchy stores the Photoshop LIGHT angle (CCW from +x, y up): 180 - deg.
    const auto shadow_angle = [&] {
      return static_cast<float>(
          std::fmod(540.0 - degrees_from_radians(effect.double_field(af::tag4("Angl"), 0.0)),
                    360.0));
    };

    if (kind == af::tag4("Shad")) {  // outer shadow
      LayerDropShadow shadow;
      shadow.enabled = true;
      shadow.blend_mode = blend(BlendMode::Multiply);
      shadow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        shadow.color = color->color;
        shadow.opacity *= color->alpha;
      }
      shadow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      shadow.distance = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Offs"), 0.0)));
      shadow.angle_degrees = shadow_angle();
      if (!effect.bool_field(af::tag4("Knck"), true)) {
        ctx.notices.push_back("Layer '" + name +
                              "': shadow drawn behind the layer ('fill knocks out shadow' off is not supported)");
      }
      style.drop_shadows.push_back(shadow);
    } else if (kind == af::tag4("InnS")) {  // inner shadow
      LayerInnerShadow shadow;
      shadow.enabled = true;
      shadow.blend_mode = blend(BlendMode::Multiply);
      shadow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        shadow.color = color->color;
        shadow.opacity *= color->alpha;
      }
      shadow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      shadow.distance = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Offs"), 0.0)));
      shadow.angle_degrees = shadow_angle();
      style.inner_shadows.push_back(shadow);
    } else if (kind == af::tag4("Strk")) {  // outline
      LayerStroke stroke;
      stroke.enabled = true;
      stroke.blend_mode = blend(BlendMode::Normal);
      stroke.opacity = opacity;
      stroke.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 0.0)));
      // Wire Alig: 0 outside (the UI default), 1 centre, 2 inside (pinned by
      // the fx-outline-* alignment sweep).
      switch (enum_field_id(effect, af::tag4("Alig"), 0)) {
        case 1: stroke.position = LayerStrokePosition::Center; break;
        case 2: stroke.position = LayerStrokePosition::Inside; break;
        default: stroke.position = LayerStrokePosition::Outside; break;
      }
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        stroke.color = color->color;
        stroke.opacity *= color->alpha;
      }
      // Wire Ftyp (StrokeFillType): 0 solid, 1 contour, 2 gradient.
      const auto fill_type = enum_field_id(effect, af::tag4("Ftyp"), 0);
      if (fill_type == 2) {
        if (auto gradient = style_gradient_from_fill(effect.child_class(af::tag4("GrFl")))) {
          stroke.uses_gradient = true;
          stroke.gradient = std::move(*gradient);
        } else {
          ctx.notices.push_back("Layer '" + name + "': stroke effect fill approximated as solid");
        }
      } else if (fill_type != 0) {
        ctx.notices.push_back("Layer '" + name + "': stroke effect fill approximated as solid");
      }
      if (stroke.size <= 0.0F) {
        continue;  // zero-width outline draws nothing in Affinity either
      }
      style.strokes.push_back(stroke);
    } else if (kind == af::tag4("GrdO")) {  // gradient overlay
      LayerGradientFill overlay;
      overlay.enabled = true;
      overlay.blend_mode = blend(BlendMode::Normal);
      overlay.opacity = opacity;
      if (auto gradient = style_gradient_from_fill(effect.child_class(af::tag4("GrFl")))) {
        overlay.gradient = std::move(*gradient);
        style.gradient_fills.push_back(overlay);
      } else {
        ctx.notices.push_back("Layer '" + name + "': gradient overlay effect not decoded; skipped");
      }
    } else if (kind == af::tag4("ColO")) {  // colour overlay
      LayerColorOverlay overlay;
      overlay.enabled = true;
      overlay.blend_mode = blend(BlendMode::Normal);
      overlay.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        overlay.color = color->color;
        overlay.opacity *= color->alpha;
      }
      style.color_overlays.push_back(overlay);
    } else if (kind == af::tag4("OutG")) {  // outer glow
      LayerOuterGlow glow;
      glow.enabled = true;
      glow.blend_mode = blend(BlendMode::Screen);
      glow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        glow.color = color->color;
        glow.opacity *= color->alpha;
      }
      glow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      // Affinity glows are plain gaussian falloffs; the raw blur (Range 100)
      // is the closest match, not Photoshop's doubled UI default.
      glow.range = 100.0F;
      style.outer_glows.push_back(glow);
    } else if (kind == af::tag4("InnG")) {  // inner glow
      LayerInnerGlow glow;
      glow.enabled = true;
      glow.blend_mode = blend(BlendMode::Screen);
      glow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        glow.color = color->color;
        glow.opacity *= color->alpha;
      }
      glow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      glow.source = effect.bool_field(af::tag4("Cntr"), false) ? LayerInnerGlowSource::Center
                                                               : LayerInnerGlowSource::Edge;
      // Affinity glows are plain gaussian falloffs; the raw blur (Range 100)
      // is the closest match, not Photoshop's doubled UI default.
      glow.range = 100.0F;
      style.inner_glows.push_back(glow);
    } else if (kind == af::tag4("BevE")) {  // bevel/emboss
      LayerBevelEmboss bevel;
      bevel.enabled = true;
      bevel.highlight_blend_mode = blend(BlendMode::Screen);
      bevel.highlight_opacity = opacity;
      bevel.shadow_opacity =
          static_cast<float>(std::clamp(effect.double_field(af::tag4("ShOp"), 0.75), 0.0, 1.0));
      const auto* shadow_blend = effect.field(af::tag4("ShBM"));
      if (shadow_blend != nullptr) {
        if (const auto* e = std::get_if<af::AfEnum>(&shadow_blend->value)) {
          if (const auto mapped = map_effect_blend_mode(*e)) {
            bevel.shadow_blend_mode = *mapped;
          }
        }
      }
      if (const auto color = effect_color(effect, af::tag4("HiCl"))) {
        bevel.highlight_color = color->color;
        bevel.highlight_opacity *= color->alpha;
      }
      if (const auto color = effect_color(effect, af::tag4("ShCl"))) {
        bevel.shadow_color = color->color;
        bevel.shadow_opacity *= color->alpha;
      }
      bevel.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      bevel.soften = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Sftn"), 0.0)));
      // Wire Dept is the bump height in px (defaults 5.0 alongside Radi 5.0);
      // Photoshop depth is a percentage of the size, so the ratio maps it.
      bevel.depth = static_cast<float>(std::clamp(
          effect.double_field(af::tag4("Dept"), 5.0) /
              std::max(1.0, effect.double_field(af::tag4("Radi"), 5.0)),
          0.05, 10.0));
      // Azim/Elev are radians in the Photoshop light convention (135deg
      // default lights from the upper-left; pinned by the fx-bevel renders).
      bevel.angle_degrees =
          static_cast<float>(degrees_from_radians(effect.double_field(af::tag4("Azim"), 2.356194490192345)));
      bevel.altitude_degrees =
          static_cast<float>(std::clamp(degrees_from_radians(effect.double_field(af::tag4("Elev"), 0.7853981633974483)), 0.0, 90.0));
      bevel.direction_up = !effect.bool_field(af::tag4("Invt"), false);
      // Wire Beve: 0 inner, 1 outer, 2 emboss, 3 pillow (the JS enum 1:1).
      switch (enum_field_id(effect, af::tag4("Beve"), 0)) {
        case 1: bevel.style = BevelEmbossStyleKind::OuterBevel; break;
        case 2: bevel.style = BevelEmbossStyleKind::Emboss; break;
        case 3: bevel.style = BevelEmbossStyleKind::PillowEmboss; break;
        default: bevel.style = BevelEmbossStyleKind::InnerBevel; break;
      }
      style.bevels.push_back(bevel);
      ctx.notices.push_back("Layer '" + name +
                            "': bevel/emboss effect approximated (engine rendering differs)");
    } else if (kind == af::tag4("PhgB")) {  // 3D (Phong) bevel -> approximate
      LayerBevelEmboss bevel;
      bevel.enabled = true;
      bevel.highlight_blend_mode = BlendMode::Screen;
      bevel.shadow_blend_mode = BlendMode::Multiply;
      bevel.highlight_opacity = opacity;
      bevel.shadow_opacity = opacity * 0.75F;
      bevel.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      bevel.soften = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Sftn"), 0.0)));
      bevel.depth = 1.0F;
      // Lighting: the first PLig light's Alph (azimuth) / Beta (elevation).
      double azimuth = 0.7853981633974483;
      double elevation = 0.7853981633974483;
      if (const af::AfClass* lights = effect.child_class(af::tag4("Lits"))) {
        if (const auto* list = class_list(*lights, af::tag4("PLis"));
            list != nullptr && !list->empty() && list->front() != nullptr) {
          azimuth = list->front()->double_field(af::tag4("Alph"), azimuth);
          elevation = list->front()->double_field(af::tag4("Beta"), elevation);
        }
      }
      bevel.angle_degrees = static_cast<float>(degrees_from_radians(azimuth));
      bevel.altitude_degrees =
          static_cast<float>(std::clamp(degrees_from_radians(elevation), 0.0, 90.0));
      bevel.style = BevelEmbossStyleKind::InnerBevel;
      style.bevels.push_back(bevel);
      ctx.notices.push_back("Layer '" + name +
                            "': 3D effect approximated as Bevel/Emboss");
    } else if (kind == af::tag4("Gaus")) {
      // Gaussian blur layer effect: a content blur, not an lfx2-style
      // destination pass (PSD cannot store it), so it BAKES into the layer's
      // pixels once they are final - the importer stamps a pending marker
      // that bake_pending_blur_effects resolves at the end of the read. Wire:
      // Radi = the UI radius in document px 1:1 (fx-gaussian probe), PrAl =
      // preserve alpha.
      const double radius = std::max(0.0, effect.double_field(af::tag4("Radi"), 0.0));
      if (radius > 0.0) {
        layer.metadata()[kAfPendingBlurKey] =
            std::to_string(radius) + " " + std::to_string(opacity) + " " +
            (effect.bool_field(af::tag4("PrAl"), false) ? "1" : "0");
      }
    } else {
      ctx.notices.push_back("Layer '" + name + "': effect '" + effect_tag_text(kind) +
                            "' is not supported; skipped");
    }
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af] effect '%s' on '%s'\n", effect_tag_text(kind).c_str(),
                   name.c_str());
    }
  }
}

// Read a curves Spln class into Patchy control points. `Vals` carries the xs,
// then the ys, then per-point tangent data; `Cnt ` (trailing space) is the
// point count. Interpolation differs slightly (both are smooth splines through
// the points), which the tolerance-based fixtures accept.
[[nodiscard]] CurveControlPoints curve_points_from_spline(const af::AfClass* spline) {
  if (spline == nullptr) {
    return {};
  }
  const auto count = static_cast<std::size_t>(spline->int_field(af::tag4("Cnt "), 0));
  const auto values = spline->vec_field(af::tag4("Vals"));
  if (count < 2 || values.size() < count * 2) {
    return {};
  }
  CurveControlPoints points;
  points.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double x = std::clamp(values[i], 0.0, 1.0);
    const double y = std::clamp(values[count + i], 0.0, 1.0);
    points.push_back({static_cast<int>(std::lround(x * 255.0)),
                      static_cast<int>(std::lround(y * 255.0))});
  }
  return points;
}

[[nodiscard]] bool curve_points_are_identity(const CurveControlPoints& points) {
  return points.size() == 2 && points.front() == CurveControlPoint{0, 0} &&
         points.back() == CurveControlPoint{255, 255};
}

// Adjustment nodes: map the kinds Patchy models onto real adjustment layers
// (wire semantics pinned by the adjust-* corpus docs; FINDINGS.md records the
// field tables). Unknown kinds and live filters keep the placeholder path.
// The node's own Bitm is its MASK plane; an all-empty plane means no mask.
[[nodiscard]] std::optional<Layer> build_adjustment_layer(LayerBuildContext& ctx,
                                                          const af::AfClass& node,
                                                          const std::string& display) {
  const std::uint32_t tag = node.type_tag;
  const af::AfClass* params = node.child_class(af::tag4("AdjP"));
  const auto to_255 = [](double value) {
    return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
  };
  const auto to_percent = [](double value) {
    return static_cast<int>(std::lround(std::clamp(value, -1.0, 1.0) * 100.0));
  };

  AdjustmentSettings settings;
  bool approximate = false;
  if (tag == af::tag4("InRA")) {
    settings.kind = AdjustmentKind::Invert;
  } else if (params == nullptr) {
    return std::nullopt;
  } else if (tag == af::tag4("LeRA")) {
    settings.kind = AdjustmentKind::Levels;
    settings.levels.black_input = to_255(params->double_field(af::tag4("Blac"), 0.0));
    settings.levels.white_input = to_255(params->double_field(af::tag4("Whit"), 1.0));
    settings.levels.gamma_percent = std::clamp(
        static_cast<int>(std::lround(params->double_field(af::tag4("Gamm"), 1.0) * 100.0)), 10,
        1000);
    settings.levels.black_output = to_255(params->double_field(af::tag4("OutB"), 0.0));
    settings.levels.white_output = to_255(params->double_field(af::tag4("OutW"), 1.0));
    const auto blacks = params->vec_field(af::tag4("BlkC"));
    const auto whites = params->vec_field(af::tag4("WhtC"));
    const auto gammas = params->vec_field(af::tag4("GamC"));
    const auto out_blacks = params->vec_field(af::tag4("OBlC"));
    const auto out_whites = params->vec_field(af::tag4("OWhC"));
    LevelsRecord* records[3] = {&settings.levels.red, &settings.levels.green,
                               &settings.levels.blue};
    for (std::size_t channel = 0; channel < 3; ++channel) {
      LevelsRecord& record = *records[channel];
      if (blacks.size() > channel) {
        record.black_input = to_255(blacks[channel]);
      }
      if (whites.size() > channel) {
        record.white_input = to_255(whites[channel]);
      }
      if (gammas.size() > channel) {
        record.gamma_percent = std::clamp(
            static_cast<int>(std::lround(gammas[channel] * 100.0)), 10, 1000);
      }
      if (out_blacks.size() > channel) {
        record.black_output = to_255(out_blacks[channel]);
      }
      if (out_whites.size() > channel) {
        record.white_output = to_255(out_whites[channel]);
      }
    }
  } else if (tag == af::tag4("CrRA")) {
    settings.kind = AdjustmentKind::Curves;
    auto master = curve_points_from_spline(params->child_class(af::tag4("Mast")));
    if (master.size() < 2) {
      return std::nullopt;
    }
    settings.curves.rgb = std::move(master);
    CurveControlPoints* channels[3] = {&settings.curves.red, &settings.curves.green,
                                      &settings.curves.blue};
    const std::uint32_t spline_tags[3] = {af::tag4("C1Sp"), af::tag4("C2Sp"), af::tag4("C3Sp")};
    for (std::size_t channel = 0; channel < 3; ++channel) {
      auto points = curve_points_from_spline(params->child_class(spline_tags[channel]));
      if (points.size() >= 2 && !curve_points_are_identity(points)) {
        *channels[channel] = std::move(points);
      }
    }
  } else if (tag == af::tag4("BCRA")) {
    // Affinity's own brightness/contrast math differs from Patchy's
    // PS-legacy-calibrated formula; the value mapping is best-effort.
    settings.kind = AdjustmentKind::BrightnessContrast;
    settings.brightness_contrast.brightness =
        to_percent(params->double_field(af::tag4("Brig"), 0.0));
    settings.brightness_contrast.contrast =
        to_percent(params->double_field(af::tag4("Ctrs"), 1.0) - 1.0);
    approximate = true;
  } else if (tag == af::tag4("PoRA")) {
    settings.kind = AdjustmentKind::Posterize;
    settings.posterize.levels = std::clamp(
        static_cast<int>(params->int_field(af::tag4("Post"), 4)), 2, 255);
  } else if (tag == af::tag4("ThRA")) {
    settings.kind = AdjustmentKind::Threshold;
    settings.threshold.level =
        std::clamp(to_255(params->double_field(af::tag4("Thre"), 0.5)), 1, 255);
  } else if (tag == af::tag4("CBRA")) {
    // Patchy's Color Balance models one midtones triple; nonzero shadow or
    // highlight axes fold away (approximate). Affinity's full-scale effect is
    // roughly a tenth of Photoshop's -100..100 (pinned against the
    // adjust-colourbalance render at three probes), hence the 10x mapping.
    settings.kind = AdjustmentKind::ColorBalance;
    const auto to_balance = [](double value) {
      return static_cast<int>(std::lround(std::clamp(value, -1.0, 1.0) * 10.0));
    };
    settings.color_balance.cyan_red = to_balance(params->double_field(af::tag4("MiCR"), 0.0));
    settings.color_balance.magenta_green =
        to_balance(params->double_field(af::tag4("MiMG"), 0.0));
    settings.color_balance.yellow_blue =
        to_balance(params->double_field(af::tag4("MiYB"), 0.0));
    approximate = true;
    for (const char* axis : {"ShCR", "ShMG", "ShYB", "HiCR", "HiMG", "HiYB"}) {
      if (std::abs(params->double_field(tag_of(axis), 0.0)) > 0.005) {
        approximate = true;
      }
    }
  } else if (tag == af::tag4("HsRA")) {
    // HueA is in turns and maps 1:1 onto the visual shift (pinned against the
    // adjust-hsl render; the JS setter's sign is inverted relative to this,
    // which briefly suggested a negation - it is not one).
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation.hue_shift = std::clamp(
        static_cast<int>(std::lround(params->double_field(af::tag4("HueA"), 0.0) * 360.0)),
        -180, 180);
    settings.hue_saturation.saturation_delta =
        to_percent(params->double_field(af::tag4("SatA"), 0.0));
    settings.hue_saturation.lightness_delta =
        to_percent(params->double_field(af::tag4("LumA"), 0.0));
  } else {
    return std::nullopt;
  }

  // Unnamed adjustment nodes: Affinity's Layers panel labels these by their
  // adjustment kind, so a generic "Layer" would read as an empty layer here.
  const std::string label = node.string_field(af::tag4("Desc")).empty()
                                ? adjustment_display_name(settings.kind)
                                : display;
  Layer layer(ctx.document.allocate_layer_id(), label, LayerKind::Adjustment);
  configure_adjustment_layer(layer, settings);
  apply_common(ctx, node, layer, label);

  // The node's own bitmap (an M8/M16 plane) is the adjustment's mask; an
  // all-zero plane means "no mask painted".
  if (const af::AfClass* bitmap = node.child_class(af::tag4("Bitm"))) {
    if (auto decoded = decode_bitmap(ctx.bytes, ctx.container, *bitmap, label, &ctx.notices,
                                     nullptr);
        decoded && !decoded->mask.empty()) {
      bool any_nonzero = false;
      for (std::int32_t y = 0; y < decoded->mask.height() && !any_nonzero; ++y) {
        for (std::int32_t x = 0; x < decoded->mask.width(); ++x) {
          if (decoded->mask.pixel(x, y)[0] != 0) {
            any_nonzero = true;
            break;
          }
        }
      }
      if (any_nonzero) {
        const auto origin = layer_origin(node_xfrm(ctx, node));
        LayerMask mask;
        mask.bounds = Rect{origin ? origin->first : 0, origin ? origin->second : 0,
                           decoded->mask.width(), decoded->mask.height()};
        mask.pixels = std::move(decoded->mask);
        mask.default_color = 255;
        layer.set_mask(std::move(mask));
      }
    }
  }
  if (approximate) {
    ctx.notices.push_back("Layer '" + label +
                          "': adjustment converted approximately (the engines' math differs)");
  }
  return layer;
}

// Adjustments can ride a layer's AdCh enclosure list instead of its child
// list (Affinity nests an adjustment INSIDE a layer either way to clip it to
// that layer; esdreika stores Brightness/Contrast + HSL on a metal texture as
// adjuncts). Emit each as a clipped adjustment layer directly above the
// owner; apply_mask_children skips these same nodes so their planes never
// become the owner's mask.
void emit_adjunct_adjustments(LayerBuildContext& ctx, const af::AfClass& node,
                              std::vector<Layer>& out) {
  const auto* adjuncts = class_list(node, af::tag4("AdCh"));
  if (adjuncts == nullptr) {
    return;
  }
  for (const auto& adjunct : *adjuncts) {
    if (adjunct == nullptr || !is_adjustment_or_filter(adjunct->type_tag)) {
      continue;
    }
    const std::string name = adjunct->string_field(af::tag4("Desc"));
    if (auto layer = build_adjustment_layer(ctx, *adjunct,
                                            name.empty() ? std::string("Layer") : name)) {
      layer->set_clipped(true);
      out.push_back(std::move(*layer));
    } else {
      ctx.notices.push_back("Layer '" + node.string_field(af::tag4("Desc")) +
                            "': an attached Affinity adjustment or live filter is not supported "
                            "and was skipped");
    }
  }
}

// Wrap a pristine placed image as an embedded Patchy smart object: the
// untouched original file becomes the source (Edit/Replace Contents work,
// PSD saves embed it) while the decoded pixels stay the layer raster. Mirrors
// the convert-to-smart-object authoring flow (main_window_smart_objects.cpp).
void attach_placed_smart_object(LayerBuildContext& ctx, Layer& layer,
                                std::shared_ptr<const std::vector<std::uint8_t>> file_bytes,
                                const std::string& source_name, std::int32_t source_w,
                                std::int32_t source_h, const std::array<double, 8>& quad) {
  if (file_bytes == nullptr || file_bytes->empty() || source_w <= 0 || source_h <= 0) {
    return;
  }
  const auto& head = *file_bytes;
  std::string filetype = "    ";
  std::string fallback_extension;
  if (head.size() >= 3 && head[0] == 0xFF && head[1] == 0xD8) {
    filetype = "JPEG";
    fallback_extension = ".jpg";
  } else if (head.size() >= 4 && head[0] == 0x89 && head[1] == 'P') {
    filetype = "png ";
    fallback_extension = ".png";
  }
  const std::string filename =
      source_name.empty() ? "Placed" + fallback_extension : source_name;
  const auto uuid = generate_smart_object_uuid();
  ctx.document.metadata().smart_objects.add_embedded(uuid, filename, filetype,
                                                     std::move(file_bytes));
  SmartObjectPlacement placement;
  placement.uuid = uuid;
  placement.transform = quad;
  placement.width = source_w;
  placement.height = source_h;
  placement.resolution = ctx.document.print_settings().horizontal_ppi;
  const auto placed_instance = generate_smart_object_uuid();
  set_layer_smart_object_metadata(layer, placement, placed_instance, "SoLd", "",
                                  kSmartObjectRasterStatusPatchy);
  // The authored SoLd rides the normal preserve-unless-edited machinery on
  // PSD save, exactly like a Patchy-converted smart object.
  layer.unknown_psd_blocks().push_back(
      UnknownPsdBlock{"SoLd", psd::author_placed_layer_sold_payload(placement, placed_instance)});
}

// Text nodes (TxtA artistic / TxtF frame): extract the story into a Patchy
// text layer carrying the standard patchy.text.* metadata plus the .af
// placement markers; MainWindow renders it post-open through the internal
// text pipeline (the SVG import pattern). Wire shape (pinned by the corpus
// text docs): StSt -> Stry -> Blok[] (StBl) with Glyp/GStr/Utf8 = the text
// (NUL-terminated per block), GAtt/GlAS/Runs[] (GlAR) = glyph style runs
// whose Indx is the run's END boundary (exclusive, Unicode CODEPOINTS of the
// block text including the NUL - pinned by the emoji fixture; the runs
// partition it) and whose Item (possibly a shared-class ref, possibly sparse
// - unset fields inherit the previous run) carries DFnt font, Doub[0] size,
// Objs[0] = brush FDsc -> FDeF -> Colr RGBA; PAtt/PaAS/Runs[] the same
// END-boundary shape for paragraphs
// (Item.Ints[0] = alignment), TxtH -> ArFr|CoFr -> FrmB = the layout box
// ([x0,y0,x1,y1]; ArtV = artistic ascent). Mixed run styles become
// patchy.text.runs/html through the shared PSD serializers. Returns nullopt
// when the story shape is missing (caller keeps the placeholder path).

// Decode one UTF-8 codepoint at `index` of `text`; lenient (malformed bytes
// decode one byte as U+003F, matching the PSD reader's tolerance).
[[nodiscard]] std::uint32_t decode_utf8_at(const std::string& text, std::size_t index,
                                           std::size_t& consumed) {
  const auto lead = static_cast<unsigned char>(text[index]);
  consumed = 1;
  if (lead < 0x80U) {
    return lead;
  }
  if ((lead & 0xE0U) == 0xC0U && index + 1 < text.size()) {
    consumed = 2;
    return ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(text[index + 1]) & 0x3FU);
  }
  if ((lead & 0xF0U) == 0xE0U && index + 2 < text.size()) {
    consumed = 3;
    return ((lead & 0x0FU) << 12U) |
           ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6U) |
           (static_cast<unsigned char>(text[index + 2]) & 0x3FU);
  }
  if ((lead & 0xF8U) == 0xF0U && index + 3 < text.size()) {
    consumed = 4;
    return ((lead & 0x07U) << 18U) |
           ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12U) |
           ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6U) |
           (static_cast<unsigned char>(text[index + 3]) & 0x3FU);
  }
  return 0x3FU;
}

// Apply a glyph-run Item's style fields onto `run` (fields the item omits keep
// the carried-in values, so sparse items inherit from the previous run).
// Caps state of a glyph-run item, from the OtAt OpenType-feature settings in
// the item's Objs (Setn[] OTFS {Feat 4CC, Valu}): the private 'CAP\x01' tag is
// All Caps (Patchy uppercases the covered text); smcp/c2sc/pcap/c2pc/titl/unic
// are the small-caps family (unsupported; notice). An item WITHOUT an OtAt is
// sparse and inherits the previous run's caps; OtAt with an empty Setn resets.
enum class AfRunCaps { Inherit, None, AllCaps, Unsupported };

[[nodiscard]] AfRunCaps af_run_caps(const af::AfClass& item) {
  const auto* objects = class_list(item, af::tag4("Objs"));
  if (objects == nullptr) {
    return AfRunCaps::Inherit;
  }
  for (const auto& object : *objects) {
    if (object == nullptr || object->type_tag != af::tag4("OtAt")) {
      continue;
    }
    AfRunCaps result = AfRunCaps::None;
    if (const auto* settings = class_list(*object, af::tag4("Setn"))) {
      for (const auto& setting : *settings) {
        if (setting == nullptr || setting->int_field(af::tag4("Valu"), 0) == 0) {
          continue;
        }
        const auto feature =
            static_cast<std::uint32_t>(setting->int_field(af::tag4("Feat"), 0));
        if (feature == af::tag4("CAP\x01")) {
          return AfRunCaps::AllCaps;
        }
        if (feature == af::tag4("smcp") || feature == af::tag4("c2sc") ||
            feature == af::tag4("pcap") || feature == af::tag4("c2pc") ||
            feature == af::tag4("titl") || feature == af::tag4("unic")) {
          result = AfRunCaps::Unsupported;
        }
      }
    }
    return result;
  }
  return AfRunCaps::Inherit;
}

// Uppercase a UTF-8 byte range in place, byte-length-preserving: ASCII a-z and
// the Latin-1 letters (except ss, whose uppercase would grow). Enough for the
// All Caps import; anything outside stays as authored.
void uppercase_utf8_range(std::string& text, std::size_t from, std::size_t to) {
  to = std::min(to, text.size());
  for (std::size_t i = from; i < to; ++i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    if (byte >= 'a' && byte <= 'z') {
      text[i] = static_cast<char>(byte - 0x20);
    } else if (byte == 0xC3U && i + 1 < to) {
      const auto low = static_cast<unsigned char>(text[i + 1]);
      // U+00E0..U+00FE lowercase (0xC3 0xA0..0xBE) -> U+00C0..U+00DE
      // (0xC3 0x80..0x9E); skip the division sign U+00F7.
      if (low >= 0xA0U && low <= 0xBEU && low != 0xB7U) {
        text[i + 1] = static_cast<char>(low - 0x20);
      }
      ++i;
    } else if (byte >= 0xC2U) {
      // Skip over the rest of any other multi-byte sequence.
      i += byte >= 0xF0U ? 3 : (byte >= 0xE0U ? 2 : 1);
    }
  }
}

void apply_af_run_item(const af::AfClass& item, psd::PsdTextStyleRun& run, float& alpha) {
  if (const af::AfClass* font = item.child_class(af::tag4("DFnt"))) {
    const std::string stored = font->string_field(af::tag4("Famy"));
    if (!stored.empty()) {
      run.family = stored;
    }
    // Non-normal width classes (Widh 5 = normal) can lose the face through
    // the wire family alone ("Arial" + Widh 3 is Arial Narrow); the
    // PostScript name resolves the display family ("ArialNarrow" -> "Arial
    // Narrow"). Upgrade ONLY when the resolved name extends the wire family
    // with a qualifier - face-specific wire families ("Futura MdCn BT") are
    // already the installed name and a humanized PostScript would miss.
    if (font->int_field(af::tag4("Widh"), 5) != 5) {
      const std::string postscript = font->string_field(af::tag4("Post"));
      if (!postscript.empty()) {
        auto resolved = psd::heuristic_resolved_photoshop_font(postscript);
        const auto ci_equal = [](char a, char b) {
          return std::tolower(static_cast<unsigned char>(a)) ==
                 std::tolower(static_cast<unsigned char>(b));
        };
        const bool extends_family =
            resolved.family.size() > run.family.size() + 1 &&
            resolved.family[run.family.size()] == ' ' &&
            std::equal(run.family.begin(), run.family.end(), resolved.family.begin(), ci_equal);
        if (extends_family) {
          run.family = std::move(resolved.family);
        }
      }
    }
    run.bold = font->int_field(af::tag4("Wegt"), 400) >= 600;
    run.italic = font->bool_field(af::tag4("Ital"), false);
  }
  const auto doubles = item.vec_field(af::tag4("Doub"));
  if (!doubles.empty() && doubles[0] > 0.0) {
    run.size = doubles[0];
  }
  if (const auto* fills = class_list(item, af::tag4("Objs"));
      fills != nullptr && !fills->empty() && fills->front() != nullptr) {
    if (const af::AfClass* fill = fills->front()->child_class(af::tag4("FDeF"))) {
      if (const auto stored = read_rgba_color(fill->child_class(af::tag4("Colr")))) {
        const auto to_channel = [](float value) {
          return static_cast<std::uint8_t>(
              std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
        };
        run.color = RgbColor{to_channel((*stored)[0]), to_channel((*stored)[1]),
                             to_channel((*stored)[2])};
        alpha = std::clamp((*stored)[3], 0.0F, 1.0F);
      }
    }
  }
}

// Affinity ParagraphAlignXType (0 left, 1 centre, 2 right, 3 justify) -> the
// PSD justification ids the paragraph-runs serializer expects (1 right,
// 2 center, 3 justify).
[[nodiscard]] int psd_justification_from_af_alignment(std::int64_t align) {
  switch (align) {
    case 1:
      return 2;
    case 2:
      return 1;
    case 3:
      return 3;
    default:
      return 0;
  }
}

[[nodiscard]] std::optional<Layer> build_text_layer(LayerBuildContext& ctx, const af::AfClass& node,
                                                    const std::string& display) {
  const af::AfClass* story = node.child_class(af::tag4("StSt"));
  if (story == nullptr) {
    return std::nullopt;
  }
  const auto* blocks = class_list(*story, af::tag4("Blok"));
  if (blocks == nullptr || blocks->empty()) {
    return std::nullopt;
  }

  std::string text;
  std::vector<psd::PsdTextStyleRun> style_runs;
  std::vector<psd::PsdTextParagraphRun> paragraph_runs;
  bool has_style = false;
  float first_alpha = 1.0F;
  std::int64_t align = -1;
  // Carried style: runs inherit unset fields from the previous run; the
  // pre-first-run default matches the old single-style fallbacks.
  psd::PsdTextStyleRun carry;
  carry.family = "Arial";
  carry.size = 12.0;
  carry.color = RgbColor{0, 0, 0};
  AfRunCaps carry_caps = AfRunCaps::None;
  bool unsupported_caps = false;
  int base_units = 0;  // output UTF-16 units emitted before the current block

  for (const auto& block : *blocks) {
    if (block == nullptr) {
      continue;
    }
    std::string raw;
    if (const af::AfClass* glyphs = block->child_class(af::tag4("Glyp"))) {
      raw = glyphs->string_field(af::tag4("Utf8"));
    }
    if (!text.empty()) {
      text += '\n';
      base_units += 1;
    }

    // Walk the block text once: paragraph breaks (U+2029) and soft line
    // breaks (U+2028) become '\n', NULs drop, and out_at maps every wire
    // CODEPOINT boundary (run Indx values count codepoints of the original
    // block text INCLUDING the trailing NUL) to its output UTF-16 offset
    // (the patchy.text.runs contract).
    std::string out;
    std::vector<int> out_at{0};
    std::vector<std::size_t> out_byte_at{0};  // codepoint boundary -> byte offset in out
    int local_out = 0;
    std::size_t at = 0;
    while (at < raw.size()) {
      std::size_t consumed = 1;
      const std::uint32_t cp = decode_utf8_at(raw, at, consumed);
      if (cp == 0x2029U || cp == 0x2028U) {
        out += '\n';
        local_out += 1;
      } else if (cp != 0U) {
        out.append(raw, at, consumed);
        local_out += cp > 0xFFFFU ? 2 : 1;
      }
      out_at.push_back(local_out);
      out_byte_at.push_back(out.size());
      at += consumed;
    }
    const std::size_t block_text_base = text.size();
    text += out;
    const int block_units = static_cast<int>(out_at.size()) - 1;

    const auto run_range = [&](int from, int to) {
      from = std::clamp(from, 0, block_units);
      to = std::clamp(to, from, block_units);
      return std::pair<int, int>{base_units + out_at[static_cast<std::size_t>(from)],
                                 base_units + out_at[static_cast<std::size_t>(to)]};
    };

    // Glyph style runs. A block without runs extends the carried style.
    const auto* glyph_runs = [&]() -> const std::vector<std::shared_ptr<af::AfClass>>* {
      const af::AfClass* glyph_atts = block->child_class(af::tag4("GAtt"));
      return glyph_atts != nullptr ? class_list(*glyph_atts, af::tag4("Runs")) : nullptr;
    }();
    const auto push_style_run = [&](const psd::PsdTextStyleRun& run) {
      if (run.length <= 0) {
        return;
      }
      if (!style_runs.empty()) {
        auto& last = style_runs.back();
        // The '\n' joining blocks belongs to no wire run; extend the previous
        // run over it so the runs fully cover the text (html emission drops
        // uncovered characters).
        if (last.start + last.length < run.start) {
          last.length = run.start - last.start;
        }
        if (last.family == run.family && last.size == run.size && last.color == run.color &&
            last.bold == run.bold && last.italic == run.italic) {
          last.length = run.start + run.length - last.start;
          return;
        }
      }
      style_runs.push_back(run);
    };
    int prev = 0;
    if (glyph_runs != nullptr) {
      for (const auto& run : *glyph_runs) {
        if (run == nullptr) {
          continue;
        }
        const int end = static_cast<int>(run->int_field(af::tag4("Indx"), block_units));
        psd::PsdTextStyleRun styled = carry;
        if (const af::AfClass* item = run->child_class(af::tag4("Item"))) {
          float alpha = 1.0F;
          apply_af_run_item(*item, styled, alpha);
          if (!has_style) {
            first_alpha = alpha;
          }
          has_style = true;
          if (const auto caps = af_run_caps(*item); caps != AfRunCaps::Inherit) {
            carry_caps = caps;
          }
        }
        if (carry_caps == AfRunCaps::AllCaps) {
          const auto from = static_cast<std::size_t>(std::clamp(prev, 0, block_units));
          const auto to = static_cast<std::size_t>(std::clamp(end, prev, block_units));
          uppercase_utf8_range(text, block_text_base + out_byte_at[from],
                               block_text_base + out_byte_at[to]);
        } else if (carry_caps == AfRunCaps::Unsupported) {
          unsupported_caps = true;
        }
        const auto [start_out, end_out] = run_range(prev, end);
        styled.start = start_out;
        styled.length = end_out - start_out;
        push_style_run(styled);
        carry = styled;
        prev = std::max(prev, end);
      }
    }
    if (prev < block_units) {
      // Tail not covered by any run keeps the carried style.
      if (carry_caps == AfRunCaps::AllCaps) {
        const auto from = static_cast<std::size_t>(std::clamp(prev, 0, block_units));
        uppercase_utf8_range(text, block_text_base + out_byte_at[from],
                             block_text_base + out_byte_at[static_cast<std::size_t>(block_units)]);
      }
      psd::PsdTextStyleRun styled = carry;
      const auto [start_out, end_out] = run_range(prev, block_units);
      styled.start = start_out;
      styled.length = end_out - start_out;
      push_style_run(styled);
    }

    // Paragraph runs (alignment; same END-boundary Indx shape).
    if (const af::AfClass* paragraph_atts = block->child_class(af::tag4("PAtt"))) {
      if (const auto* runs = class_list(*paragraph_atts, af::tag4("Runs"))) {
        int para_prev = 0;
        for (const auto& run : *runs) {
          if (run == nullptr) {
            continue;
          }
          const int end = static_cast<int>(run->int_field(af::tag4("Indx"), block_units));
          std::int64_t run_align = 0;
          double space_before = 0.0;
          double space_after = 0.0;
          double first_line_indent = 0.0;
          double left_indent = 0.0;
          double right_indent = 0.0;
          if (const af::AfClass* item = run->child_class(af::tag4("Item"))) {
            const auto ints = item->vec_field(af::tag4("Ints"));
            if (!ints.empty()) {
              run_align = static_cast<std::int64_t>(ints[0]);
            }
            // Doub[5]/[6] = paragraph space before/after in document px
            // (pinned by the 9/17 spacing probe doc). Doub[2]/[3]/[4] =
            // left/right/first-line indents (the text-indent probe doc):
            // Affinity's left indent positions CONTINUATION lines only, and
            // the first-line indent is ABSOLUTE from the column edge with
            // negatives clamped to 0 at render (tips.psd's PS hanging indent
            // StartIndent 24 / FirstLineIndent -24 converts to wire 24 / 0).
            const auto doubles = item->vec_field(af::tag4("Doub"));
            if (doubles.size() > 6) {
              space_before = std::clamp(doubles[5], 0.0, 10000.0);
              space_after = std::clamp(doubles[6], 0.0, 10000.0);
            }
            if (doubles.size() > 3) {
              left_indent = std::clamp(doubles[2], 0.0, 10000.0);
              right_indent = std::clamp(doubles[3], 0.0, 10000.0);
            }
            if (doubles.size() > 4) {
              first_line_indent = std::clamp(doubles[4], 0.0, 10000.0);
            }
          }
          if (align < 0) {
            align = run_align;
          }
          const auto [start_out, end_out] = run_range(para_prev, end);
          psd::PsdTextParagraphRun paragraph;
          paragraph.start = start_out;
          paragraph.length = end_out - start_out;
          paragraph.justification = psd_justification_from_af_alignment(run_align);
          // Affinity does not push the first paragraph down by its
          // space-before (Qt's top margin would), so the leading paragraph
          // keeps only its space-after.
          paragraph.space_before = paragraph.start == 0 ? 0.0 : space_before;
          paragraph.space_after = space_after;
          // Serialized in the PS/Qt convention: first-line relative to the
          // left indent, so a wire hanging indent goes negative.
          paragraph.first_line_indent = first_line_indent - left_indent;
          paragraph.start_indent = left_indent;
          paragraph.end_indent = right_indent;
          if (paragraph.length > 0) {
            paragraph_runs.push_back(paragraph);
          }
          para_prev = std::max(para_prev, end);
        }
      }
    }

    base_units += local_out;
  }
  const bool only_whitespace =
      text.find_first_not_of(" \t\r\n") == std::string::npos;
  if (only_whitespace || !has_style || style_runs.empty()) {
    return std::nullopt;
  }

  // Layer-level style (options-bar defaults, single-style render path): the
  // first run, exactly like the PSD reader.
  const std::string family = style_runs.front().family;
  const bool bold = style_runs.front().bold;
  const bool italic = style_runs.front().italic;
  const int size = std::clamp(static_cast<int>(std::lround(style_runs.front().size)), 1, 4000);
  const RgbColor first_color = style_runs.front().color;

  // Placement: the TxtH frame box (node-local; Xfrm positions it). For an
  // axis-aligned transform, fold the scale into the font size and map the box
  // through the affine. Rotated/sheared ARTISTIC text instead keeps the raw
  // node-local box and unscaled sizes and carries the full Xfrm in a marker:
  // the post-open pass renders the glyphs THROUGH the affine (crisp, exact)
  // and stamps the standard patchy.text.transform. Rotated FRAME text still
  // approximates axis-aligned (the box-flow renderer has no transform path).
  const af::AfClass* frame = node.child_class(af::tag4("TxtH"));
  if (frame == nullptr) {
    return std::nullopt;
  }
  auto box = frame->vec_field(af::tag4("FrmB"));
  if (box.size() != 4) {
    return std::nullopt;
  }
  double ascent = frame->double_field(af::tag4("ArtV"), 0.0);
  double effective_size = static_cast<double>(size);
  double size_scale = 1.0;
  bool transform_approximated = false;
  std::string transform_marker;
  if (const auto xfrm = node_xfrm(ctx, node); xfrm.size() == 6) {
    const double a = xfrm[0];
    const double b_term = xfrm[1];
    const double tx = xfrm[2];
    const double c_term = xfrm[3];
    const double d = xfrm[4];
    const double ty = xfrm[5];
    const double scale_x = std::hypot(a, c_term);
    const double scale_y = std::hypot(b_term, d);
    const double scale = (scale_x + scale_y) * 0.5;
    if (scale < 0.01) {
      return std::nullopt;
    }
    const double rotation = std::atan2(c_term, a);
    const bool non_axis_aligned =
        std::abs(rotation) > 0.03 || std::abs(scale_x - scale_y) > 0.05 * scale;
    if (non_axis_aligned && node.type_tag == af::tag4("TxtA")) {
      // Full-affine path: raw box, wire-unit sizes, the whole Xfrm rides the
      // marker ("a b tx c d ty", the wire order).
      for (int i = 0; i < 6; ++i) {
        if (i != 0) {
          transform_marker += ' ';
        }
        transform_marker += std::to_string(xfrm[static_cast<std::size_t>(i)]);
      }
    } else {
      // Map the four box corners through the affine; keep the axis-aligned box.
      double min_x = 0.0;
      double min_y = 0.0;
      double max_x = 0.0;
      double max_y = 0.0;
      bool first_corner = true;
      for (const auto& [sx, sy] : {std::pair<double, double>{box[0], box[1]},
                                   {box[2], box[1]},
                                   {box[0], box[3]},
                                   {box[2], box[3]}}) {
        const double px = a * sx + b_term * sy + tx;
        const double py = c_term * sx + d * sy + ty;
        min_x = first_corner ? px : std::min(min_x, px);
        max_x = first_corner ? px : std::max(max_x, px);
        min_y = first_corner ? py : std::min(min_y, py);
        max_y = first_corner ? py : std::max(max_y, py);
        first_corner = false;
      }
      // Assign the four edges in place. box is already known to hold exactly
      // four values, and assigning a braced list instead makes GCC's optimizer
      // misjudge the temporary array it builds for the list, which it reports
      // as a false-positive -Warray-bounds.
      box[0] = min_x;
      box[1] = min_y;
      box[2] = max_x;
      box[3] = max_y;
      effective_size *= scale;
      size_scale = scale;
      ascent *= scale_y;
      transform_approximated = non_axis_aligned;
    }
  }
  const int stored_size =
      std::clamp(static_cast<int>(std::lround(effective_size)), 1, 4000);
  // Fold the transform scale into every run. Rounding the wire size before
  // scaling matches the historical single-style fold (stored_size above), and
  // integral sizes keep the runs serialization at the v1 shape Patchy's own
  // text tool writes.
  for (auto& run : style_runs) {
    run.size = static_cast<double>(std::clamp(
        std::lround(static_cast<double>(std::lround(run.size)) * size_scale), 1L, 4000L));
  }
  for (auto& paragraph : paragraph_runs) {
    paragraph.space_before *= size_scale;
    paragraph.space_after *= size_scale;
    paragraph.first_line_indent *= size_scale;
    paragraph.start_indent *= size_scale;
    paragraph.end_indent *= size_scale;
  }

  Layer layer(ctx.document.allocate_layer_id(), display, LayerKind::Pixel);
  auto& metadata = layer.metadata();
  metadata[kLayerMetadataText] = text;
  metadata[kLayerMetadataTextFont] = family;
  metadata[kLayerMetadataTextSize] = std::to_string(stored_size);
  constexpr char kHex[] = "0123456789abcdef";
  std::string hex = "#......";
  hex[1] = kHex[first_color.red >> 4];
  hex[2] = kHex[first_color.red & 15];
  hex[3] = kHex[first_color.green >> 4];
  hex[4] = kHex[first_color.green & 15];
  hex[5] = kHex[first_color.blue >> 4];
  hex[6] = kHex[first_color.blue & 15];
  metadata[kLayerMetadataTextColor] = hex;
  metadata[kLayerMetadataTextBold] = bold ? "true" : "false";
  metadata[kLayerMetadataTextItalic] = italic ? "true" : "false";
  const bool is_frame = node.type_tag == af::tag4("TxtF");
  if (is_frame) {
    // Frame text wraps within its box (the internal pipeline's box flow).
    metadata[kLayerMetadataTextFlow] = "box";
    metadata[kLayerMetadataTextBoxWidth] = std::to_string(
        std::max(1, static_cast<int>(std::lround(box[2] - box[0]))));
    metadata[kLayerMetadataTextBoxHeight] = std::to_string(
        std::max(1, static_cast<int>(std::lround(box[3] - box[1]))));
  }
  // Mixed run styles and non-left alignment ride the standard PSD-shaped
  // rich-text metadata (runs + Qt html body); a single left-aligned style
  // keeps the plain single-style metadata exactly as before.
  const bool multi_style = style_runs.size() > 1;
  bool any_paragraph_layout = false;
  for (const auto& run : paragraph_runs) {
    any_paragraph_layout = any_paragraph_layout || run.justification != 0 ||
                           run.space_before > 0.0 || run.space_after > 0.0 ||
                           run.first_line_indent != 0.0 || run.start_indent > 0.0 ||
                           run.end_indent > 0.0;
  }
  // Runs also emit for single-style text that carries paragraph layout: the
  // rich-text-runs render path builds REAL paragraph blocks from the plain
  // text, while the html body is a single <p> with <br/> breaks, so block
  // alignment and spacing only apply on the runs path (Affinity's centred
  // multi-paragraph text lost its paragraph spacing through html).
  if (multi_style || any_paragraph_layout) {
    metadata[kLayerMetadataTextRuns] = psd::serialize_patchy_text_runs(style_runs);
  }
  if (any_paragraph_layout) {
    metadata[kLayerMetadataTextParagraphRuns] =
        psd::serialize_patchy_paragraph_runs(paragraph_runs);
  }
  if (multi_style || align > 0) {
    metadata[kLayerMetadataTextHtml] =
        psd::html_from_text_runs(text, style_runs, paragraph_runs);
  }
  metadata[kLayerMetadataAfPendingText] = "1";
  {
    std::string box_text;
    for (int i = 0; i < 4; ++i) {
      if (i != 0) {
        box_text += ' ';
      }
      box_text += std::to_string(box[static_cast<std::size_t>(i)]);
    }
    metadata[kLayerMetadataAfTextFrame] = box_text;
  }
  if (node.type_tag == af::tag4("TxtA") && ascent > 0.0) {
    metadata[kLayerMetadataAfTextAscent] = std::to_string(ascent);
  }
  if (align > 0) {
    metadata[kLayerMetadataAfTextAlign] = std::to_string(align);
  }
  if (!transform_marker.empty()) {
    metadata[kLayerMetadataAfTextXfrm] = transform_marker;
  }

  apply_common(ctx, node, layer, display);
  if (first_alpha < 1.0F) {
    layer.set_opacity(layer.opacity() * first_alpha);
  }
  // Rough placement until the post-open pass renders real glyphs.
  layer.set_bounds(Rect{static_cast<std::int32_t>(std::lround(box[0])),
                        static_cast<std::int32_t>(std::lround(box[1])), 1, 1});
  if (transform_approximated) {
    ctx.notices.push_back("Layer '" + display +
                          "': text rotation/shear is approximated (rendered axis-aligned)");
  }
  if (unsupported_caps) {
    ctx.notices.push_back("Layer '" + display +
                          "': small/petite caps text style is not supported; rendered as typed");
  }
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::fprintf(stderr, "[af] text '%s': %zu style runs, %zu paragraph runs, align %lld\n",
                 display.c_str(), style_runs.size(), paragraph_runs.size(),
                 static_cast<long long>(align));
    for (const auto& run : style_runs) {
      std::fprintf(stderr, "[af]   run %d+%d '%s' %.1f%s%s #%02x%02x%02x\n", run.start,
                   run.length, run.family.c_str(), run.size, run.bold ? " bold" : "",
                   run.italic ? " italic" : "", run.color.red, run.color.green, run.color.blue);
    }
  }
  return layer;
}

// ---- Vector curve nodes (PCrv) -> Patchy shape layers ----

// A vector fill from an FDsc descriptor: FDeF is FilS (solid Colr), FilG
// (gradient - the same shape style_gradient_from_fill decodes, including the
// FDeX placement transform), or FilN (none). Patchy's VectorFill carries no
// alpha, so the color's alpha comes back separately.
struct AfVectorFill {
  VectorFill fill;
  float alpha{1.0F};
};

// A node's descriptor field is a single class in some documents and a
// one-element class list in others (BFFl/LIFl mirror LILn's list shape).
[[nodiscard]] const af::AfClass* first_class_of(const af::AfClass& node, std::uint32_t tag) {
  if (const af::AfClass* single = node.child_class(tag)) {
    return single;
  }
  if (const auto* list = class_list(node, tag); list != nullptr && !list->empty()) {
    return list->front().get();
  }
  return nullptr;
}

[[nodiscard]] std::optional<AfVectorFill> vector_fill_from_descriptor(const af::AfClass* descriptor) {
  if (descriptor == nullptr) {
    return std::nullopt;
  }
  const af::AfClass* fill = descriptor->child_class(af::tag4("FDeF"));
  if (fill == nullptr && (descriptor->type_tag == af::tag4("FilS") ||
                          descriptor->type_tag == af::tag4("FilN") ||
                          descriptor->type_tag == af::tag4("FilG"))) {
    // The oldest generation (doc versions 3..9) stores the Fill class
    // DIRECTLY on the field (vh-check's BFil/PFil) instead of wrapping it in
    // an FDsc; a direct FilG has no FDeX placement and stays undecoded.
    fill = descriptor;
  }
  if (fill == nullptr) {
    return std::nullopt;
  }
  AfVectorFill result;
  if (fill->type_tag == af::tag4("FilN")) {
    result.fill.kind = VectorFillKind::None;
    return result;
  }
  if (fill->type_tag == af::tag4("FilG")) {
    if (auto gradient = style_gradient_from_fill(descriptor)) {
      result.fill.kind = VectorFillKind::Gradient;
      result.fill.gradient = std::move(*gradient);
      return result;
    }
    return std::nullopt;
  }
  if (const auto stored = read_rgba_color(fill->child_class(af::tag4("Colr")))) {
    const auto to_channel = [](float value) {
      return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    result.fill.kind = VectorFillKind::Solid;
    result.fill.color =
        RgbColor{to_channel((*stored)[0]), to_channel((*stored)[1]), to_channel((*stored)[2])};
    result.alpha = std::clamp((*stored)[3], 0.0F, 1.0F);
    return result;
  }
  return std::nullopt;
}

// The node's path: Crvs -> PCvD -> Data, an UNTAGGED inline class laid out as
// [u8 version][u32 subpath count] then per subpath [bool closed][point
// curve-array]. Each 18-byte point record is x f64 LE, y f64 LE, u16 flags:
// 0x0001 corner anchor, 0x0002 smooth anchor, 0x0100 = the previous anchor's
// control-out, 0x0200 = the NEXT anchor's control-in. Closed subpaths repeat
// the first anchor at the end (folded away below). All subpaths share shape
// group 0: Affinity fills a poly-curve even-odd (the two same-winding nested
// rectangles of the donut probe render a hole), which is exactly Patchy's
// within-group rule.
// Symbol instances (the 2.x generation's Designer symbols): a linked instance
// stores no geometry of its own; its SLnk -> ILSN -> ILOb ring lists every
// sibling instance of the same symbol member, and the defining sibling carries
// the real record (Crvs for curves, Shpe for parametric shapes). The tree
// parser resolves shared references to the defining AfClass, so one hop
// through the ring finds it. Pinned by the beat-bot wild file (two symbol
// raccoons; the second is all linked instances). Paint (BFFl/LILn/LIFl) and
// the transform stay per-instance on the node itself.
[[nodiscard]] const af::AfClass* symbol_sibling_with(const af::AfClass& node,
                                                     std::uint32_t field_tag) {
  const af::AfClass* link = node.child_class(af::tag4("SLnk"));
  if (link == nullptr) {
    return nullptr;
  }
  const auto* members = class_list(*link, af::tag4("ILOb"));
  if (members == nullptr) {
    return nullptr;
  }
  for (const auto& member : *members) {
    if (member != nullptr && member.get() != &node &&
        member->child_class(field_tag) != nullptr) {
      return member.get();
    }
  }
  return nullptr;
}

[[nodiscard]] std::optional<VectorPath> vector_path_from_node(const af::AfClass& node) {
  const af::AfClass* curves = node.child_class(af::tag4("Crvs"));
  if (curves == nullptr) {
    // Compound (Comp) nodes store their baked, already-booleaned result
    // poly-curve under a lowercase 'crvs' field with the same PCvD layout.
    curves = node.child_class(af::tag4("crvs"));
  }
  if (curves == nullptr) {
    if (const af::AfClass* sibling = symbol_sibling_with(node, af::tag4("Crvs"))) {
      curves = sibling->child_class(af::tag4("Crvs"));
    }
  }
  if (curves == nullptr) {
    return std::nullopt;
  }
  const af::AfClass* data = curves->child_class(af::tag4("Data"));
  if (data == nullptr) {
    return std::nullopt;
  }
  const auto read_f64 = [](const std::uint8_t* bytes) {
    std::uint64_t bits = 0;
    for (int i = 7; i >= 0; --i) {
      bits = (bits << 8U) | bytes[i];
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  };
  VectorPath path;
  bool closed = true;
  for (const auto& field : data->fields) {
    if (const auto* flag = std::get_if<bool>(&field.value)) {
      closed = *flag;
      continue;
    }
    const auto* records = std::get_if<af::AfCurveArray>(&field.value);
    if (records == nullptr || records->record_size < 18) {
      continue;
    }
    PathSubpath subpath;
    subpath.closed = closed;
    subpath.op = PathCombineOp::Add;
    subpath.shape_group = 0;
    bool pending_in = false;
    double in_x = 0.0;
    double in_y = 0.0;
    const std::size_t stride = records->record_size;
    for (std::size_t at = 0; at + stride <= records->bytes.size(); at += stride) {
      const std::uint8_t* bytes = records->bytes.data() + at;
      const double x = read_f64(bytes);
      const double y = read_f64(bytes + 8);
      if (!std::isfinite(x) || !std::isfinite(y)) {
        continue;
      }
      const std::uint16_t flags =
          static_cast<std::uint16_t>(bytes[16] | (static_cast<std::uint16_t>(bytes[17]) << 8U));
      if ((flags & 0x0003U) != 0) {
        PathAnchor anchor{x, y, x, y, x, y, (flags & 0x0002U) != 0};
        if (pending_in) {
          anchor.in_x = in_x;
          anchor.in_y = in_y;
          pending_in = false;
        }
        subpath.anchors.push_back(anchor);
      } else if ((flags & 0x0100U) != 0) {
        if (!subpath.anchors.empty()) {
          subpath.anchors.back().out_x = x;
          subpath.anchors.back().out_y = y;
        }
      } else if ((flags & 0x0200U) != 0) {
        pending_in = true;
        in_x = x;
        in_y = y;
      }
    }
    // The closing record repeats anchor 0; fold its incoming handle there.
    if (subpath.closed && subpath.anchors.size() >= 2) {
      const auto& first = subpath.anchors.front();
      const auto& last = subpath.anchors.back();
      if (std::abs(first.anchor_x - last.anchor_x) < 0.0001 &&
          std::abs(first.anchor_y - last.anchor_y) < 0.0001) {
        subpath.anchors.front().in_x = last.in_x;
        subpath.anchors.front().in_y = last.in_y;
        subpath.anchors.pop_back();
      }
    }
    if (subpath.anchors.size() >= 2) {
      path.subpaths.push_back(std::move(subpath));
    }
    closed = true;
  }
  if (path.subpaths.empty()) {
    return std::nullopt;
  }
  return path;
}

// One decoded LDsc line descriptor. The LSty Data blob is a curve12_t (one
// f64 + four u8 flag bytes, per the afread headers); byte 2 of the four is
// the stroke panel's line-style control, pinned against ground truth July
// 2026: 0 = None (sprite wild file: black stroke defined but not drawn),
// 1 = Solid (the vec-* probe fixtures), 2 = Dashed (restaurant-menu-inside:
// Patn [4,2] at weight 8.33 renders 33/17 px dashes, so Patn values are
// stroke-width multiples exactly like Patchy's dash entries), 3 = textured
// brush (approximated as solid + notice). LDSa is the alignment control:
// 0 = Center (vec-*), 1 = Inside (restaurant-menu-inside), 2 = Outside (the
// remaining state of the three-way control). LDSc scales the width with the
// object transform.
struct AfLineStyle {
  double weight{0.0};
  std::uint8_t line_style{1};
  VectorStrokeAlignment alignment{VectorStrokeAlignment::Center};
  std::vector<double> dashes;      // stroke-width multiples
  double dash_phase{0.0};          // same units
  bool scale_with_object{false};
};

[[nodiscard]] std::optional<AfLineStyle> line_style_from_descriptor(
    const af::AfClass* line_descriptor) {
  if (line_descriptor == nullptr) {
    return std::nullopt;
  }
  const af::AfClass* style = line_descriptor->child_class(af::tag4("LDeL"));
  if (style == nullptr) {
    return std::nullopt;
  }
  AfLineStyle result;
  result.weight = style->double_field(af::tag4("Wght"), 0.0);
  if (const auto* data = style->field(af::tag4("Data"))) {
    if (const auto* blob = std::get_if<af::AfCurveArray>(&data->value);
        blob != nullptr && blob->bytes.size() >= 12) {
      result.line_style = blob->bytes[10];
    }
  }
  result.dashes = style->vec_field(af::tag4("Patn"));
  result.dash_phase = style->double_field(af::tag4("Phse"), 0.0);
  switch (line_descriptor->int_field(af::tag4("LDSa"), 0)) {
    case 1: result.alignment = VectorStrokeAlignment::Inside; break;
    case 2: result.alignment = VectorStrokeAlignment::Outside; break;
    default: result.alignment = VectorStrokeAlignment::Center; break;
  }
  result.scale_with_object = line_descriptor->bool_field(af::tag4("LDSc"), false);
  return result;
}

// A local-space vector path -> a real Patchy shape layer (the SVG import
// pattern: vector content + a baked raster; kLayerMetadataVectorShape +
// block-dirty so saves regenerate the PSD vector blocks). Shared by PCrv
// (hand-drawn curves) and ShpN (parametric shapes). Fill = BFFl, stroke paint
// = LIFl with width/style/alignment/dashes from LILn's line descriptor and
// the miter limit from the node's CnML; cap and join keep Patchy defaults
// (their wire enums are unmined - every observed file carries the identical
// default tuple). The node Xfrm maps local path coordinates into document
// space (full affine - vectors need no axis-aligned approximation). Returns
// nullopt when nothing is visible (placeholder path).
[[nodiscard]] std::optional<Layer> build_vector_layer_from_path(LayerBuildContext& ctx,
                                                                const af::AfClass& node,
                                                                const std::string& display,
                                                                VectorPath path) {
  double transform_scale = 1.0;
  if (const auto xfrm = node_xfrm(ctx, node); xfrm.size() == 6) {
    transform_vector_path(path, {xfrm[0], xfrm[3], xfrm[1], xfrm[4], xfrm[2], xfrm[5]});
    const double det = xfrm[0] * xfrm[4] - xfrm[1] * xfrm[3];
    if (std::isfinite(det) && std::abs(det) > 0.0) {
      transform_scale = std::sqrt(std::abs(det));
    }
  }
  VectorShapeContent content;
  content.path = std::move(path);
  float fill_alpha = 1.0F;
  bool brush_stroke_approximated = false;
  // The fill descriptor rides BFFl on current files; the oldest generation
  // (doc versions 3..9, the 2026-07-28 wild sweep) names the same field BFil
  // and stores the Fill class directly (no FDsc wrapper). Its stroke fields
  // are old-named too: the LDsc line descriptor rides a field tagged LSty
  // (same shape as LILn) and the stroke paint rides PFil (a direct Fill).
  const auto read_paint = [transform_scale, &brush_stroke_approximated](
                              const af::AfClass& source, VectorShapeContent& into,
                              float& alpha) -> bool {
    const af::AfClass* fill_descriptor = first_class_of(source, af::tag4("BFFl"));
    if (fill_descriptor == nullptr) {
      fill_descriptor = first_class_of(source, af::tag4("BFil"));
    }
    if (auto fill = vector_fill_from_descriptor(fill_descriptor)) {
      into.fill = std::move(fill->fill);
      alpha = fill->alpha;
    } else {
      into.fill.kind = VectorFillKind::None;
    }
    // LILn[0] (LDsc) -> LDeL: the field's value IS the LSty class.
    auto line = line_style_from_descriptor(first_class_of(source, af::tag4("LILn")));
    if (!line.has_value()) {
      line = line_style_from_descriptor(first_class_of(source, af::tag4("LSty")));
    }
    if (line.has_value() && line->weight > 0.0 && line->line_style != 0) {
      auto stroke_fill = vector_fill_from_descriptor(first_class_of(source, af::tag4("LIFl")));
      if (!stroke_fill.has_value() || stroke_fill->fill.kind == VectorFillKind::None) {
        stroke_fill = vector_fill_from_descriptor(first_class_of(source, af::tag4("PFil")));
      }
      if (stroke_fill.has_value() && stroke_fill->fill.kind != VectorFillKind::None) {
        into.stroke.enabled = true;
        into.stroke.width =
            line->scale_with_object ? line->weight * transform_scale : line->weight;
        into.stroke.alignment = line->alignment;
        into.stroke.content = std::move(stroke_fill->fill);
        into.stroke.opacity = std::clamp(static_cast<double>(stroke_fill->alpha), 0.0, 1.0);
        if (line->line_style == 2 && !line->dashes.empty()) {
          into.stroke.dashes = line->dashes;
          into.stroke.dash_offset = line->dash_phase;
        } else if (line->line_style == 3) {
          brush_stroke_approximated = true;
        }
        into.stroke.miter_limit =
            std::max(1.0, source.double_field(af::tag4("CnML"), into.stroke.miter_limit));
      }
    }
    return into.fill.kind != VectorFillKind::None || into.stroke.enabled;
  };
  bool painted = read_paint(node, content, fill_alpha);
  if (!painted) {
    // Symbol instances may store their paint on the defining sibling too
    // (beat-bot's linked raccoon members parse no local fill); chase the
    // instance-link rings the same way the geometry does.
    static constexpr const char* kRings[] = {"SLnk", "GLnk", "DLnk", "CLnk"};
    for (const char* ring : kRings) {
      const af::AfClass* link = node.child_class(tag_of(ring));
      const auto* members = link != nullptr ? class_list(*link, af::tag4("ILOb")) : nullptr;
      if (members == nullptr) {
        continue;
      }
      for (const auto& member : *members) {
        if (member != nullptr && member.get() != &node &&
            read_paint(*member, content, fill_alpha)) {
          painted = true;
          break;
        }
      }
      if (painted) {
        break;
      }
    }
  }
  if (!painted) {
    return std::nullopt;  // nothing visible; the honest placeholder says so
  }

  if (brush_stroke_approximated) {
    ctx.notices.push_back("Layer '" + display +
                          "': Affinity brush-textured stroke approximated as a solid stroke");
  }
  Layer layer(ctx.document.allocate_layer_id(), display, LayerKind::Pixel);
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  mark_layer_vector_block_dirty(layer);  // no preserved PSD blocks: regenerate on save
  layer.set_vector_shape(std::move(content));
  apply_common(ctx, node, layer, display);
  if (fill_alpha < 1.0F) {
    layer.set_fill_opacity(layer.fill_opacity() * fill_alpha);
  }
  apply_mask_children(ctx, node, layer, display);
  update_vector_shape_raster(layer, Rect{0, 0, ctx.document.width(), ctx.document.height()},
                             nullptr);
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::fprintf(stderr, "[af] vector '%s': baked %dx%d at %d,%d fill=%d stroke=%d\n",
                 display.c_str(), std::as_const(layer).pixels().width(),
                 std::as_const(layer).pixels().height(), layer.bounds().x, layer.bounds().y,
                 static_cast<int>(layer.vector_shape()->fill.kind),
                 layer.vector_shape()->stroke.enabled);
  }
  return layer;
}

// PCrv -> shape layer via its stored poly-curve.
[[nodiscard]] std::optional<Layer> build_vector_layer(LayerBuildContext& ctx, const af::AfClass& node,
                                                      const std::string& display) {
  auto path = vector_path_from_node(node);
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::fprintf(stderr, "[af] vector '%s': path=%d subpaths=%zu crvs=%d data=%d\n",
                 display.c_str(), path.has_value(),
                 path.has_value() ? path->subpaths.size() : 0U,
                 node.child_class(af::tag4("Crvs")) != nullptr,
                 node.child_class(af::tag4("Crvs")) != nullptr &&
                     node.child_class(af::tag4("Crvs"))->child_class(af::tag4("Data")) != nullptr);
  }
  if (!path.has_value()) {
    return std::nullopt;
  }
  return build_vector_layer_from_path(ctx, node, display, std::move(*path));
}

// ---- Parametric shape nodes (ShpN) -> Patchy shape layers ----

// The cubic-bezier approximation constant for a quarter circle.
constexpr double kQuarterArcK = 0.5522847498307936;

// Corner-type enum on the wire (ShapeCornerType in the scripting API).
enum : int {
  kAfCornerRound = 0,
  kAfCornerStraight = 1,
  kAfCornerRoundInverse = 2,
  kAfCornerCutout = 3,
  kAfCornerNone = 4,
};

// Appends one rectangle corner's anchors to `subpath`, walking clockwise.
// `px`/`py` is the sharp corner point, `enter` the unit direction the path
// travels ALONG the incoming edge to reach it, `exit` the direction it leaves
// with, `radius` the corner size in local pixels (0 = sharp) and `type` the
// wire corner type. Geometry pinned by the shp-rect-* probe renders: Round is
// a circular quarter arc, Straight the chamfer between the two offset points,
// Cutout removes the radius-sized corner square, RoundInverse a concave
// quarter arc centered on the corner point itself.
void append_rect_corner(PathSubpath& subpath, double px, double py, double enter_x, double enter_y,
                        double exit_x, double exit_y, double radius, int type) {
  const auto add = [&](double x, double y, bool smooth) -> PathAnchor& {
    subpath.anchors.push_back(PathAnchor{x, y, x, y, x, y, smooth});
    return subpath.anchors.back();
  };
  if (radius <= 0.0 || type == kAfCornerNone) {
    add(px, py, false);
    return;
  }
  const double in_x = px - enter_x * radius;   // radius before the corner
  const double in_y = py - enter_y * radius;
  const double out_x = px + exit_x * radius;   // radius past the corner
  const double out_y = py + exit_y * radius;
  const double k = kQuarterArcK * radius;
  switch (type) {
    case kAfCornerRound: {
      PathAnchor& a = add(in_x, in_y, true);
      a.out_x = in_x + enter_x * k;
      a.out_y = in_y + enter_y * k;
      PathAnchor& b = add(out_x, out_y, true);
      b.in_x = out_x - exit_x * k;
      b.in_y = out_y - exit_y * k;
      break;
    }
    case kAfCornerRoundInverse: {
      PathAnchor& a = add(in_x, in_y, false);
      a.out_x = in_x + exit_x * k;
      a.out_y = in_y + exit_y * k;
      PathAnchor& b = add(out_x, out_y, false);
      b.in_x = out_x - enter_x * k;
      b.in_y = out_y - enter_y * k;
      break;
    }
    case kAfCornerCutout:
      add(in_x, in_y, false);
      add(in_x + out_x - px, in_y + out_y - py, false);
      add(out_x, out_y, false);
      break;
    case kAfCornerStraight:
    default:  // an unknown corner type degrades to its chamfer
      add(in_x, in_y, false);
      add(out_x, out_y, false);
      break;
  }
}

// Builds the local-space path for a parametric shape node's Shpe record, or
// nullopt for shape kinds Patchy does not model (the honest placeholder).
// Wire semantics pinned by the af-spike shp-* one-toggle probes (2026-07-28):
// the record's class tag is the shape kind; ShpB [x0,y0,x1,y1] is the local
// box the geometry fills; ShNR carries ShCR per-corner radii in
// TL/TR/BR/BL order (fraction of min(w,h), or pixels when AbSz), CTyp
// per-corner types, and Lock (absent/true = corner 0's radius AND type render
// on all four corners); ShpE is the inscribed ellipse; ShPy a regular Side-gon
// inscribed in the box ellipse, first vertex up.
//
// The 2026-07-29 long-tail kinds (diamond, trapezoid, pie, segment, crescent,
// heart, tear, arrow, double/square star, cog, cloud, curved-edge stars) are
// pinned by the af-spike shape-curves sweep: every kind was authored through
// the JS bridge at several parameter settings, Convert-to-Curves'd, and the
// exact baked beziers mined (corpus/shape-curves*.json); the constructions
// below reproduce those regions with Patchy's own arc segmentation. Radial
// kinds work in the box-inscribed ellipse's "unit space" (y down), where
// Affinity's own geometry is circles/arcs that the box then stretches.
[[nodiscard]] std::optional<VectorPath> parametric_shape_path(const af::AfClass& node,
                                                              std::string* why) {
  // A linked symbol instance keeps its geometry (Shpe record AND local box)
  // on the defining sibling; the instance's own transform still places it.
  const af::AfClass* geometry_node = &node;
  if (node.child_class(af::tag4("Shpe")) == nullptr) {
    if (const af::AfClass* sibling = symbol_sibling_with(node, af::tag4("Shpe"))) {
      geometry_node = sibling;
    }
  }
  const af::AfClass* shape = geometry_node->child_class(af::tag4("Shpe"));
  if (shape == nullptr) {
    *why = "has no shape record";
    return std::nullopt;
  }
  const auto box = geometry_node->vec_field(af::tag4("ShpB"));
  if (box.size() != 4 || !(box[2] > box[0]) || !(box[3] > box[1])) {
    *why = "has a degenerate shape box";
    return std::nullopt;
  }
  const double x0 = box[0];
  const double y0 = box[1];
  const double x1 = box[2];
  const double y1 = box[3];
  const double w = x1 - x0;
  const double h = y1 - y0;

  VectorPath path;
  PathSubpath subpath;
  subpath.closed = true;

  // Shared helpers for the long-tail kinds: unit space is the box-inscribed
  // ellipse's coordinate system (centre (0,0), radius 1, y down); mapping to
  // the document is an anisotropic scale, which keeps beziers beziers.
  constexpr double kPiSh = 3.14159265358979323846;
  constexpr double kCircleK = 0.5522847498307934;
  const double ucx = (x0 + x1) / 2.0;
  const double ucy = (y0 + y1) / 2.0;
  const double hw = w / 2.0;
  const double hh = h / 2.0;
  const auto unit_x = [&](double ux) { return ucx + hw * ux; };
  const auto unit_y = [&](double uy) { return ucy + hh * uy; };
  const auto push_corner = [](PathSubpath& sp, double px, double py) {
    sp.anchors.push_back(PathAnchor{px, py, px, py, px, py, false});
  };
  const auto push_corner_unit = [&](PathSubpath& sp, double ux, double uy) {
    push_corner(sp, unit_x(ux), unit_y(uy));
  };
  // Append a circular arc (unit space: centre (ax,ay), radius r) from angle
  // a0 to a1, either direction, split into <=90-degree bezier segments. When
  // the subpath already ends at the arc's start point only its out handle is
  // set (so lines and arcs chain); interior anchors are smooth, the arc's
  // endpoints stay corners for the caller to join.
  const auto append_unit_arc = [&](PathSubpath& sp, double ax, double ay, double r, double a0,
                                   double a1) {
    const int segments =
        std::max(1, static_cast<int>(std::ceil(std::abs(a1 - a0) / (kPiSh / 3.0) - 1e-9)));
    const double step = (a1 - a0) / segments;
    const double k = 4.0 / 3.0 * std::tan(step / 4.0) * r;
    for (int i = 0; i <= segments; ++i) {
      const double ang = a0 + step * static_cast<double>(i);
      const double px = unit_x(ax + r * std::cos(ang));
      const double py = unit_y(ay + r * std::sin(ang));
      const double tx = -std::sin(ang) * k * hw;
      const double ty = std::cos(ang) * k * hh;
      if (i == 0) {
        if (!sp.anchors.empty() && std::abs(sp.anchors.back().anchor_x - px) < 1e-6 &&
            std::abs(sp.anchors.back().anchor_y - py) < 1e-6) {
          sp.anchors.back().out_x = px + tx;
          sp.anchors.back().out_y = py + ty;
        } else {
          sp.anchors.push_back(PathAnchor{px, py, px, py, px + tx, py + ty, false});
        }
        continue;
      }
      PathAnchor anchor{px, py, px - tx, py - ty, px, py, i < segments};
      if (i < segments) {
        anchor.out_x = px + tx;
        anchor.out_y = py + ty;
      }
      sp.anchors.push_back(anchor);
    }
  };
  // A closed subpath built from chained arcs ends where it began; fold the
  // duplicate final anchor's incoming handle onto the first anchor.
  const auto close_fold = [](PathSubpath& sp) {
    if (sp.anchors.size() >= 2) {
      const auto& first = sp.anchors.front();
      const auto& last = sp.anchors.back();
      if (std::abs(first.anchor_x - last.anchor_x) < 1e-6 &&
          std::abs(first.anchor_y - last.anchor_y) < 1e-6) {
        sp.anchors.front().in_x = last.in_x;
        sp.anchors.front().in_y = last.in_y;
        sp.anchors.pop_back();
      }
    }
  };
  subpath.op = PathCombineOp::Add;
  subpath.shape_group = 0;

  if (shape->type_tag == af::tag4("ShNR")) {
    auto radii = shape->vec_field(af::tag4("ShCR"));
    radii.resize(4, 0.0);
    auto types_raw = shape->vec_field(af::tag4("CTyp"));
    types_raw.resize(4, static_cast<double>(kAfCornerNone));
    std::array<int, 4> types{};
    for (std::size_t i = 0; i < 4; ++i) {
      types[i] = static_cast<int>(types_raw[i]);
    }
    if (shape->bool_field(af::tag4("Lock"), true)) {
      // Single-radius mode: corner 0 defines every corner.
      radii[1] = radii[2] = radii[3] = radii[0];
      types[1] = types[2] = types[3] = types[0];
    }
    const bool absolute = shape->bool_field(af::tag4("AbSz"), false);
    const double reference = std::min(w, h);
    std::array<double, 4> r{};  // TL, TR, BR, BL in local pixels
    for (std::size_t i = 0; i < 4; ++i) {
      const double value = absolute ? radii[i] : radii[i] * reference;
      r[i] = (types[i] == kAfCornerNone) ? 0.0 : std::max(0.0, value);
    }
    // Adjacent radii cannot exceed their shared edge (the CSS overlap rule;
    // Affinity's exact overflow behavior is unprobed).
    double scale = 1.0;
    const auto limit = [&](double a, double b, double edge) {
      if (a + b > edge && a + b > 0.0) {
        scale = std::min(scale, edge / (a + b));
      }
    };
    limit(r[0], r[1], w);
    limit(r[1], r[2], h);
    limit(r[2], r[3], w);
    limit(r[3], r[0], h);
    for (double& value : r) {
      value *= scale;
    }
    append_rect_corner(subpath, x0, y0, 0.0, -1.0, 1.0, 0.0, r[0], types[0]);
    append_rect_corner(subpath, x1, y0, 1.0, 0.0, 0.0, 1.0, r[1], types[1]);
    append_rect_corner(subpath, x1, y1, 0.0, 1.0, -1.0, 0.0, r[2], types[2]);
    append_rect_corner(subpath, x0, y1, -1.0, 0.0, 0.0, -1.0, r[3], types[3]);
  } else if (shape->type_tag == af::tag4("ShpE")) {
    const double cx = (x0 + x1) / 2.0;
    const double cy = (y0 + y1) / 2.0;
    const double kx = kQuarterArcK * (w / 2.0);
    const double ky = kQuarterArcK * (h / 2.0);
    const auto add = [&](double ax, double ay, double ix, double iy, double ox, double oy) {
      subpath.anchors.push_back(PathAnchor{ax, ay, ix, iy, ox, oy, true});
    };
    add(cx, y0, cx - kx, y0, cx + kx, y0);
    add(x1, cy, x1, cy - ky, x1, cy + ky);
    add(cx, y1, cx + kx, y1, cx - kx, y1);
    add(x0, cy, x0, cy + ky, x0, cy - ky);
  } else if (shape->type_tag == af::tag4("ShPy")) {
    for (const auto& field : shape->fields) {
      if (field.tag != af::tag4("Side") && field.tag != af::tag4("Smth") &&
          field.tag != af::tag4("Curv")) {
        *why = "is a non-regular Affinity polygon";
        return std::nullopt;
      }
    }
    const auto sides = std::clamp<std::int64_t>(shape->int_field(af::tag4("Side"), 5), 3, 256);
    const bool smooth = shape->bool_field(af::tag4("Smth"), false);
    const double cx = (x0 + x1) / 2.0;
    const double cy = (y0 + y1) / 2.0;
    constexpr double kPi = 3.14159265358979323846;
    // Smoothed polygons replace every corner with a symmetric smooth anchor
    // whose tangent length scales with Curv: the shp-polygon6-smooth probe
    // pins Smth=true/Curv=0 as rendering EXACTLY like the plain polygon, so
    // Curv is the rounding amount. Curv=1 is mapped to the circle through
    // the vertices ((4/3)tan(pi/2n) in unit-circle space) - plausible but
    // unpinned, no Curv>0 ground truth exists yet.
    const double curv = std::clamp(shape->double_field(af::tag4("Curv"), 0.0), 0.0, 1.0);
    const double tangent =
        smooth ? curv * (4.0 / 3.0) * std::tan(kPi / (2.0 * static_cast<double>(sides))) : 0.0;
    for (std::int64_t i = 0; i < sides; ++i) {
      const double angle = -kPi / 2.0 + 2.0 * kPi * static_cast<double>(i) /
                                            static_cast<double>(sides);
      const double ux = std::cos(angle);
      const double uy = std::sin(angle);
      const double x = cx + (w / 2.0) * ux;
      const double y = cy + (h / 2.0) * uy;
      if (!smooth) {
        subpath.anchors.push_back(PathAnchor{x, y, x, y, x, y, false});
        continue;
      }
      // The tangent runs perpendicular to the radius (unit space), scaled per
      // axis by the box's half sizes.
      const double tx = -uy * tangent * (w / 2.0);
      const double ty = ux * tangent * (h / 2.0);
      subpath.anchors.push_back(PathAnchor{x, y, x - tx, y - ty, x + tx, y + ty, true});
    }
  } else if (shape->type_tag == af::tag4("ShSt")) {
    // Star: Pnts outer vertices on the box-inscribed ellipse (first vertex
    // up, like ShPy), alternating with inner vertices at the IRad fraction,
    // offset by half a step. CrvL/CrvR only take effect when Lgcy
    // (curvedEdges) is true - the shape-curves sweep pins both a plain-star
    // conversion for Lgcy alone or CrvL/CrvR alone, and tangential tip
    // handles for the combination (star-lgcy-crv, CrvL=CrvR=0.4 -> unit
    // handle 0.18665). Circle-rounded stars (CrcI/CrcO) keep the
    // placeholder until their tangent construction is pinned.
    if (shape->double_field(af::tag4("CrcI"), 0.0) > 1e-6 ||
        shape->double_field(af::tag4("CrcO"), 0.0) > 1e-6) {
      *why = "is a circle-rounded Affinity star";
      return std::nullopt;
    }
    const auto points = std::clamp<std::int64_t>(shape->int_field(af::tag4("Pnts"), 5), 3, 256);
    const double inner = std::clamp(shape->double_field(af::tag4("IRad"), 0.5), 0.0, 1.0);
    const bool curved = shape->bool_field(af::tag4("Lgcy"), false);
    const double curve_left =
        curved ? std::clamp(shape->double_field(af::tag4("CrvL"), 0.0), 0.0, 1.0) : 0.0;
    const double curve_right =
        curved ? std::clamp(shape->double_field(af::tag4("CrvR"), 0.0), 0.0, 1.0) : 0.0;
    // The pinned 0.4 -> 0.18665 sample gives the per-unit-curve handle scale
    // at five points; scale other point counts by their half-step tangent.
    const double handle_scale = 0.46662 * std::tan(kPiSh / static_cast<double>(points)) /
                                std::tan(kPiSh / 5.0);
    const double cx = (x0 + x1) / 2.0;
    const double cy = (y0 + y1) / 2.0;
    constexpr double kPi = 3.14159265358979323846;
    for (std::int64_t i = 0; i < points; ++i) {
      const double outer_angle = -kPi / 2.0 + 2.0 * kPi * static_cast<double>(i) /
                                                  static_cast<double>(points);
      const double inner_angle = outer_angle + kPi / static_cast<double>(points);
      const double ox = cx + (w / 2.0) * std::cos(outer_angle);
      const double oy = cy + (h / 2.0) * std::sin(outer_angle);
      if (curve_left > 0.0 || curve_right > 0.0) {
        // Tangential handles round the tip; CrvL feeds the incoming side.
        const double tx = -std::sin(outer_angle) * hw;
        const double ty = std::cos(outer_angle) * hh;
        subpath.anchors.push_back(PathAnchor{
            ox, oy, ox - tx * curve_left * handle_scale, oy - ty * curve_left * handle_scale,
            ox + tx * curve_right * handle_scale, oy + ty * curve_right * handle_scale, false});
      } else {
        subpath.anchors.push_back(PathAnchor{ox, oy, ox, oy, ox, oy, false});
      }
      const double ix = cx + inner * (w / 2.0) * std::cos(inner_angle);
      const double iy = cy + inner * (h / 2.0) * std::sin(inner_angle);
      subpath.anchors.push_back(PathAnchor{ix, iy, ix, iy, ix, iy, false});
    }
  } else if (shape->type_tag == af::tag4("ShpD")) {
    // Diamond: side vertices sit Pos of the way up the box (default 0.5).
    const double pos = std::clamp(shape->double_field(af::tag4("Pos "), 0.5), 0.0, 1.0);
    const double side_y = y1 - pos * h;
    push_corner(subpath, ucx, y0);
    push_corner(subpath, x1, side_y);
    push_corner(subpath, ucx, y1);
    push_corner(subpath, x0, side_y);
  } else if (shape->type_tag == af::tag4("ShTz")) {
    // Trapezoid: the top edge runs from PosL to PosR across the box.
    const double pos_l = std::clamp(shape->double_field(af::tag4("PosL"), 0.25), 0.0, 1.0);
    const double pos_r = std::clamp(shape->double_field(af::tag4("PosR"), 0.75), 0.0, 1.0);
    push_corner(subpath, x1, y1);
    push_corner(subpath, x0, y1);
    push_corner(subpath, x0 + pos_l * w, y0);
    push_corner(subpath, x0 + pos_r * w, y0);
  } else if (shape->type_tag == af::tag4("ShHt")) {
    // Heart: a fixed six-anchor template in box fractions; only the top
    // cleft anchor moves - its y is Sprd of the way down the box.
    const double spread = std::clamp(shape->double_field(af::tag4("Sprd"), 0.2), 0.0, 1.0);
    struct HeartAnchor {
      double ax, ay, inx, iny, outx, outy;
      bool smooth;
    };
    const HeartAnchor kHeart[6] = {
        {0.5, 0.0, 0.3947370, 0.0, 0.6052630, 0.0, false},
        {0.9210530, 0.1, 0.8157890, 0.0, 1.0263160, 0.2, true},
        {0.9210530, 0.6, 1.0263160, 0.4, 0.8473680, 0.75, true},
        {0.5, 1.0, 0.6578950, 0.9, 0.3421050, 0.9, false},
        {0.0789474, 0.6, 0.1526320, 0.75, -0.0263158, 0.4, true},
        {0.0789474, 0.1, -0.0263158, 0.2, 0.1842110, 0.0, true},
    };
    for (const auto& a : kHeart) {
      const double ay = a.ax == 0.5 && a.ay == 0.0 ? spread : a.ay;
      subpath.anchors.push_back(PathAnchor{x0 + a.ax * w, y0 + ay * h, x0 + a.inx * w,
                                           y0 + a.iny * h, x0 + a.outx * w, y0 + a.outy * h,
                                           a.smooth});
    }
  } else if (shape->type_tag == af::tag4("ShTr")) {
    // Tear: an ellipse whose top anchor collapses into a corner at Tail
    // across the top edge; Curv scales the upper side handles (default 0.3),
    // Bend curls the tail tip (handle template pinned at Tail 0.5). The Fixd
    // and Ball fields do not alter 3.x geometry (sweep-pinned) and are
    // ignored.
    const double tail = std::clamp(shape->double_field(af::tag4("Tail"), 0.5), 0.0, 1.0);
    const double curve = std::clamp(shape->double_field(af::tag4("Curv"), 0.3), 0.0, 1.0);
    const double bend = std::clamp(shape->double_field(af::tag4("Bend"), 0.0), -1.0, 1.0);
    const double tip_x = x0 + tail * w;
    // The bottom bulb is a half-ellipse whose vertical radius caps at half
    // the WIDTH (the tall-box probe pins side anchors at y1 - w/2 there);
    // the side anchors' upper handles are Curv of the way back up to the top.
    const double bulb = std::min(hw, hh);
    const double side_y = y1 - bulb;
    subpath.anchors.push_back(PathAnchor{tip_x, y0, tip_x + bend * w * 0.1,
                                         y0 + bend * h * 0.25, tip_x + bend * w * 0.25, y0,
                                         false});
    subpath.anchors.push_back(PathAnchor{x1, side_y, x1, side_y - curve * (side_y - y0), x1,
                                         side_y + kCircleK * bulb, true});
    subpath.anchors.push_back(PathAnchor{ucx, y1, ucx + kCircleK * hw, y1, ucx - kCircleK * hw,
                                         y1, true});
    subpath.anchors.push_back(PathAnchor{x0, side_y, x0, side_y + kCircleK * bulb, x0,
                                         side_y - curve * (side_y - y0), true});
  } else if (shape->type_tag == af::tag4("ShDA")) {
    // Arrow: a straight-line polygon. Head length is LPr1/RPr1 of the BOX
    // HEIGHT (wide-box pinned), the shaft is Thck of the height, and
    // LPr2/RPr2 offset the shaft junction from the barb base. Only end
    // styles 0 (flat) and 1 (the plain arrowhead) are modeled.
    const auto left_style = enum_field_id(*shape, af::tag4("LSty"), 1);
    const auto right_style = enum_field_id(*shape, af::tag4("RSty"), 1);
    if (left_style > 1 || right_style > 1) {
      *why = "uses an Affinity arrow end style Patchy does not model";
      return std::nullopt;
    }
    const double thick = std::clamp(shape->double_field(af::tag4("Thck"), 0.35), 0.0, 1.0);
    const double shaft_top = ucy - thick * hh;
    const double shaft_bottom = ucy + thick * hh;
    double left_len = std::max(0.0, shape->double_field(af::tag4("LPr1"), 0.5)) * h;
    const double left_inner = shape->double_field(af::tag4("LPr2"), 0.0) * h;
    double right_len = std::max(0.0, shape->double_field(af::tag4("RPr1"), 0.5)) * h;
    const double right_inner = shape->double_field(af::tag4("RPr2"), 0.0) * h;
    // Heads that would overlap scale down proportionally to meet (the
    // tall-box probe: two 30px heads in a 44px box become 22px each).
    const double head_total = (left_style == 1 ? left_len : 0.0) +
                              (right_style == 1 ? right_len : 0.0);
    if (head_total > w && head_total > 0.0) {
      left_len *= w / head_total;
      right_len *= w / head_total;
    }
    const double barb_l = x0 + left_len;
    const double shaft_l = left_style == 1 ? barb_l + left_inner : x0;
    const double barb_r = x1 - right_len;
    const double shaft_r = right_style == 1 ? barb_r - right_inner : x1;
    if (left_style == 1) {
      push_corner(subpath, shaft_l, shaft_bottom);
      push_corner(subpath, barb_l, y1);
      push_corner(subpath, x0, ucy);
      push_corner(subpath, barb_l, y0);
      push_corner(subpath, shaft_l, shaft_top);
    } else {
      push_corner(subpath, x0, shaft_bottom);
      push_corner(subpath, x0, shaft_top);
    }
    if (right_style == 1) {
      push_corner(subpath, shaft_r, shaft_top);
      push_corner(subpath, barb_r, y0);
      push_corner(subpath, x1, ucy);
      push_corner(subpath, barb_r, y1);
      push_corner(subpath, shaft_r, shaft_bottom);
    } else {
      push_corner(subpath, x1, shaft_top);
      push_corner(subpath, x1, shaft_bottom);
    }
  } else if (shape->type_tag == af::tag4("ShPi")) {
    // Pie: the wire angles are radians in y-UP math orientation (AngS
    // default pi/2 = top, AngE default 0 = right); the filled sector runs
    // from AngE to AngS clockwise on screen. IRad > 0 cuts an annulus.
    const double phi_s = -shape->double_field(af::tag4("AngS"), kPiSh / 2.0);
    double phi_e = -shape->double_field(af::tag4("AngE"), 0.0);
    const double inner = std::clamp(shape->double_field(af::tag4("IRad"), 0.0), 0.0, 1.0);
    double sweep = phi_s - phi_e;
    sweep -= std::floor(sweep / (2.0 * kPiSh)) * 2.0 * kPiSh;  // wrap into (0, 2*pi]
    if (sweep < 1e-9) {
      sweep = 2.0 * kPiSh;
    }
    const double phi_end = phi_e + sweep;  // == phi_s modulo 2*pi
    if (inner <= 1e-6) {
      push_corner_unit(subpath, std::cos(phi_end), std::sin(phi_end));
      push_corner_unit(subpath, 0.0, 0.0);
      append_unit_arc(subpath, 0.0, 0.0, 1.0, phi_e, phi_end);
    } else {
      push_corner_unit(subpath, std::cos(phi_end), std::sin(phi_end));
      append_unit_arc(subpath, 0.0, 0.0, inner, phi_end, phi_e);
      append_unit_arc(subpath, 0.0, 0.0, 1.0, phi_e, phi_end);
    }
    close_fold(subpath);
  } else if (shape->type_tag == af::tag4("ShSg")) {
    // Segment: the box ellipse clipped to the band between two chords
    // perpendicular to the Angl direction (unit space): Pos0/Pos1 map to
    // signed offsets 2*Pos-1 along the direction.
    const double angle = shape->double_field(af::tag4("Angl"), kPiSh / 2.0);
    double c0 = std::clamp(2.0 * shape->double_field(af::tag4("Pos0"), 0.25) - 1.0, -1.0, 1.0);
    double c1 = std::clamp(2.0 * shape->double_field(af::tag4("Pos1"), 1.0) - 1.0, -1.0, 1.0);
    if (c0 > c1) {
      std::swap(c0, c1);
    }
    const double alpha0 = std::acos(std::clamp(c0, -1.0, 1.0));
    const double alpha1 = std::acos(std::clamp(c1, -1.0, 1.0));
    if (alpha0 - alpha1 < 1e-6) {
      *why = "has a degenerate shape outline";
      return std::nullopt;
    }
    const double delta = -angle;  // screen angle of the cut direction
    push_corner_unit(subpath, std::cos(delta + alpha0), std::sin(delta + alpha0));
    push_corner_unit(subpath, std::cos(delta - alpha0), std::sin(delta - alpha0));
    if (alpha1 > 1e-6) {
      append_unit_arc(subpath, 0.0, 0.0, 1.0, delta - alpha0, delta - alpha1);
      push_corner_unit(subpath, std::cos(delta + alpha1), std::sin(delta + alpha1));
      append_unit_arc(subpath, 0.0, 0.0, 1.0, delta + alpha1, delta + alpha0);
    } else {
      append_unit_arc(subpath, 0.0, 0.0, 1.0, delta - alpha0, delta + alpha0);
    }
    close_fold(subpath);
  } else if (shape->type_tag == af::tag4("ShCr")) {
    // Crescent: the region between two boundary curves (unit space), each
    // from (0,-1) through (Arc, 0) to (0,1) - ArcL/ArcR are the signed bulge
    // fractions (the JS setters negate leftArc on the wire; the wire values
    // place both boundaries directly). Affinity's boundary is two cubics
    // whose handles the shape-curves sweep pins exactly across four bulges:
    // the endpoint handle is (K*m, (1-|m|)/3) and the mid handle is vertical
    // with length K*|m| + (1-|m|)/3 - the ellipse half at |m|=1, the
    // straight line at m=0, blended linearly between.
    const double arc_l = std::clamp(shape->double_field(af::tag4("ArcL"), -1.0), -1.0, 1.0);
    const double arc_r = std::clamp(shape->double_field(af::tag4("ArcR"), -0.3), -1.0, 1.0);
    if (std::abs(arc_r - arc_l) < 1e-6) {
      *why = "has a degenerate shape outline";
      return std::nullopt;
    }
    // Emits the boundary for bulge m from (0,-1) to (0,1) when down is true,
    // or the reverse; the subpath's last anchor is the start point.
    const auto boundary = [&](double m, bool down) {
      const double s = down ? 1.0 : -1.0;
      if (std::abs(m) < 1e-6) {
        push_corner_unit(subpath, 0.0, s);
        return;
      }
      const double end_off = (1.0 - std::abs(m)) / 3.0;
      const double mid_len = kCircleK * std::abs(m) + end_off;
      auto& start = subpath.anchors.back();
      start.out_x = unit_x(kCircleK * m);
      start.out_y = unit_y(-s + s * end_off);
      subpath.anchors.push_back(PathAnchor{unit_x(m), unit_y(0.0), unit_x(m),
                                           unit_y(-s * mid_len), unit_x(m), unit_y(s * mid_len),
                                           true});
      subpath.anchors.push_back(PathAnchor{unit_x(0.0), unit_y(s), unit_x(kCircleK * m),
                                           unit_y(s - s * end_off), unit_x(0.0), unit_y(s),
                                           false});
    };
    push_corner_unit(subpath, 0.0, -1.0);
    boundary(arc_r, true);
    boundary(arc_l, false);
    close_fold(subpath);
  } else if (shape->type_tag == af::tag4("ShDS")) {
    // Double star: 4*Pnts straight-line vertices at uniform angle steps from
    // the top, radii cycling [1, IRad, PRad, IRad].
    const auto points = std::clamp<std::int64_t>(shape->int_field(af::tag4("Pnts"), 5), 2, 128);
    const double inner = std::clamp(shape->double_field(af::tag4("IRad"), 0.525731), 0.0, 1.0);
    const double point_r = std::clamp(shape->double_field(af::tag4("PRad"), 0.809017), 0.0, 1.0);
    const double radii[4] = {1.0, inner, point_r, inner};
    const std::int64_t count = points * 4;
    for (std::int64_t i = 0; i < count; ++i) {
      const double ang = -kPiSh / 2.0 +
                         2.0 * kPiSh * static_cast<double>(i) / static_cast<double>(count);
      const double r = radii[i % 4];
      push_corner_unit(subpath, r * std::cos(ang), r * std::sin(ang));
    }
  } else if (shape->type_tag == af::tag4("ShSS")) {
    // Square star: Side arms whose tips are flattened; per arm the two outer
    // corners sit at (cos h, +/- (1-COut)*sin h) in tip-local unit space
    // (h = pi/Side), with the inner corner between arms at radius 1-COut.
    const auto sides = std::clamp<std::int64_t>(shape->int_field(af::tag4("Side"), 5), 3, 128);
    const double cutout = std::clamp(shape->double_field(af::tag4("COut"), 0.5), 0.0, 1.0);
    const double inner_r = 1.0 - cutout;
    const double half = kPiSh / static_cast<double>(sides);
    for (std::int64_t k = 0; k < sides; ++k) {
      const double tip = -kPiSh / 2.0 + half +
                         2.0 * kPiSh * static_cast<double>(k) / static_cast<double>(sides);
      const double ct = std::cos(tip);
      const double st = std::sin(tip);
      const double px = std::cos(half);
      const double py = inner_r * std::sin(half);
      push_corner_unit(subpath, ct * px - st * -py, st * px + ct * -py);
      push_corner_unit(subpath, ct * px - st * py, st * px + ct * py);
      push_corner_unit(subpath, inner_r * std::cos(tip + half), inner_r * std::sin(tip + half));
    }
  } else if (shape->type_tag == af::tag4("ShCg")) {
    // Cog: alternating tooth-top arcs (radius 1, width TtSz of the tooth
    // period) and root arcs (radius IRad, width NtSz), joined by straight
    // flanks; Curv bows the flanks (handles at 2/3*Curv along the chord,
    // approximate); Hole cuts a centre ellipse.
    const auto teeth = std::clamp<std::int64_t>(shape->int_field(af::tag4("Teth"), 12), 3, 256);
    const double root_r = std::clamp(shape->double_field(af::tag4("IRad"), 0.85), 0.0, 1.0);
    const double hole = std::clamp(shape->double_field(af::tag4("Hole"), 0.2), 0.0, 1.0);
    const double tooth_size = std::clamp(shape->double_field(af::tag4("TtSz"), 0.37), 0.0, 1.0);
    const double notch_size = std::clamp(shape->double_field(af::tag4("NtSz"), 0.42), 0.0, 1.0);
    const double curve = std::clamp(shape->double_field(af::tag4("Curv"), 0.0), 0.0, 1.0);
    const double period = 2.0 * kPiSh / static_cast<double>(teeth);
    const double half_top = tooth_size * period / 2.0;
    const double half_notch = notch_size * period / 2.0;
    for (std::int64_t k = 0; k < teeth; ++k) {
      const double centre = -kPiSh / 2.0 + period * static_cast<double>(k);
      append_unit_arc(subpath, 0.0, 0.0, 1.0, centre - half_top, centre + half_top);
      append_unit_arc(subpath, 0.0, 0.0, root_r, centre + period / 2.0 - half_notch,
                      centre + period / 2.0 + half_notch);
    }
    close_fold(subpath);
    if (curve > 0.0) {
      // Bow every straight flank: a flank runs between an arc-end anchor
      // (degenerate out handle) and the next arc-start anchor.
      const std::size_t n = subpath.anchors.size();
      for (std::size_t i = 0; i < n; ++i) {
        auto& a = subpath.anchors[i];
        auto& b = subpath.anchors[(i + 1) % n];
        const bool a_flat = a.out_x == a.anchor_x && a.out_y == a.anchor_y;
        const bool b_flat = b.in_x == b.anchor_x && b.in_y == b.anchor_y;
        if (a_flat && b_flat) {
          const double dx = (b.anchor_x - a.anchor_x) * (2.0 / 3.0) * curve;
          const double dy = (b.anchor_y - a.anchor_y) * (2.0 / 3.0) * curve;
          a.out_x = a.anchor_x + dx;
          a.out_y = a.anchor_y + dy;
          b.in_x = b.anchor_x - dx;
          b.in_y = b.anchor_y - dy;
        }
      }
    }
    if (subpath.anchors.size() < 2) {
      *why = "has a degenerate shape outline";
      return std::nullopt;
    }
    path.subpaths.push_back(std::move(subpath));
    if (hole > 1e-6) {
      PathSubpath hole_path;
      hole_path.closed = true;
      hole_path.op = PathCombineOp::Add;
      hole_path.shape_group = 0;
      append_unit_arc(hole_path, 0.0, 0.0, hole, -kPiSh / 2.0, 3.0 * kPiSh / 2.0);
      close_fold(hole_path);
      path.subpaths.push_back(std::move(hole_path));
    }
    return path;
  } else if (shape->type_tag == af::tag4("ShCl")) {
    // Cloud: Bubl bubbles; notches at radius IRad between bumps at radius 1.
    // Each bubble half is a quarter-ellipse in the bump-rotated frame with
    // radial semi-axis 1 - IRad*cos(P/2) and tangential semi-axis
    // IRad*sin(P/2) (wide- and default-probe pinned).
    const auto bubbles = std::clamp<std::int64_t>(shape->int_field(af::tag4("Bubl"), 12), 3, 256);
    const double inner = std::clamp(shape->double_field(af::tag4("IRad"), 0.8164966), 0.01, 1.0);
    const double period = 2.0 * kPiSh / static_cast<double>(bubbles);
    const double radial = std::max(0.0, 1.0 - inner * std::cos(period / 2.0));
    const double tangential = inner * std::sin(period / 2.0);
    for (std::int64_t k = 0; k < bubbles; ++k) {
      const double bump = -kPiSh / 2.0 + period * static_cast<double>(k);
      const double dirx = std::cos(bump);
      const double diry = std::sin(bump);
      const double tanx = -std::sin(bump);
      const double tany = std::cos(bump);
      const double n1x = inner * std::cos(bump - period / 2.0);
      const double n1y = inner * std::sin(bump - period / 2.0);
      const double n2x = inner * std::cos(bump + period / 2.0);
      const double n2y = inner * std::sin(bump + period / 2.0);
      const auto push_or_patch = [&](double ux, double uy, double outx, double outy) {
        const double px = unit_x(ux);
        const double py = unit_y(uy);
        if (!subpath.anchors.empty() && std::abs(subpath.anchors.back().anchor_x - px) < 1e-6 &&
            std::abs(subpath.anchors.back().anchor_y - py) < 1e-6) {
          subpath.anchors.back().out_x = unit_x(outx);
          subpath.anchors.back().out_y = unit_y(outy);
        } else {
          subpath.anchors.push_back(PathAnchor{px, py, px, py, unit_x(outx), unit_y(outy),
                                               false});
        }
      };
      push_or_patch(n1x, n1y, n1x + kCircleK * radial * dirx, n1y + kCircleK * radial * diry);
      subpath.anchors.push_back(PathAnchor{
          unit_x(dirx), unit_y(diry), unit_x(dirx - kCircleK * tangential * tanx),
          unit_y(diry - kCircleK * tangential * tany),
          unit_x(dirx + kCircleK * tangential * tanx),
          unit_y(diry + kCircleK * tangential * tany), true});
      subpath.anchors.push_back(PathAnchor{unit_x(n2x), unit_y(n2y),
                                           unit_x(n2x + kCircleK * radial * dirx),
                                           unit_y(n2y + kCircleK * radial * diry), unit_x(n2x),
                                           unit_y(n2y), false});
    }
    close_fold(subpath);
  } else if (shape->type_tag == af::tag4("ShpT")) {
    // Triangle: apex at the "Pos " fraction across the top edge (default
    // centered), base along the bottom of the box.
    for (const auto& field : shape->fields) {
      if (field.tag != af::tag4("Pos ")) {
        *why = "is an Affinity triangle variant Patchy does not model";
        return std::nullopt;
      }
    }
    const double pos = std::clamp(shape->double_field(af::tag4("Pos "), 0.5), 0.0, 1.0);
    const double ax = x0 + pos * w;
    subpath.anchors.push_back(PathAnchor{ax, y0, ax, y0, ax, y0, false});
    subpath.anchors.push_back(PathAnchor{x1, y1, x1, y1, x1, y1, false});
    subpath.anchors.push_back(PathAnchor{x0, y1, x0, y1, x0, y1, false});
  } else {
    *why = "is an Affinity shape kind Patchy does not model";
    return std::nullopt;
  }

  if (subpath.anchors.size() < 2) {
    *why = "has a degenerate shape outline";
    return std::nullopt;
  }
  path.subpaths.push_back(std::move(subpath));
  return path;
}

// Collect the geometry of a vector-mask adjunct subtree (declared above
// apply_mask_children): a PCrv/ShpN/Comp node contributes its path, a Grup
// recurses its children; every contributor's LOCAL path maps through its own
// Xfrm composed onto the running affine, and each contributor takes its own
// shape_group with PathCombineOp::Add - the union-of-even-odd-shapes rule the
// SVG importer's clipPath handling established. Hidden contributors are
// skipped. `appended`/`unsupported` let the caller distinguish "not vector
// geometry at all" (stay silent) from a partly-decodable mask (drop whole,
// with a notice - a partial mask would clip wrongly).
void append_vector_mask_paths(const af::AfClass& node, const std::array<double, 6>& base,
                              int depth, VectorPath& into, std::int32_t& group,
                              int& appended, int& unsupported) {
  if (depth > 16) {
    ++unsupported;
    return;
  }
  if (!node.bool_field(af::tag4("Visi"), true)) {
    return;
  }
  auto composed = base;
  if (const auto xfrm = node.vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
    std::array<double, 6> own{};
    std::copy(xfrm.begin(), xfrm.end(), own.begin());
    composed = compose_transforms(base, own);
  }
  auto path = vector_path_from_node(node);
  if (!path.has_value() && node.type_tag == af::tag4("ShpN")) {
    std::string why;
    path = parametric_shape_path(node, &why);
  }
  if (path.has_value()) {
    transform_vector_path(
        *path, {composed[0], composed[3], composed[1], composed[4], composed[2], composed[5]});
    for (auto& subpath : path->subpaths) {
      subpath.shape_group = group;
      subpath.op = PathCombineOp::Add;
      into.subpaths.push_back(std::move(subpath));
    }
    ++group;
    ++appended;
    return;
  }
  if (const auto* kids = class_list(node, af::tag4("Chld")); kids != nullptr && !kids->empty()) {
    for (const auto& child : *kids) {
      if (child != nullptr) {
        append_vector_mask_paths(*child, composed, depth + 1, into, group, appended, unsupported);
      }
    }
    return;
  }
  ++unsupported;
}

// ShpN -> shape layer via its parametric outline. Fails (placeholder) with a
// reason for unmodeled shape kinds so the notice stays specific.
[[nodiscard]] std::optional<Layer> build_shape_layer(LayerBuildContext& ctx,
                                                     const af::AfClass& node,
                                                     const std::string& display,
                                                     std::string* why) {
  auto path = parametric_shape_path(node, why);
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    const af::AfClass* shape = node.child_class(af::tag4("Shpe"));
    std::fprintf(stderr, "[af] shape '%s': kind=%08x path=%d why=%s\n", display.c_str(),
                 shape != nullptr ? shape->type_tag : 0U, path.has_value(),
                 why->c_str());
  }
  if (!path.has_value()) {
    return std::nullopt;
  }
  auto layer = build_vector_layer_from_path(ctx, node, display, std::move(*path));
  if (!layer.has_value()) {
    *why = "has no visible fill or stroke";
  }
  return layer;
}

// Artboard detection: old-generation files mark the node with ABEn=true
// (ns-logo, v7); current files store a non-null phrp -> aprp artboard-
// properties class instead (the authored tiny-artboards fixture pins it;
// arkanis-discord/desktop carry the same shape, including artboards nested
// inside transformed groups).
[[nodiscard]] bool is_artboard_node(const af::AfClass& node) {
  return node.type_tag == af::tag4("ShpN") &&
         (node.bool_field(af::tag4("ABEn"), false) ||
          node.child_class(af::tag4("phrp")) != nullptr);
}

// Union of every artboard box under `node` (transformed through the composed
// ancestor affine), for sizing the canvas when the spread stores no SprB.
void accumulate_artboard_bounds(const af::AfClass& node, const std::array<double, 6>& parent,
                                int depth, bool& any, std::array<double, 4>& bounds) {
  if (depth > 64) {
    return;
  }
  const auto* kids = class_list(node, af::tag4("Chld"));
  if (kids == nullptr) {
    return;
  }
  for (const auto& child : *kids) {
    if (child == nullptr) {
      continue;
    }
    auto composed = parent;
    if (const auto xfrm = child->vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
      std::array<double, 6> local{};
      std::copy(xfrm.begin(), xfrm.end(), local.begin());
      composed = compose_transforms(parent, local);
    }
    if (is_artboard_node(*child)) {
      const auto box = child->vec_field(af::tag4("ShpB"));
      if (box.size() == 4) {
        for (const auto& [sx, sy] : {std::pair<double, double>{box[0], box[1]},
                                     {box[2], box[1]},
                                     {box[0], box[3]},
                                     {box[2], box[3]}}) {
          const double px = composed[0] * sx + composed[1] * sy + composed[2];
          const double py = composed[3] * sx + composed[4] * sy + composed[5];
          bounds[0] = any ? std::min(bounds[0], px) : px;
          bounds[1] = any ? std::min(bounds[1], py) : py;
          bounds[2] = any ? std::max(bounds[2], px) : px;
          bounds[3] = any ? std::max(bounds[3], py) : py;
          any = true;
        }
      }
      continue;  // artboards do not nest inside artboards
    }
    accumulate_artboard_bounds(*child, composed, depth + 1, any, bounds);
  }
}

// An artboard: a container whose children live in its local space. It
// imports as a group at its layout position - the artboard's own rectangle
// (usually a solid background, sometimes fill-less for transparent
// artboards) paints as a bottom layer, the artboard Xfrm composes onto every
// descendant, and a rectangular mask clips the group to the artboard bounds
// exactly as Affinity clips artboard content. Pinned by ns-logo.afdesign (a
// grid of nine 1024 px artboards) and the authored tiny-artboards fixture
// (Affinity's own render clips a child rect at the artboard edge).
[[nodiscard]] Layer build_artboard(LayerBuildContext& ctx, const af::AfClass& node,
                                   const std::string& display) {
  Layer group(ctx.document.allocate_layer_id(), display, LayerKind::Group);
  group.set_visible(node.bool_field(af::tag4("Visi"), true));
  group.set_opacity(
      static_cast<float>(std::clamp(node.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0)));
  group.set_blend_mode(BlendMode::PassThrough);

  std::vector<Layer> children;
  {
    // The artboard's own rectangle; the group carries the node's
    // visibility/opacity, so the background keeps defaults.
    std::string why;
    if (auto background = build_shape_layer(ctx, node, display + " Background", &why)) {
      background->set_visible(true);
      background->set_opacity(1.0F);
      children.push_back(std::move(*background));
    }
  }
  const auto saved = ctx.transform;
  if (const auto xfrm = node.vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
    std::array<double, 6> child{};
    std::copy(xfrm.begin(), xfrm.end(), child.begin());
    ctx.transform = compose_transforms(ctx.transform, child);
  }
  if (const auto* kids = class_list(node, af::tag4("Chld"))) {
    build_layers(ctx, *kids, children);
  }
  ctx.transform = saved;
  for (auto& child : children) {
    group.add_child(std::move(child));
  }

  // Clip to the artboard bounds (the ShpB box through the effective
  // transform; artboards are pure translations in practice, so the bounding
  // rect IS the artboard rect).
  const auto box = node.vec_field(af::tag4("ShpB"));
  if (box.size() == 4 && box[2] > box[0] && box[3] > box[1]) {
    double min_x = box[0];
    double min_y = box[1];
    double max_x = box[2];
    double max_y = box[3];
    if (const auto xfrm = node_xfrm(ctx, node); xfrm.size() == 6) {
      bool first = true;
      for (const auto& [sx, sy] : {std::pair<double, double>{box[0], box[1]},
                                   {box[2], box[1]},
                                   {box[0], box[3]},
                                   {box[2], box[3]}}) {
        const double px = xfrm[0] * sx + xfrm[1] * sy + xfrm[2];
        const double py = xfrm[3] * sx + xfrm[4] * sy + xfrm[5];
        min_x = first ? px : std::min(min_x, px);
        max_x = first ? px : std::max(max_x, px);
        min_y = first ? py : std::min(min_y, py);
        max_y = first ? py : std::max(max_y, py);
        first = false;
      }
    }
    const auto mask_x = static_cast<std::int32_t>(std::lround(min_x));
    const auto mask_y = static_cast<std::int32_t>(std::lround(min_y));
    const auto mask_w = static_cast<std::int32_t>(std::lround(max_x - min_x));
    const auto mask_h = static_cast<std::int32_t>(std::lround(max_y - min_y));
    if (mask_w > 0 && mask_h > 0 && mask_w <= kMaxLayerSide && mask_h <= kMaxLayerSide) {
      PixelBuffer plane(mask_w, mask_h, PixelFormat::gray8());
      for (std::int32_t y = 0; y < mask_h; ++y) {
        auto row = plane.row(y);
        std::memset(row.data(), 0xFF, row.size());
      }
      LayerMask mask;
      mask.bounds = Rect{mask_x, mask_y, mask_w, mask_h};
      mask.pixels = std::move(plane);
      mask.default_color = 0;  // content beyond the artboard is clipped away
      group.set_mask(std::move(mask));
    }
  }
  return group;
}

// The inverse-alpha plane for an Erase carrier: 255 - the coverage the layer
// would have painted. Coverage is the compositor's exact formula for a Pixel
// layer (source alpha x Opacity x Fill opacity x its own raster/vector mask),
// so the hole matches the shape Affinity erased, antialiased rim included.
// Clipped to the canvas; default_color 255 leaves everything beyond the
// carrier untouched. nullopt when the carrier erases nothing.
[[nodiscard]] std::optional<LayerMask> erase_mask_from_layer(const Layer& layer, Rect canvas) {
  const PixelBuffer& pixels = std::as_const(layer).pixels();
  const Rect source = layer.bounds();
  const Rect bounds = intersect_rect(source, canvas);
  if (pixels.empty() || bounds.empty() || bounds.width > kMaxLayerSide ||
      bounds.height > kMaxLayerSide) {
    return std::nullopt;
  }
  const auto channels = static_cast<std::int32_t>(pixels.format().channels);
  if (pixels.format().bit_depth != BitDepth::UInt8 || channels < 4) {
    return std::nullopt;
  }
  const float scale = std::clamp(layer.opacity(), 0.0F, 1.0F) *
                      std::clamp(layer.fill_opacity(), 0.0F, 1.0F);
  PixelBuffer plane(bounds.width, bounds.height, PixelFormat::gray8());
  bool erases_anything = false;
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    const auto row = plane.row(y);
    const auto src = pixels.row(bounds.y + y - source.y);
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      const auto sx = static_cast<std::size_t>(bounds.x + x - source.x);
      const float coverage = static_cast<float>(src[sx * static_cast<std::size_t>(channels) + 3U]) /
                             255.0F * scale *
                             layer_mask_alpha_at(layer, bounds.x + x, bounds.y + y);
      const auto keep = static_cast<std::uint8_t>(
          std::lround(std::clamp(1.0F - coverage, 0.0F, 1.0F) * 255.0F));
      row[static_cast<std::size_t>(x)] = keep;
      erases_anything = erases_anything || keep < 255;
    }
  }
  if (!erases_anything) {
    return std::nullopt;
  }
  LayerMask mask;
  mask.bounds = bounds;
  mask.pixels = std::move(plane);
  mask.default_color = 255;
  return mask;
}

// Rewrites each recorded Erase carrier (bottom-up) into an ISOLATED Normal
// group wrapping the sibling layers beneath it, masked by the carrier's
// inverse alpha - the only PSD-expressible construction: a pass-through group
// mask attenuates every child contribution separately (coverage 1-(1-m)^N
// over N stacked children), while the isolated path applies the mask exactly
// once to the merged stack. The carrier itself is dropped. Carriers the
// construction cannot express keep their layer (rendered Normal) + a notice.
void fold_erase_layers(LayerBuildContext& ctx, std::vector<Layer>& out,
                       const std::vector<std::size_t>& slots, EraseFolding folding) {
  constexpr int kMaxEraseFolds = 64;  // bounds wrapper nesting depth
  const Rect canvas{0, 0, ctx.document.width(), ctx.document.height()};
  std::size_t removed = 0;
  int folded = 0;
  for (const auto slot : slots) {
    const std::size_t index = slot - removed;
    if (index >= out.size()) {
      continue;  // defensive: a carrier whose branch emitted nothing
    }
    const Layer& carrier = out[index];
    const std::string name = carrier.name();
    const auto decline = [&](const std::string& why) {
      ctx.notices.push_back("Layer '" + name + "': Affinity blend mode 'Erase' " + why +
                            "; shown as Normal");
    };
    if (!carrier.visible() || carrier.opacity() <= 0.0F) {
      continue;  // erases nothing and paints nothing; leave silently
    }
    if (folding == EraseFolding::Disabled) {
      decline("is not supported inside a clipping group");
      continue;
    }
    if (carrier.kind() != LayerKind::Pixel) {
      decline("is not supported on a group or adjustment layer");
      continue;
    }
    if (carrier.clipped()) {
      decline("is not supported on a clipped layer");
      continue;
    }
    if (index + 1 < out.size() && out[index + 1].clipped()) {
      decline("is not supported on the base of a clipping group");
      continue;
    }
    if (index == 0) {
      decline("has no layers beneath it");
      continue;
    }
    if (folded >= kMaxEraseFolds) {
      decline("exceeds the supported number of Erase layers");
      break;
    }
    auto mask = erase_mask_from_layer(carrier, canvas);
    if (!mask.has_value()) {
      decline("is empty");
      continue;
    }
    const bool had_effects = !carrier.layer_style().empty();
    Layer wrapper(ctx.document.allocate_layer_id(), name + " (Erase)", LayerKind::Group);
    wrapper.set_blend_mode(BlendMode::Normal);  // isolated: the mask applies once
    for (std::size_t k = 0; k < index; ++k) {
      wrapper.add_child(std::move(out[k]));
    }
    wrapper.set_mask(std::move(*mask));
    out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(index) + 1);
    out.insert(out.begin(), std::move(wrapper));
    removed += index;
    ++folded;
    ctx.notices.push_back("Layer '" + name +
                          "': Affinity blend mode 'Erase' imported as a mask on a new group "
                          "holding the layers beneath it");
    if (had_effects) {
      ctx.notices.push_back("Layer '" + name +
                            "': its layer effects were dropped when the Erase was applied");
    }
  }
}

// The node's own outline in document space (PCrv poly-curve or parametric
// ShpN), for use as a crop mask. Mirrors build_vector_layer_from_path's
// transform mapping; all subpaths share one Add group (the even-odd union
// rule the vector-mask adjunct import established).
[[nodiscard]] std::optional<VectorPath> crop_outline_path(LayerBuildContext& ctx,
                                                          const af::AfClass& node) {
  auto path = vector_path_from_node(node);
  if (!path.has_value() && node.type_tag == af::tag4("ShpN")) {
    std::string why;
    path = parametric_shape_path(node, &why);
  }
  if (!path.has_value() || path->subpaths.empty()) {
    return std::nullopt;
  }
  if (const auto xfrm = node_xfrm(ctx, node); xfrm.size() == 6) {
    transform_vector_path(*path,
                          {xfrm[0], xfrm[3], xfrm[1], xfrm[4], xfrm[2], xfrm[5]});
  }
  for (auto& subpath : path->subpaths) {
    subpath.shape_group = 0;
    subpath.op = PathCombineOp::Add;
  }
  return path;
}

// Emit `node` (and, for content layers, its clipped Chld children after it) into
// `out`. Affinity nests clipped layers INSIDE their base layer's child list;
// Patchy models the same thing as clipped siblings above the base.
void build_layers(LayerBuildContext& ctx, const std::vector<std::shared_ptr<af::AfClass>>& children,
                  std::vector<Layer>& out, EraseFolding folding) {
  std::vector<std::size_t> erase_slots;
  for (const auto& child : children) {
    if (child == nullptr) {
      continue;
    }
    if (++ctx.layer_count > LayerBuildContext::kMaxLayers) {
      throw std::runtime_error("Affinity document has an implausible number of layers");
    }
    const af::AfClass& node = *child;
    std::string name = node.string_field(af::tag4("Desc"));
    if (name.empty()) {
      // Placed images with no user-given name: Affinity's Layers panel shows
      // the image resource's file name (IRFN), so mirror that instead of the
      // generic "Layer" (the esdreika wild file's metal_grid.png nodes).
      name = node.string_field(af::tag4("IRFN"));
    }
    const std::string display = name.empty() ? std::string("Layer") : name;
    const std::uint32_t tag = node.type_tag;
    const af::AfClass* bitmap_class = node.child_class(af::tag4("Bitm"));

    // Erase carriers: remember the index this node's own layer will take.
    // Every dispatch branch below pushes it first (clipped children follow),
    // so out.size() here IS that index.
    if (const auto* blnd = node.field(af::tag4("Blnd")); blnd != nullptr) {
      if (const auto* e = std::get_if<af::AfEnum>(&blnd->value);
          e != nullptr && is_erase_blend(*e)) {
        erase_slots.push_back(out.size());
      }
    }

    // `compose` composes this node's own transform onto the child build:
    // children of ANY content node live in its local space (Affinity is a
    // scene graph; arkanis-discord's scaled containers carry children whose
    // transforms exactly invert the parent scale, and ns-logo's artboard
    // grid places children only through the artboard transform).
    const auto emit_clipped_children = [&](const af::AfClass* compose,
                                           bool base_is_placeholder = false) {
      const auto* kids = class_list(node, af::tag4("Chld"));
      if (kids == nullptr || kids->empty()) {
        return;
      }
      const auto saved = ctx.transform;
      if (compose != nullptr) {
        if (const auto xfrm = compose->vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
          std::array<double, 6> child{};
          std::copy(xfrm.begin(), xfrm.end(), child.begin());
          ctx.transform = compose_transforms(ctx.transform, child);
        }
      }
      // The clipped-sibling rule holds across generations (tiny-live-filter,
      // the corpus docs, and the 1.x wild trading cards), with one 1.x
      // exception: children of a base that painted NOTHING emit unclipped
      // there. vh-check's paint-less phone container (doc v4) holds its five
      // painted panels as plain scene-graph children; clipping them to the
      // empty placeholder hid the whole mockup. Modern files keep clipping
      // even to placeholders (tiny-live-filter's exposure adjustment pin).
      const bool clip_children = ctx.document_version >= 10 || !base_is_placeholder;
      std::vector<Layer> clipped;
      build_layers(ctx, *kids, clipped,
                   clip_children ? EraseFolding::Disabled : EraseFolding::Enabled);
      ctx.transform = saved;
      for (auto& layer : clipped) {
        if (clip_children) {
          layer.set_clipped(true);
        }
        out.push_back(std::move(layer));
      }
    };

    const auto emit_placeholder = [&](const std::string& why) {
      ctx.notices.push_back("Layer '" + display + "' " + why +
                            "; imported as an empty placeholder (not rendered)");
      Layer layer(ctx.document.allocate_layer_id(), display, LayerKind::Pixel);
      apply_common(ctx, node, layer, display);
      out.push_back(std::move(layer));
    };

    // Emit a successfully-built vector base (PCrv/ShpN) together with its crop
    // children. Flat children keep the clipped-siblings model. Children that
    // include a GROUP cannot (groups can never be clipped - Photoshop's rule -
    // so the run rendered unclipped, and a HIDDEN container leaked every
    // stacked card of the wild trading-card template). Those containers become
    // an ISOLATED wrapper group carrying the node's own visibility/opacity/
    // blend/effects/masks, the shape's paint as bottom child, the children
    // above it, and the shape outline baked into the group's mask (the crop) -
    // the same PSD-native construction artboards and the Erase fold use.
    const auto emit_vector_base_with_children = [&](Layer&& base_layer) {
      const auto* kids = class_list(node, af::tag4("Chld"));
      if (kids == nullptr || kids->empty()) {
        out.push_back(std::move(base_layer));
        return;
      }
      const auto saved = ctx.transform;
      if (const auto xfrm = node.vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
        std::array<double, 6> child{};
        std::copy(xfrm.begin(), xfrm.end(), child.begin());
        ctx.transform = compose_transforms(ctx.transform, child);
      }
      std::vector<Layer> child_layers;
      build_layers(ctx, *kids, child_layers, EraseFolding::Disabled);
      ctx.transform = saved;
      const auto has_group_child =
          std::any_of(child_layers.begin(), child_layers.end(),
                      [](const Layer& child) { return child.kind() == LayerKind::Group; });
      if (!has_group_child) {
        out.push_back(std::move(base_layer));
        for (auto& child : child_layers) {
          child.set_clipped(true);
          out.push_back(std::move(child));
        }
        return;
      }
      Layer wrapper(ctx.document.allocate_layer_id(), display, LayerKind::Group);
      // Node-level properties live on the wrapper; absent Blnd keeps the
      // constructor's Normal, which is the isolated compositing a crop needs.
      apply_common(ctx, node, wrapper, display);
      apply_mask_children(ctx, node, wrapper, display);
      if (std::as_const(wrapper).vector_mask() == nullptr) {
        if (auto crop = crop_outline_path(ctx, node)) {
          LayerVectorMask mask;
          mask.path = std::move(*crop);
          wrapper.set_vector_mask(std::move(mask));
          update_vector_mask_raster(wrapper, Rect{0, 0, ctx.document.width(), ctx.document.height()});
          // Bake the outline into a raster group mask when the raster mask
          // slot is free: folder records round-trip raster masks natively,
          // while group records never write vmsk blocks (the artboard/Erase
          // construction). An adjunct-occupied slot keeps the vector mask
          // instead (still renders; a PSD save loses only the crop then).
          const auto* baked = std::as_const(wrapper).vector_mask();
          if (!std::as_const(wrapper).mask().has_value() && baked != nullptr && !baked->cache.empty()) {
            LayerMask raster_mask;
            raster_mask.bounds = baked->cache_bounds;
            raster_mask.pixels = baked->cache;
            raster_mask.default_color = 0;  // content beyond the outline is cropped away
            wrapper.clear_vector_mask();
            wrapper.set_mask(std::move(raster_mask));
          }
        }
      }
      // The base keeps only its own paint; the unit properties (visibility,
      // opacity, blend, effects, masks) moved to the wrapper.
      base_layer.set_visible(true);
      base_layer.set_opacity(1.0F);
      base_layer.set_blend_mode(BlendMode::Normal);
      base_layer.layer_style() = LayerStyle{};
      base_layer.clear_mask();
      base_layer.clear_vector_mask();
      wrapper.add_child(std::move(base_layer));
      for (auto& child : child_layers) {
        wrapper.add_child(std::move(child));
      }
      out.push_back(std::move(wrapper));
    };

    // Artboards: containers whose children live in artboard-local space; the
    // artboard's own rectangle renders as a background inside the group and
    // the group clips to the artboard bounds (Affinity clips artboard
    // content). read_document sizes the canvas to the spread bounds so every
    // artboard imports at its layout position.
    if (is_artboard_node(node)) {
      out.push_back(build_artboard(ctx, node, display));
      continue;
    }

    // Adjustment nodes: the kinds Patchy models become real adjustment layers
    // (with their Bitm mask plane); the rest stay placeholders.
    if (is_adjustment_or_filter(tag)) {
      if (auto adjustment = build_adjustment_layer(ctx, node, display)) {
        out.push_back(std::move(*adjustment));
        emit_clipped_children(&node);
        continue;
      }
      emit_placeholder("is an Affinity adjustment or live filter (not applied yet)");
      emit_clipped_children(&node, /*base_is_placeholder=*/true);
      continue;
    }

    // Compound shapes: Affinity bakes the boolean result into the node's own
    // lowercase 'crvs' poly-curve, so the compound imports as ONE exact shape
    // layer with the node's own fill/stroke (per-child ComO ops stay
    // unmined). Without a decodable baked path the children still carry the
    // operand shapes; they import as a group (subtract operands render
    // opaque - approximate, the pre-2026-07 behavior).
    if (tag == af::tag4("Comp")) {
      if (auto compound = build_vector_layer(ctx, node, display)) {
        out.push_back(std::move(*compound));
        continue;
      }
      out.push_back(build_group(ctx, node, name));
      continue;
    }

    // Group: nest children (a group has a child list and no bitmap).
    if (tag == af::tag4("Grup") ||
        (node.field(af::tag4("Chld")) != nullptr && bitmap_class == nullptr &&
         tag != af::tag4("TxtA") && tag != af::tag4("TxtF") && tag != af::tag4("PCrv") &&
         tag != af::tag4("ShpN"))) {
      out.push_back(build_group(ctx, node, name));
      continue;
    }

    if (bitmap_class != nullptr) {
      std::optional<PixelBuffer> pixels;
      bool approximate_color = false;
      bool bitmap_is_mask_plane = false;
      std::string fail_reason;
      std::shared_ptr<const std::vector<std::uint8_t>> original_bytes;
      std::string original_name;
      if (bitmap_class->type_tag == af::tag4("EmbR")) {
        pixels = flatten_embedded(ctx, *bitmap_class, display);
      } else {
        auto decoded = decode_bitmap(ctx.bytes, ctx.container, *bitmap_class, display,
                                     &ctx.notices, &fail_reason);
        if (decoded && !decoded->rgba.empty()) {
          approximate_color = decoded->approximate_color;
          pixels = std::move(decoded->rgba);
          original_bytes = std::move(decoded->original_bytes);
          original_name = std::move(decoded->original_name);
        } else if (decoded && !decoded->mask.empty()) {
          // A node whose own bitmap is a MASK plane is an adjustment or live
          // filter Patchy does not map (live filters follow this shape; their
          // bitmap is the filter's mask, not content).
          bitmap_is_mask_plane = true;
        }
      }
      // The placed quad (source rect corners through the node transform), for
      // the smart-object wrapper when the layer is a pristine placed image.
      std::array<double, 8> quad{};
      std::int32_t source_w = 0;
      std::int32_t source_h = 0;
      auto xfrm = node_xfrm(ctx, node);
      // A MODERN embedded document (EmbR) is CENTER-anchored: the node
      // transform's translation maps the CENTER of the flattened canvas, not
      // its origin. Pinned against Affinity's render of the restaurant-menu
      // corpus doc (version 30), where all three placed swashes land exactly
      // on the canvas corners only under this rule (raw-origin placement put
      // them ~half a canvas low/right). The 1.x generation (versions 3..9)
      // anchors at the ORIGIN instead: center-anchoring ns-splash (v7)
      // measurably worsens its layout against its own thumbnail. Plain
      // Rstr/ImgN rasters keep origin anchoring in every generation
      // (corpus-pinned).
      if (bitmap_class->type_tag == af::tag4("EmbR") && pixels &&
          ctx.document_version >= 20) {
        if (xfrm.size() != 6) {
          xfrm = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
        }
        const double half_w = static_cast<double>(pixels->width()) / 2.0;
        const double half_h = static_cast<double>(pixels->height()) / 2.0;
        xfrm[2] -= xfrm[0] * half_w + xfrm[1] * half_h;
        xfrm[5] -= xfrm[3] * half_w + xfrm[4] * half_h;
      }
      if (pixels) {
        source_w = pixels->width();
        source_h = pixels->height();
        const double xs[4] = {0.0, static_cast<double>(source_w), static_cast<double>(source_w),
                              0.0};
        const double ys[4] = {0.0, 0.0, static_cast<double>(source_h),
                              static_cast<double>(source_h)};
        for (int corner = 0; corner < 4; ++corner) {
          if (xfrm.size() == 6) {
            quad[static_cast<std::size_t>(corner) * 2U] =
                xfrm[0] * xs[corner] + xfrm[1] * ys[corner] + xfrm[2];
            quad[static_cast<std::size_t>(corner) * 2U + 1U] =
                xfrm[3] * xs[corner] + xfrm[4] * ys[corner] + xfrm[5];
          } else {
            quad[static_cast<std::size_t>(corner) * 2U] = xs[corner];
            quad[static_cast<std::size_t>(corner) * 2U + 1U] = ys[corner];
          }
        }
      }
      const auto origin = layer_origin(xfrm);
      std::optional<PlacedRaster> placed;
      if (pixels && origin) {
        placed = PlacedRaster{std::move(*pixels), origin->first, origin->second};
      } else if (pixels) {
        // Scale/rotate transform: rasterize through the affine into an
        // axis-aligned layer (bilinear, pinned against Affinity's render).
        placed = resample_affine(*pixels, xfrm, false);
      }
      if (placed) {
        if (approximate_color) {
          ctx.notices.push_back("Layer '" + display +
                                "': CMYK converted without a color profile (approximate)");
        }
        const std::int32_t w = placed->pixels.width();
        const std::int32_t h = placed->pixels.height();
        Layer layer(ctx.document.allocate_layer_id(), display, std::move(placed->pixels));
        layer.set_bounds(Rect{placed->x, placed->y, w, h});
        apply_common(ctx, node, layer, display);
        const bool wrapped_smart_object = original_bytes != nullptr;
        if (original_bytes != nullptr) {
          attach_placed_smart_object(ctx, layer, std::move(original_bytes), original_name,
                                     source_w, source_h, quad);
        }
        apply_mask_children(ctx, node, layer, display);
        if (environment_variable_is_set("PATCHY_AF_TRACE")) {
          std::size_t opaque = 0;
          const auto& px = layer.pixels();
          for (std::int32_t yy = 0; yy < px.height(); yy += 16) {
            const auto row_span = std::as_const(px).row(yy);
            for (std::int32_t xx = 0; xx < px.width(); xx += 16) {
              opaque += row_span[static_cast<std::size_t>(xx) * 4U + 3U] > 0 ? 1U : 0U;
            }
          }
          std::fprintf(stderr, "[af] raster layer '%s': %dx%d at %d,%d sampled-opaque=%zu so=%d\n",
                       display.c_str(), w, h, placed->x, placed->y, opaque,
                       wrapped_smart_object ? 1 : 0);
        }
        out.push_back(std::move(layer));
        emit_adjunct_adjustments(ctx, node, out);
        emit_clipped_children(&node);
        continue;
      }
      emit_placeholder(
          bitmap_is_mask_plane
              ? "is an Affinity adjustment or live filter (not applied yet)"
              : (!pixels ? (fail_reason.empty() ? "has an unsupported pixel format" : fail_reason)
                         : "has a degenerate transform"));
      emit_clipped_children(&node, /*base_is_placeholder=*/true);
      continue;
    }

    // Text: extract the story into a pending text layer (rendered post-open).
    const bool is_text = tag == af::tag4("TxtA") || tag == af::tag4("TxtF");
    if (is_text) {
      if (auto text_layer = build_text_layer(ctx, node, display)) {
        apply_mask_children(ctx, node, *text_layer, display);
        out.push_back(std::move(*text_layer));
        emit_clipped_children(&node);
        continue;
      }
    }

    // Vector curves: real shape layers with baked pixels.
    const bool is_vector = tag == af::tag4("PCrv");
    if (is_vector) {
      if (auto vector_layer = build_vector_layer(ctx, node, display)) {
        emit_vector_base_with_children(std::move(*vector_layer));
        continue;
      }
    }

    // Parametric shapes: the kinds Patchy models (rectangle with corner
    // radii, ellipse, regular polygon) become shape layers; the rest keep a
    // placeholder naming the reason.
    const bool is_shape = tag == af::tag4("ShpN");
    if (is_shape) {
      std::string why;
      if (auto shape_layer = build_shape_layer(ctx, node, display, &why)) {
        emit_vector_base_with_children(std::move(*shape_layer));
        continue;
      }
      emit_placeholder(why.empty() ? "is an Affinity shape kind Patchy does not model" : why);
      emit_clipped_children(&node, /*base_is_placeholder=*/true);
      continue;
    }

    // Undecodable vector leaves and text whose story shape is missing: named
    // placeholders.
    emit_placeholder(is_text ? "is text content" : (is_vector ? "is vector content"
                                                              : "is unsupported content"));
  }
  if (!erase_slots.empty()) {
    fold_erase_layers(ctx, out, erase_slots, folding);
  }
}

// Read an Affinity color class: its `_col` field is a sized struct of
// little-endian float32 components in 0..1. RGBA classes carry four
// sRGB-encoded channels directly (like the UI); HSLA classes (older-
// generation gradient stops, 2026-07-28 wild sweep) store hue in turns plus
// saturation/lightness and convert through the standard HSL model; CMYK
// classes (five floats c,m,y,k,a - embedded icon docs inside CMYK parents)
// convert with the same naive profile-less (1-ink)(1-k) mix the CMYK raster
// decoder uses. Reading a CMYK payload's first four floats as RGBA painted
// the restaurant-menu icons cyan-green once their embeds started importing.
// Other color classes (Gray, LABA) stay unmapped for now.
[[nodiscard]] std::optional<std::array<float, 4>> read_rgba_color(const af::AfClass* color_class) {
  if (color_class == nullptr) {
    return std::nullopt;
  }
  const auto* field = color_class->field(af::tag4("_col"));
  if (field == nullptr) {
    return std::nullopt;
  }
  const auto* data = std::get_if<std::vector<std::uint8_t>>(&field->value);
  if (data == nullptr || data->size() < 16) {
    return std::nullopt;
  }
  const auto read_component = [&data](int i) {
    const std::uint32_t bits =
        static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U]) |
        (static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U + 1]) << 8U) |
        (static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U + 2]) << 16U) |
        (static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U + 3]) << 24U);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    if (!(value >= 0.0F)) {
      value = 0.0F;  // also catches NaN
    }
    return std::min(value, 1.0F);
  };
  std::array<float, 4> color{};
  for (int i = 0; i < 4; ++i) {
    color[i] = read_component(i);
  }
  if (color_class->type_tag == af::tag4("CMYK") && data->size() >= 20) {
    const float k = color[3];
    const std::array<float, 4> converted{(1.0F - color[0]) * (1.0F - k),
                                         (1.0F - color[1]) * (1.0F - k),
                                         (1.0F - color[2]) * (1.0F - k), read_component(4)};
    return converted;
  }
  if (color_class->type_tag == af::tag4("HSLA")) {
    const float hue = color[0] - std::floor(color[0]);  // turns, wrapped
    const float saturation = color[1];
    const float lightness = color[2];
    const float chroma = (1.0F - std::abs(2.0F * lightness - 1.0F)) * saturation;
    const float segment = hue * 6.0F;
    const float x = chroma * (1.0F - std::abs(std::fmod(segment, 2.0F) - 1.0F));
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    switch (static_cast<int>(segment)) {
      case 0: r = chroma; g = x; break;
      case 1: r = x; g = chroma; break;
      case 2: g = chroma; b = x; break;
      case 3: g = x; b = chroma; break;
      case 4: r = x; b = chroma; break;
      default: r = chroma; b = x; break;
    }
    const float m = lightness - chroma / 2.0F;
    color[0] = std::clamp(r + m, 0.0F, 1.0F);
    color[1] = std::clamp(g + m, 0.0F, 1.0F);
    color[2] = std::clamp(b + m, 0.0F, 1.0F);
  }
  return color;
}

// True when at least one layer in the tree carries decoded pixels or a pending
// text render (real content once the post-open pass runs). An import where
// every node degraded to an empty placeholder renders as a blank canvas, which
// is worse than the tier-0 embedded preview; read_container checks this and
// prefers the preview for such documents.
[[nodiscard]] bool any_layer_has_pixels(const std::vector<Layer>& layers) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Pixel && !layer.pixels().empty()) {
      return true;
    }
    if (layer.metadata().contains(kLayerMetadataAfPendingText)) {
      return true;
    }
    if (any_layer_has_pixels(layer.children())) {
      return true;
    }
  }
  return false;
}

// Build a full tier-1 document from the parsed tree. Throws on a structurally
// unusable tree; the caller then falls back to the tier-0 preview.
[[nodiscard]] Document build_tier1(std::span<const std::uint8_t> bytes, const Container& container,
                                   const af::AfDocument& tree, std::vector<std::string>& notices,
                                   int embed_depth, bool* placeholders_only = nullptr) {
  if (tree.root == nullptr) {
    throw std::runtime_error("Affinity document tree is empty");
  }
  // root (Pers) -> DocR field -> document node (DfSz + Chld=[spread]).
  const af::AfClass* doc_node = tree.root->child_class(af::tag4("DocR"));
  if (doc_node == nullptr) {
    throw std::runtime_error("Affinity document has no document node");
  }
  // The document node carries DfSz [w,h]; the spread carries the layer children.
  const auto size = doc_node->vec_field(af::tag4("DfSz"));
  if (size.size() != 2) {
    throw std::runtime_error("Affinity document has no canvas size");
  }
  std::int32_t width = static_cast<std::int32_t>(std::lround(size[0]));
  std::int32_t height = static_cast<std::int32_t>(std::lround(size[1]));
  if (width <= 0 || height <= 0 || width > kMaxLayerSide || height > kMaxLayerSide) {
    throw std::runtime_error("Affinity document has an invalid canvas size");
  }
  // The document's Chld is the spread(s); each spread's Chld is the layers.
  const auto* doc_children = doc_node->field(af::tag4("Chld"));
  const auto* spreads =
      doc_children != nullptr
          ? std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&doc_children->value)
          : nullptr;
  if (spreads == nullptr || spreads->empty() || spreads->front() == nullptr) {
    throw std::runtime_error("Affinity document has no spread");
  }
  if (spreads->size() > 1) {
    notices.push_back("This document has " + std::to_string(spreads->size()) +
                      " pages/artboards; only the first was imported");
  }
  const af::AfClass& spread = *spreads->front();
  const auto* spread_children = spread.field(af::tag4("Chld"));
  const auto* layers =
      spread_children != nullptr
          ? std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&spread_children->value)
          : nullptr;

  // The first spread's bounds (SprB) are the canvas whenever they are
  // present and sane. DfSz is only a fallback: 2.x-era files routinely
  // store the New Document preset size there rather than the current canvas
  // (a wild favicon: DfSz 1920x1080, SprB 800x800, and Affinity's own
  // thumbnail is square; across the 2026-07 wild sweeps every DfSz/SprB
  // mismatch resolves to SprB by thumbnail aspect). Artboard spreads lay
  // every artboard out side by side, so when SprB is missing the canvas is
  // the union of the artboard boxes; the whole build then shifts by the
  // spread origin. Affinity's own export of the tiny-artboards fixture pins
  // the union-canvas shape. Photo-persona spreads (2.x .afphoto and the
  // scripted 3.x fixtures) store NO SprB at all; their page rect lives in
  // SpMd -> PagR[0] (PgIn) -> rctp, which tracks canvas resizes while DfSz
  // keeps the creation size (a wild resized document: DfSz 512x512, rctp
  // 0,0,1024,1024, and Affinity shows 1024; every committed fixture's rctp
  // equals its true canvas, including the artboard union).
  double origin_x = 0.0;
  double origin_y = 0.0;
  {
    auto bounds = spread.vec_field(af::tag4("SprB"));
    if (bounds.size() != 4 || !(bounds[2] > bounds[0]) || !(bounds[3] > bounds[1])) {
      bool any_artboard = false;
      std::array<double, 4> computed{};
      accumulate_artboard_bounds(spread, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0}, 0, any_artboard,
                                 computed);
      bounds.clear();
      if (any_artboard) {
        bounds.assign(computed.begin(), computed.end());
      }
    }
    if (bounds.size() != 4) {
      if (const af::AfClass* spread_metadata = spread.child_class(af::tag4("SpMd"))) {
        if (const auto* pages = class_list(*spread_metadata, af::tag4("PagR"));
            pages != nullptr && !pages->empty() && pages->front() != nullptr) {
          auto page_rect = pages->front()->vec_field(af::tag4("rctp"));
          if (page_rect.size() == 4 && page_rect[2] > page_rect[0] &&
              page_rect[3] > page_rect[1]) {
            bounds = std::move(page_rect);
          }
        }
      }
    }
    if (bounds.size() == 4) {
      const auto spread_w = static_cast<std::int32_t>(std::lround(bounds[2] - bounds[0]));
      const auto spread_h = static_cast<std::int32_t>(std::lround(bounds[3] - bounds[1]));
      if (spread_w > 0 && spread_h > 0 && spread_w <= kMaxLayerSide &&
          spread_h <= kMaxLayerSide) {
        width = spread_w;
        height = spread_h;
        origin_x = bounds[0];
        origin_y = bounds[1];
      }
    }
  }

  Document document(width, height, PixelFormat::rgba8());
  // Document resolution first (the root's units block stores pixels-per-inch):
  // placed smart objects record it in their placement.
  if (const af::AfClass* units = tree.root->child_class(af::tag4("UVCn"))) {
    const double ppi = units->double_field(af::tag4("UPPI"), 0.0);
    if (ppi >= 1.0 && ppi <= 10000.0) {
      document.print_settings().horizontal_ppi = ppi;
      document.print_settings().vertical_ppi = ppi;
    }
  }
  std::vector<Layer> built;
  if (layers != nullptr) {
    LayerBuildContext ctx{bytes, container, document, notices, embed_depth};
    ctx.document_version = tree.document_version;
    ctx.transform[2] = -origin_x;
    ctx.transform[5] = -origin_y;
    build_layers(ctx, *layers, built);
  }
  if (placeholders_only != nullptr) {
    *placeholders_only = !any_layer_has_pixels(built);
  }

  // Spread background: unless the spread is transparent (SprT), Affinity
  // paints its background color (BgrC, default white) behind every layer and
  // composites it into its own exports; mirror that with a bottom fill layer.
  // It is added BEFORE `built` deliberately: Affinity's spread background is
  // not a layer, so an Erase wrapper folded inside `built` must not cover it.
  if (!spread.bool_field(af::tag4("SprT"), false)) {
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    if (const auto stored = read_rgba_color(spread.child_class(af::tag4("BgrC")))) {
      color = *stored;
    }
    if (color[3] > 0.0F) {
      PixelBuffer fill(width, height, PixelFormat::rgba8());
      std::array<std::uint8_t, 4> rgba{};
      for (int i = 0; i < 4; ++i) {
        rgba[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(std::lround(color[static_cast<std::size_t>(i)] * 255.0F));
      }
      for (std::int32_t y = 0; y < height; ++y) {
        auto row = fill.row(y);
        for (std::int32_t x = 0; x < width; ++x) {
          std::memcpy(row.data() + static_cast<std::size_t>(x) * 4U, rgba.data(), 4U);
        }
      }
      document.add_pixel_layer("Background", std::move(fill));
    }
  }
  for (auto& layer : built) {
    document.add_layer(std::move(layer));
  }
  if (document.layers().empty()) {
    throw std::runtime_error("Affinity document produced no layers");
  }
  return document;
}

}  // namespace

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 0xFF && bytes[2] == 0x4B &&
         bytes[3] == 0x41;
}

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return sniff(bytes);
}

namespace {

// Tier-0 fallback: import just the embedded preview as one layer.
[[nodiscard]] Document read_preview_only(std::span<const std::uint8_t> bytes,
                                         const Container& container, std::uint32_t document_version,
                                         std::vector<std::string>& notices) {
  PixelBuffer preview = extract_preview(bytes, container);
  Document document(preview.width(), preview.height(), PixelFormat::rgba8());
  document.add_pixel_layer("Affinity preview", std::move(preview));
  std::string summary = "Imported the embedded Affinity preview only (" +
                        std::to_string(document.width()) + "x" + std::to_string(document.height()) +
                        "; the document's layers could not be decoded)";
  if (document_version != 0) {
    summary += "; document format version " + std::to_string(document_version);
  }
  notices.push_back(std::move(summary));
  return document;
}

// Old-generation files SAVE their snapshot caches: the root's Snap subtree
// holds a nested document whose Rstr DyBm names a full-resolution PNG render
// of the whole document in a c/<n> stream (modern files save these slots
// empty). When the tree parses but nothing decodes to pixels, that baked
// render beats the small embedded thumbnail. The snapshot is a CACHE and can
// lag the document's latest edits (fladder-banner2's logo sits elsewhere in
// its snapshot than its node transform says), so it is a fallback only.
[[nodiscard]] std::optional<Document> read_snapshot_render(std::span<const std::uint8_t> bytes,
                                                           const Container& container,
                                                           const af::AfDocument& tree,
                                                           std::uint32_t document_version,
                                                           std::vector<std::string>& notices) {
  if (tree.root == nullptr) {
    return std::nullopt;
  }
  const auto* snap_field = tree.root->field(af::tag4("Snap"));
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::fprintf(stderr, "[af] snapshot: field=%d\n", snap_field != nullptr);
  }
  if (snap_field == nullptr) {
    return std::nullopt;
  }
  std::vector<const af::AfClass*> pending;
  if (const auto* single = std::get_if<std::shared_ptr<af::AfClass>>(&snap_field->value)) {
    pending.push_back(single->get());
  } else if (const auto* list =
                 std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&snap_field->value)) {
    for (const auto& item : *list) {
      pending.push_back(item.get());
    }
  }
  // The largest decodable render wins (the full-document slot; other DyBms
  // under the snapshot are per-layer caches).
  std::optional<PixelBuffer> best;
  int guard = 0;
  while (!pending.empty()) {
    const af::AfClass* node = pending.back();
    pending.pop_back();
    if (node == nullptr) {
      continue;
    }
    if (++guard > 4096) {
      break;
    }
    if (node->type_tag == af::tag4("DyBm")) {
      auto original = decode_embedded_original(bytes, container, *node);
      if (environment_variable_is_set("PATCHY_AF_TRACE")) {
        std::fprintf(stderr, "[af] snapshot: dybm decoded=%d size=%dx%d\n", original.has_value(),
                     original ? original->pixels.width() : 0,
                     original ? original->pixels.height() : 0);
      }
      if (original) {
        if (!original->pixels.empty() &&
            (!best || static_cast<std::int64_t>(original->pixels.width()) *
                              original->pixels.height() >
                          static_cast<std::int64_t>(best->width()) * best->height())) {
          best = std::move(original->pixels);
        }
      }
      continue;
    }
    for (const auto& field : node->fields) {
      if (const auto* child = std::get_if<std::shared_ptr<af::AfClass>>(&field.value)) {
        pending.push_back(child->get());
      } else if (const auto* list =
                     std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&field.value)) {
        for (const auto& item : *list) {
          pending.push_back(item.get());
        }
      }
    }
  }
  if (!best) {
    return std::nullopt;
  }
  Document document(best->width(), best->height(), PixelFormat::rgba8());
  document.add_pixel_layer("Affinity preview", std::move(*best));
  std::string summary = "Imported the document's saved snapshot render only (" +
                        std::to_string(document.width()) + "x" + std::to_string(document.height()) +
                        "; the document's layers could not be decoded)";
  if (document_version != 0) {
    summary += "; document format version " + std::to_string(document_version);
  }
  notices.push_back(std::move(summary));
  return document;
}

// Affinity's Layers panel opens every document with its groups closed, and the
// .af tree records no disclosure state to restore (a Grup node carries only
// Visi/Edtb/MEtb/TrFV/deco, and the container holds no UI-state stream), so
// Affinity is applying a fixed closed default rather than reloading anything.
// Patchy's default for a group with no recorded state is expanded, so match the
// file's appearance in Affinity by stamping every imported group closed. This
// runs as a sweep over the finished tree so it also covers the groups the
// importer synthesizes itself: artboards, Erase folds, and crop-to-shape
// wrappers. The flag is native PSD (the folder's section-divider type), so it
// survives a save.
void collapse_imported_groups(std::vector<Layer>& layers) {
  for (auto& layer : layers) {
    if (layer.kind() != LayerKind::Group) {
      continue;
    }
    set_layer_group_expanded(layer, false);
    collapse_imported_groups(layer.children());
  }
}

// Shared by the public read() and the embedded-document recursion (embedded
// containers are complete .af containers stored in edc/<n> streams).
// Resolve every pending Gaussian-blur layer effect (stamped by
// apply_layer_effects) now that layer pixels are final: blur through the
// calibrated smart-filter kernel (Affinity's wire radius maps 1:1 onto
// document px), honoring the effect's opacity (blend back over the sharp
// original) and preserve-alpha flag (blur stays inside the original
// coverage). Pending text layers render post-open, after this pass can see
// them, so they keep a notice instead.
void bake_pending_blur_effects(std::vector<Layer>& layers, std::vector<std::string>& notices) {
  for (auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      bake_pending_blur_effects(layer.children(), notices);
    }
    auto& metadata = layer.metadata();
    const auto entry = metadata.find(kAfPendingBlurKey);
    if (entry == metadata.end()) {
      continue;
    }
    double radius = 0.0;
    double opacity = 1.0;
    int preserve_alpha = 0;
    {
      char* end = nullptr;
      radius = std::strtod(entry->second.c_str(), &end);
      if (end != nullptr) {
        opacity = std::strtod(end, &end);
      }
      if (end != nullptr) {
        preserve_alpha = static_cast<int>(std::strtol(end, nullptr, 10));
      }
    }
    metadata.erase(entry);
    if (metadata.contains(kLayerMetadataAfPendingText)) {
      notices.push_back("Layer '" + layer.name() +
                        "': Gaussian blur layer effect is not applied to text");
      continue;
    }
    const auto& source = std::as_const(layer).pixels();
    if (source.empty() || radius <= 0.0) {
      continue;
    }
    const Rect source_bounds = layer.bounds();
    // The smart-filter blur keeps the supplied bounds (edge-repeat outside),
    // but Affinity's effect diffuses the layer's alpha boundary too - pad
    // with transparency so the edges soften and the bounds grow with the
    // spill.
    const std::int32_t pad =
        static_cast<std::int32_t>(std::ceil(radius)) * 3 + 1;
    PixelBuffer padded(source.width() + 2 * pad, source.height() + 2 * pad,
                       PixelFormat::rgba8());
    for (std::int32_t y = 0; y < source.height(); ++y) {
      const auto src_row = std::as_const(source).row(y);
      auto dst_row = padded.row(y + pad);
      std::copy(src_row.begin(), src_row.end(),
                dst_row.begin() + static_cast<std::size_t>(pad) * 4U);
    }
    const Rect padded_bounds{source_bounds.x - pad, source_bounds.y - pad, padded.width(),
                             padded.height()};
    // Affinity's wire radius measures 2 sigma on its own renders while the
    // Photoshop-calibrated kernel's radius measures ~1 sigma (edge-profile
    // fit of the fx-gaussian probe), so halve it.
    auto blurred = render_photoshop_gaussian_blur(padded, padded_bounds, radius * 0.5);
    if (blurred.pixels.empty()) {
      continue;
    }
    if (opacity < 1.0) {
      // The effect blends over the sharp original at its opacity.
      auto& px = blurred.pixels;
      for (std::int32_t y = 0; y < px.height(); ++y) {
        auto row = px.row(y);
        const std::int32_t sy = blurred.bounds.y + y - source_bounds.y;
        for (std::int32_t x = 0; x < px.width(); ++x) {
          const std::int32_t sx = blurred.bounds.x + x - source_bounds.x;
          std::uint8_t original[4] = {0, 0, 0, 0};
          if (sx >= 0 && sy >= 0 && sx < source.width() && sy < source.height()) {
            const std::uint8_t* sp = source.pixel(sx, sy);
            std::copy(sp, sp + 4, original);
          }
          std::uint8_t* bp = row.data() + static_cast<std::size_t>(x) * 4U;
          // Mix in premultiplied space so transparent-area colors stay inert.
          const double ba = bp[3] / 255.0;
          const double oa = original[3] / 255.0;
          const double out_a = ba * opacity + oa * (1.0 - opacity);
          for (int c = 0; c < 3; ++c) {
            const double mixed =
                bp[c] * ba * opacity + original[c] * oa * (1.0 - opacity);
            bp[c] = static_cast<std::uint8_t>(std::lround(
                out_a > 0.0 ? std::clamp(mixed / out_a, 0.0, 255.0) : 0.0));
          }
          bp[3] = static_cast<std::uint8_t>(std::lround(std::clamp(out_a * 255.0, 0.0, 255.0)));
        }
      }
    }
    if (preserve_alpha != 0) {
      // Preserve Alpha keeps the original coverage: crop the blur back to the
      // source bounds and reinstate the original alpha plane.
      PixelBuffer kept(source.width(), source.height(), PixelFormat::rgba8());
      for (std::int32_t y = 0; y < kept.height(); ++y) {
        auto row = kept.row(y);
        const std::int32_t by = source_bounds.y + y - blurred.bounds.y;
        for (std::int32_t x = 0; x < kept.width(); ++x) {
          std::uint8_t* kp = row.data() + static_cast<std::size_t>(x) * 4U;
          const std::int32_t bx = source_bounds.x + x - blurred.bounds.x;
          if (bx >= 0 && by >= 0 && bx < blurred.pixels.width() && by < blurred.pixels.height()) {
            const std::uint8_t* bp = blurred.pixels.pixel(bx, by);
            std::copy(bp, bp + 3, kp);
          }
          kp[3] = source.pixel(x, y)[3];
        }
      }
      layer.set_pixels(std::move(kept));
      notices.push_back("Layer '" + layer.name() +
                        "': Gaussian blur layer effect baked into the layer pixels");
      continue;
    }
    layer.set_pixels(std::move(blurred.pixels));
    layer.set_bounds(blurred.bounds);
    notices.push_back("Layer '" + layer.name() +
                      "': Gaussian blur layer effect baked into the layer pixels");
  }
}

[[nodiscard]] Document read_container(std::span<const std::uint8_t> bytes,
                                      std::vector<std::string>& notices, int embed_depth) {
  if (!sniff(bytes)) {
    throw std::runtime_error("Not an Affinity document");
  }
  const Container container = parse_container(bytes);
  if (container.version > kNewestVerifiedContainerVersion) {
    notices.push_back("This file was saved by a newer Affinity (container version " +
                      std::to_string(container.version) + "; Patchy has been verified up to " +
                      std::to_string(kNewestVerifiedContainerVersion) +
                      "); the layer import may be incomplete");
  }

  std::uint32_t document_version = 0;
  const auto doc_stream = container.streams.find("doc.dat");
  if (doc_stream != container.streams.end()) {
    try {
      const auto tree_bytes = extract_stream(bytes, doc_stream->second, "doc.dat", &notices);
      const af::AfDocument tree = af::parse_tree(std::span<const std::uint8_t>(tree_bytes));
      document_version = tree.document_version;
      bool placeholders_only = false;
      Document document = build_tier1(bytes, container, tree, notices, embed_depth,
                                      &placeholders_only);
      if (placeholders_only) {
        // Nothing decoded to pixels (all placeholders); a flat render shows
        // the user their document where the structural import would show a
        // blank canvas. Old files save a full-resolution snapshot render,
        // which beats the small embedded thumbnail; keep the structural
        // result only when neither exists.
        if (auto snapshot =
                read_snapshot_render(bytes, container, tree, document_version, notices)) {
          return std::move(*snapshot);
        }
        try {
          return read_preview_only(bytes, container, document_version, notices);
        } catch (const std::exception&) {
        }
      }
      bake_pending_blur_effects(document.layers(), notices);
      collapse_imported_groups(document.layers());
      return document;
    } catch (const std::exception& error) {
      // A structurally unusable tree (or an unforeseen shape) falls back to the
      // preview rather than failing the open. Record why for diagnosis.
      notices.emplace_back(std::string("Full layer import failed (") + error.what() +
                           "); fell back to the embedded preview");
      if (environment_variable_is_set("PATCHY_AF_TRACE")) {
        std::fprintf(stderr, "[af] tier-1 import threw: %s\n", error.what());
      }
    }
  }
  return read_preview_only(bytes, container, document_version, notices);
}

}  // namespace

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  std::vector<std::string> pending;
  Document document = read_container(bytes, pending, 0);
  if (notices != nullptr) {
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
