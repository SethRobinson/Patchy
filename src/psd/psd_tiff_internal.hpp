#pragma once

// TIFF structure shared by the layered TIFF reader and writer (psd_tiff.cpp) and the flat
// TIFF decoder (psd_tiff_flat.cpp): tag numbers, bounds-checked reads in the file's byte
// order, and one image file directory. Qt-free. docs/tiff.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace patchy::psd::tiff_detail {

constexpr std::uint16_t kTagNewSubfileType = 254;
constexpr std::uint16_t kTagImageWidth = 256;
constexpr std::uint16_t kTagImageLength = 257;
constexpr std::uint16_t kTagBitsPerSample = 258;
constexpr std::uint16_t kTagCompression = 259;
constexpr std::uint16_t kTagPhotometric = 262;
constexpr std::uint16_t kTagStripOffsets = 273;
constexpr std::uint16_t kTagSamplesPerPixel = 277;
constexpr std::uint16_t kTagRowsPerStrip = 278;
constexpr std::uint16_t kTagStripByteCounts = 279;
constexpr std::uint16_t kTagXResolution = 282;
constexpr std::uint16_t kTagYResolution = 283;
constexpr std::uint16_t kTagPlanarConfiguration = 284;
constexpr std::uint16_t kTagResolutionUnit = 296;
constexpr std::uint16_t kTagPredictor = 317;
constexpr std::uint16_t kTagExtraSamples = 338;
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

inline std::optional<bool> tiff_little_endian(std::span<const std::uint8_t> bytes) noexcept {
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

inline std::size_t type_size(std::uint16_t type) noexcept {
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
inline std::optional<Directory> read_directory(const TiffBytes& tiff, std::uint32_t offset) {
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

inline std::optional<std::uint32_t> unsigned_value(const TiffBytes& tiff, const Entry* entry, std::uint32_t index = 0) {
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

inline std::optional<double> rational_value(const TiffBytes& tiff, const Entry* entry) {
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

}  // namespace patchy::psd::tiff_detail
