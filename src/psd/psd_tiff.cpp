#include "psd/psd_tiff.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_io_internal.hpp"
#include "psd/psd_little_endian.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>

namespace patchy::psd {
namespace {

constexpr std::string_view kDocumentDataHeader{"Adobe Photoshop Document Data Block\0", 36};
constexpr std::string_view kLargeDocumentDataHeader{"Adobe Photoshop Document Data V0002\0", 36};

constexpr std::uint16_t kTagNewSubfileType = 254;
constexpr std::uint16_t kTagImageWidth = 256;
constexpr std::uint16_t kTagImageLength = 257;
constexpr std::uint16_t kTagBitsPerSample = 258;
constexpr std::uint16_t kTagPhotometric = 262;
constexpr std::uint16_t kTagXResolution = 282;
constexpr std::uint16_t kTagYResolution = 283;
constexpr std::uint16_t kTagResolutionUnit = 296;
constexpr std::uint16_t kTagSampleFormat = 339;
constexpr std::uint16_t kTagPhotoshopResources = 34377;
constexpr std::uint16_t kTagIccProfile = 34675;
constexpr std::uint16_t kTagImageSourceData = 37724;

constexpr std::size_t kMaxDirectoryEntries = 4096;
constexpr std::size_t kMaxPages = 65536;

// Bounds-checked reads in the TIFF's byte order.
class TiffBytes {
 public:
  TiffBytes(std::span<const std::uint8_t> bytes, bool little_endian) : bytes_(bytes), little_endian_(little_endian) {}

  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] bool little_endian() const noexcept { return little_endian_; }
  [[nodiscard]] bool fits(std::uint64_t offset, std::uint64_t count) const noexcept {
    return offset <= bytes_.size() && count <= bytes_.size() - offset;
  }
  [[nodiscard]] std::uint64_t value(std::size_t offset, std::size_t width) const noexcept {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < width; ++index) {
      const auto byte = static_cast<std::uint64_t>(bytes_[offset + index]);
      result |= little_endian_ ? byte << (8U * index) : byte << (8U * (width - 1U - index));
    }
    return result;
  }
  [[nodiscard]] std::uint16_t u16(std::size_t offset) const noexcept {
    return static_cast<std::uint16_t>(value(offset, 2));
  }
  [[nodiscard]] std::uint32_t u32(std::size_t offset) const noexcept {
    return static_cast<std::uint32_t>(value(offset, 4));
  }
  [[nodiscard]] std::span<const std::uint8_t> span(std::size_t offset, std::size_t count) const noexcept {
    return bytes_.subspan(offset, count);
  }

 private:
  std::span<const std::uint8_t> bytes_;
  bool little_endian_;
};

std::optional<bool> tiff_little_endian(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < 8U) {
    return std::nullopt;
  }
  if (bytes[0] == 'I' && bytes[1] == 'I' && bytes[2] == 42 && bytes[3] == 0) {
    return true;
  }
  if (bytes[0] == 'M' && bytes[1] == 'M' && bytes[2] == 0 && bytes[3] == 42) {
    return false;
  }
  return std::nullopt;  // not TIFF, or BigTIFF (Photoshop never writes layers there)
}

std::size_t type_size(std::uint16_t type) noexcept {
  switch (type) {
    case 1: case 2: case 6: case 7: return 1;
    case 3: case 8: return 2;
    case 4: case 9: case 11: case 13: return 4;
    case 5: case 10: case 12: return 8;
    default: return 0;
  }
}

struct Entry {
  std::uint16_t type{0};
  std::uint32_t count{0};
  std::size_t offset{0};  // where the value bytes start
  std::size_t size{0};
};

struct Directory {
  std::vector<std::pair<std::uint16_t, Entry>> entries;
  std::uint32_t next{0};

  [[nodiscard]] const Entry* find(std::uint16_t tag) const noexcept {
    for (const auto& [entry_tag, entry] : entries) {
      if (entry_tag == tag) {
        return &entry;
      }
    }
    return nullptr;
  }
};

// One directory; entries whose value bytes fall outside the file are left out.
std::optional<Directory> read_directory(const TiffBytes& tiff, std::uint32_t offset) {
  if (!tiff.fits(offset, 2)) {
    return std::nullopt;
  }
  const auto count = tiff.u16(offset);
  if (count == 0U || count > kMaxDirectoryEntries || !tiff.fits(offset + 2ULL, count * 12ULL + 4ULL)) {
    return std::nullopt;
  }
  Directory directory;
  for (std::size_t index = 0; index < count; ++index) {
    const auto at = offset + 2U + index * 12U;
    Entry entry;
    const auto tag = tiff.u16(at);
    entry.type = tiff.u16(at + 2U);
    entry.count = tiff.u32(at + 4U);
    const auto width = type_size(entry.type);
    if (width == 0U) {
      continue;
    }
    const auto size = static_cast<std::uint64_t>(width) * entry.count;
    const std::uint64_t value_offset = size <= 4U ? at + 8U : tiff.u32(at + 8U);
    if (!tiff.fits(value_offset, size)) {
      continue;
    }
    entry.offset = static_cast<std::size_t>(value_offset);
    entry.size = static_cast<std::size_t>(size);
    directory.entries.emplace_back(tag, entry);
  }
  directory.next = tiff.u32(offset + 2U + count * 12U);
  return directory;
}

std::optional<std::uint32_t> unsigned_value(const TiffBytes& tiff, const Entry* entry, std::uint32_t index = 0) {
  if (entry == nullptr || index >= entry->count) {
    return std::nullopt;
  }
  switch (entry->type) {
    case 1: return tiff.span(entry->offset + index, 1)[0];
    case 3: return tiff.u16(entry->offset + index * 2U);
    case 4: return tiff.u32(entry->offset + index * 4U);
    default: return std::nullopt;
  }
}

std::optional<double> rational_value(const TiffBytes& tiff, const Entry* entry) {
  if (entry == nullptr || entry->type != 5 || entry->count < 1U) {
    return std::nullopt;
  }
  const auto numerator = tiff.u32(entry->offset);
  const auto denominator = tiff.u32(entry->offset + 4U);
  if (denominator == 0U || numerator == 0U) {
    return std::nullopt;
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

struct LayerBlock {
  std::string key;
  std::span<const std::uint8_t> payload;
  bool wide{false};
};

struct LayerData {
  bool little_endian{false};
  bool large_document{false};
  std::vector<LayerBlock> blocks;
};

std::span<const std::uint8_t> source_data_span(const TiffBytes& tiff, const Directory& directory) {
  const auto* entry = directory.find(kTagImageSourceData);
  if (entry == nullptr || (entry->type != 7 && entry->type != 1) || entry->size < kDocumentDataHeader.size()) {
    return {};
  }
  return tiff.span(entry->offset, entry->size);
}

bool has_document_data_header(std::span<const std::uint8_t> data) noexcept {
  if (data.size() < kDocumentDataHeader.size()) {
    return false;
  }
  const std::string_view head(reinterpret_cast<const char*>(data.data()), kDocumentDataHeader.size());
  return head == kDocumentDataHeader || head == kLargeDocumentDataHeader;
}

// The tagged blocks after the header. Each block's signature says its byte order: '8BIM'
// (or '8B64') is big-endian, "MIB8" (or "46B8") little-endian; Photoshop writes the TIFF's
// own order, but the signature is what describes the bytes that follow.
LayerData parse_layer_data(std::span<const std::uint8_t> data) {
  LayerData result;
  result.large_document =
      std::string_view(reinterpret_cast<const char*>(data.data()), kLargeDocumentDataHeader.size()) ==
      kLargeDocumentDataHeader;
  std::size_t position = kDocumentDataHeader.size();
  std::optional<bool> order;
  while (data.size() - position >= 12U) {
    const auto signature = std::string_view(reinterpret_cast<const char*>(data.data() + position), 4U);
    bool little = false;
    bool wide_signature = false;
    if (signature == "8BIM" || signature == "8B64") {
      wide_signature = signature == "8B64";
    } else if (signature == "MIB8" || signature == "46B8") {
      little = true;
      wide_signature = signature == "46B8";
    } else {
      break;  // trailing bytes that are not a block: the blocks before still count
    }
    if (order.has_value() && *order != little) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's Photoshop layer data mixes byte orders"));
    }
    order = little;
    const TiffBytes block(data, little);
    std::string key = little ? little_endian_ostype(std::span<const std::uint8_t, 4>(data.data() + position + 4U, 4U))
                             : std::string(reinterpret_cast<const char*>(data.data() + position + 4U), 4U);
    position += 8U;
    const bool wide = wide_signature || (result.large_document && tagged_block_length_is_u64(key));
    if (wide && data.size() - position < 8U) {
      break;
    }
    const auto length = block.value(position, wide ? 8U : 4U);
    position += wide ? 8U : 4U;
    if (length > data.size() - position) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's Photoshop layer data is cut short"));
    }
    result.blocks.push_back(LayerBlock{std::move(key), data.subspan(position, static_cast<std::size_t>(length)), wide});
    position += static_cast<std::size_t>(length);
    position += std::min<std::size_t>((4U - (length % 4U)) % 4U, data.size() - position);
  }
  result.little_endian = order.value_or(false);
  return result;
}

struct ColorLayout {
  std::uint16_t mode{0};
  std::uint16_t channels{0};
};

ColorLayout color_layout(std::uint32_t photometric) {
  switch (photometric) {
    case 0:
    case 1: return {kColorModeGrayscale, 1};
    case 2: return {kColorModeRgb, 3};
    case 5: return {kColorModeCmyk, 4};
    case 8:
    case 9: return {kColorModeLab, 3};
    default:
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Patchy reads TIFF layers only in RGB, CMYK, grayscale and Lab files"));
  }
}

void append_resource(BigEndianWriter& writer, std::uint16_t id, std::span<const std::uint8_t> payload) {
  writer.write_bytes(std::array<std::uint8_t, 4>{'8', 'B', 'I', 'M'});
  writer.write_u16(id);
  writer.write_u16(0);  // empty name, padded to even
  writer.write_u32(static_cast<std::uint32_t>(payload.size()));
  writer.write_bytes(payload);
  if ((payload.size() % 2U) != 0U) {
    writer.write_u8(0);
  }
}

// Resource 1005 (pixels per inch as 16.16 fixed point) from the TIFF's resolution tags,
// for files whose resources lack it.
std::vector<std::uint8_t> resolution_resource(double horizontal_ppi, double vertical_ppi) {
  BigEndianWriter writer;
  const auto fixed = [](double ppi) {
    return static_cast<std::uint32_t>(std::clamp(ppi, 1.0, 30000.0) * 65536.0 + 0.5);
  };
  writer.write_u32(fixed(horizontal_ppi));
  writer.write_u16(1);
  writer.write_u16(1);
  writer.write_u32(fixed(vertical_ppi));
  writer.write_u16(1);
  writer.write_u16(1);
  return writer.bytes();
}

std::int16_t layer_count_of(std::span<const std::uint8_t> big_endian_layer_info) {
  if (big_endian_layer_info.size() < 2U) {
    return 0;
  }
  return static_cast<std::int16_t>((big_endian_layer_info[0] << 8U) | big_endian_layer_info[1]);
}

}  // namespace

bool tiff_has_photoshop_layers(std::span<const std::uint8_t> bytes) noexcept {
  const auto little = tiff_little_endian(bytes);
  if (!little.has_value()) {
    return false;
  }
  const TiffBytes tiff(bytes, *little);
  const auto directory = read_directory(tiff, tiff.u32(4));
  return directory.has_value() && has_document_data_header(source_data_span(tiff, *directory));
}

std::size_t tiff_page_count(std::span<const std::uint8_t> bytes) noexcept {
  const auto little = tiff_little_endian(bytes);
  if (!little.has_value()) {
    return 0;
  }
  const TiffBytes tiff(bytes, *little);
  std::set<std::uint32_t> seen;
  std::size_t pages = 0;
  for (auto offset = tiff.u32(4); offset != 0U && seen.size() < kMaxPages && seen.insert(offset).second;) {
    const auto directory = read_directory(tiff, offset);
    if (!directory.has_value()) {
      break;
    }
    const auto subfile = unsigned_value(tiff, directory->find(kTagNewSubfileType)).value_or(0U);
    if ((subfile & 0x01U) == 0U) {
      ++pages;
    }
    offset = directory->next;
  }
  return pages;
}

std::vector<std::uint8_t> photoshop_document_from_tiff(std::span<const std::uint8_t> bytes,
                                                       std::vector<std::string>* notices) {
  const auto little = tiff_little_endian(bytes);
  if (!little.has_value()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Not a TIFF file Patchy reads layers from"));
  }
  const TiffBytes tiff(bytes, *little);
  const auto directory = read_directory(tiff, tiff.u32(4));
  if (!directory.has_value()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's first directory is damaged"));
  }
  const auto source_data = source_data_span(tiff, *directory);
  if (!has_document_data_header(source_data)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF has no Photoshop layer data"));
  }

  const auto width = unsigned_value(tiff, directory->find(kTagImageWidth));
  const auto height = unsigned_value(tiff, directory->find(kTagImageLength));
  const auto* bits_entry = directory->find(kTagBitsPerSample);
  const auto bits = unsigned_value(tiff, bits_entry).value_or(1U);
  const auto photometric = unsigned_value(tiff, directory->find(kTagPhotometric));
  const auto sample_format = unsigned_value(tiff, directory->find(kTagSampleFormat)).value_or(1U);
  if (!width.has_value() || !height.has_value() || !photometric.has_value()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's first directory lacks its image size or color layout"));
  }
  if (!((bits == 8U || bits == 16U) && sample_format == 1U) && !(bits == 32U && sample_format == 3U)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Patchy reads TIFF layers only at 8 or 16 bits or 32-bit float"));
  }
  const auto layout = color_layout(*photometric);

  auto layer_data = parse_layer_data(source_data);
  const LittleEndianSource source{layer_data.large_document};
  const std::string layer_key = bits == 16U ? "Lr16" : bits == 32U ? "Lr32" : "Layr";
  std::vector<std::string> dropped;
  std::optional<std::vector<std::uint8_t>> layer_info;
  std::vector<std::uint8_t> global_mask;
  struct OutBlock {
    std::string key;
    std::vector<std::uint8_t> payload;
    bool wide{false};
  };
  std::vector<OutBlock> globals;
  for (const auto& block : layer_data.blocks) {
    const bool layer_block = block.key == "Layr" || block.key == "Lr16" || block.key == "Lr32";
    if (layer_block) {
      // The depth's own key wins; another layer key stands in only when it is the sole one
      // (a writer that labeled a 16-bit file's layers 'Layr').
      if (layer_info.has_value() && block.key != layer_key) {
        continue;
      }
      layer_info = layer_data.little_endian ? layer_info_to_big_endian(block.payload, source, dropped)
                                            : std::vector<std::uint8_t>(block.payload.begin(), block.payload.end());
      continue;
    }
    std::optional<std::vector<std::uint8_t>> payload =
        layer_data.little_endian ? document_block_to_big_endian(block.key, block.payload, source)
                                 : std::vector<std::uint8_t>(block.payload.begin(), block.payload.end());
    if (!payload.has_value()) {
      dropped.push_back(block.key);
      continue;
    }
    if (block.key == "LMsk") {
      global_mask = std::move(*payload);
    } else {
      globals.push_back(OutBlock{block.key, std::move(*payload), block.wide});
    }
  }
  if (!layer_info.has_value() || layer_count_of(*layer_info) == 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's Photoshop layer data holds no layers"));
  }
  if (!dropped.empty() && notices != nullptr) {
    notices->push_back(PATCHY_TRANSLATE_NOOP(
        "QObject",
        "This TIFF stores its layers in IBM PC byte order, and some of that layer data is in a form Patchy "
        "cannot convert yet. Patchy left it out, so some layers may look different than in Photoshop."));
  }

  // Image resources: tag 34377 is big-endian in either byte order. The TIFF's profile and
  // resolution tags stand in for resources the block lacks.
  std::vector<std::uint8_t> resources;
  if (const auto* entry = directory->find(kTagPhotoshopResources);
      entry != nullptr && entry->size >= 4U && std::memcmp(bytes.data() + entry->offset, "8BIM", 4U) == 0) {
    const auto span = tiff.span(entry->offset, entry->size);
    resources.assign(span.begin(), span.end());
  }
  {
    BigEndianWriter extra;
    if (const auto* icc = directory->find(kTagIccProfile);
        icc != nullptr && icc->size > 0U && !find_image_resource_payload(resources, kImageResourceIccProfile)) {
      append_resource(extra, kImageResourceIccProfile, tiff.span(icc->offset, icc->size));
    }
    if (!find_image_resource_payload(resources, kImageResourceResolutionInfo)) {
      auto horizontal = rational_value(tiff, directory->find(kTagXResolution));
      auto vertical = rational_value(tiff, directory->find(kTagYResolution));
      if (horizontal.has_value()) {
        const double scale = unsigned_value(tiff, directory->find(kTagResolutionUnit)).value_or(2U) == 3U ? 2.54 : 1.0;
        append_resource(extra, kImageResourceResolutionInfo,
                        resolution_resource(*horizontal * scale, vertical.value_or(*horizontal) * scale));
      }
    }
    resources.insert(resources.end(), extra.bytes().begin(), extra.bytes().end());
  }

  // The PSD stream: header, empty color mode data, resources, the layer and mask
  // section, and a two-byte image data section the layered read never decodes.
  const bool large = layer_data.large_document;
  const bool merged_transparency = layer_count_of(*layer_info) < 0;
  BigEndianWriter section;
  const auto write_length = [&section, large](std::uint64_t length, bool wide) {
    if (wide && large) {
      section.write_u64(length);
    } else {
      section.write_u32(static_cast<std::uint32_t>(length));
    }
  };
  if (layer_key == "Layr") {
    const auto padded = layer_info->size() + (layer_info->size() % 2U);
    write_length(padded, true);
    section.write_bytes(*layer_info);
    if (padded != layer_info->size()) {
      section.write_u8(0);
    }
  } else {
    write_length(0, true);
    globals.insert(globals.begin(), OutBlock{layer_key, std::move(*layer_info), true});
  }
  section.write_u32(static_cast<std::uint32_t>(global_mask.size()));
  section.write_bytes(global_mask);
  for (const auto& block : globals) {
    const bool wide = large && (block.wide || tagged_block_length_is_u64(block.key));
    section.write_bytes(std::array<std::uint8_t, 4>{'8', 'B', 'I', 'M'});
    section.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(block.key.data()), 4U));
    if (wide) {
      section.write_u64(block.payload.size());
    } else {
      section.write_u32(static_cast<std::uint32_t>(block.payload.size()));
    }
    section.write_bytes(block.payload);
    for (auto pad = (4U - (block.payload.size() % 4U)) % 4U; pad > 0U; --pad) {
      section.write_u8(0);
    }
  }

  BigEndianWriter writer;
  Header header;
  header.large_document = large;
  header.channels = static_cast<std::uint16_t>(layout.channels + (merged_transparency ? 1U : 0U));
  header.width = *width;
  header.height = *height;
  header.depth = static_cast<std::uint16_t>(bits);
  header.color_mode = layout.mode;
  write_header(writer, header);
  writer.write_u32(0);  // color mode data
  writer.write_u32(static_cast<std::uint32_t>(resources.size()));
  writer.write_bytes(resources);
  if (!large && section.bytes().size() > 0xFFFFFFFFULL) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's Photoshop layer data is too large"));
  }
  if (large) {
    writer.write_u64(section.bytes().size());
  } else {
    writer.write_u32(static_cast<std::uint32_t>(section.bytes().size()));
  }
  writer.write_bytes(section.bytes());
  writer.write_u16(0);
  return std::move(writer.bytes());
}

Document read_layered_tiff(std::span<const std::uint8_t> bytes, ReadOptions options) {
  const auto psd = photoshop_document_from_tiff(bytes, options.notices);
  options.prefer_flat_composite = false;
  options.retain_flat_composite = false;
  auto document = DocumentIo::read(psd, options);
  if (document.layers().empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's Photoshop layer data holds no layers"));
  }
  document.metadata().values["tiff.layers"] = "photoshop";
  return document;
}

Document read_layered_tiff_file(const std::filesystem::path& path, ReadOptions options) {
  const auto bytes = read_file_bytes(path);
  return read_layered_tiff(bytes, std::move(options));
}

}  // namespace patchy::psd
