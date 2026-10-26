#include "formats/metadata/tiff_ifd.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace patchy::metadata {
namespace {

// Hard caps against hostile streams. Real EXIF has a few hundred entries in total.
constexpr std::uint32_t kMaxEntriesPerIfd = 2048;
constexpr std::size_t kMaxIfds = 64;
constexpr std::uint64_t kMaxValueBytes = 64ULL * 1024ULL * 1024ULL;

struct Reader {
  std::span<const std::uint8_t> bytes;
  bool big_endian{false};

  [[nodiscard]] bool has(std::uint64_t offset, std::uint64_t length) const noexcept {
    return offset <= bytes.size() && length <= bytes.size() - offset;
  }
  [[nodiscard]] std::uint16_t u16(std::size_t offset) const noexcept {
    return big_endian ? static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1])
                      : static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
  }
  [[nodiscard]] std::uint32_t u32(std::size_t offset) const noexcept {
    if (big_endian) {
      return (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
    }
    return bytes[offset] | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
  }
};

std::uint64_t read_unsigned(std::span<const std::uint8_t> data, std::size_t offset, std::size_t size, bool big_endian) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < size; ++i) {
    const auto byte = static_cast<std::uint64_t>(data[offset + (big_endian ? i : size - 1 - i)]);
    value = (value << 8) | byte;
  }
  return value;
}

void append_unsigned(std::vector<std::uint8_t>& out, std::uint64_t value, std::size_t size, bool big_endian) {
  for (std::size_t i = 0; i < size; ++i) {
    const auto shift = 8 * (big_endian ? size - 1 - i : i);
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
}

void put_unsigned(std::vector<std::uint8_t>& out, std::size_t at, std::uint64_t value, std::size_t size,
                  bool big_endian) {
  for (std::size_t i = 0; i < size; ++i) {
    const auto shift = 8 * (big_endian ? size - 1 - i : i);
    out[at + i] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
  }
}

struct Walk {
  Reader reader;
  TiffStream stream;
  std::set<std::uint32_t> visited;
  TiffParseOptions options;

  // Reads one directory into `kind`; returns the next-IFD offset (0 at the end).
  std::uint32_t read_ifd(std::uint32_t offset, IfdKind kind) {
    if (stream.ifds.size() >= kMaxIfds || offset < 8 || !visited.insert(offset).second || !reader.has(offset, 2)) {
      stream.truncated = stream.truncated || offset != 0;
      return 0;
    }
    const std::uint32_t count = reader.u16(offset);
    if (count > kMaxEntriesPerIfd || !reader.has(offset + 2ULL, count * 12ULL)) {
      stream.truncated = true;
      return 0;
    }
    TiffIfd ifd;
    ifd.kind = kind;
    ifd.entries.reserve(count);
    std::uint64_t total_value_bytes = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
      const std::size_t at = offset + 2U + i * 12U;
      TiffEntry entry;
      entry.tag = reader.u16(at);
      entry.type = reader.u16(at + 2);
      entry.count = reader.u32(at + 4);
      const auto type_size = tiff_type_size(entry.type);
      if (type_size == 0) {
        continue;  // unknown type: skip the entry, keep the directory
      }
      const std::uint64_t size = static_cast<std::uint64_t>(type_size) * entry.count;
      total_value_bytes += size;
      if (size > kMaxValueBytes || total_value_bytes > kMaxValueBytes) {
        stream.truncated = true;
        continue;
      }
      std::uint64_t value_offset = at + 8;
      if (size > 4) {
        value_offset = reader.u32(at + 8);
      }
      if (!reader.has(value_offset, size)) {
        stream.truncated = true;
        continue;
      }
      entry.data.assign(reader.bytes.begin() + static_cast<std::ptrdiff_t>(value_offset),
                        reader.bytes.begin() + static_cast<std::ptrdiff_t>(value_offset + size));
      ifd.entries.push_back(std::move(entry));
    }
    const std::uint32_t next = reader.has(offset + 2ULL + count * 12ULL, 4) ? reader.u32(offset + 2U + count * 12U) : 0;
    stream.ifds.push_back(std::move(ifd));
    const auto index = stream.ifds.size() - 1;

    // Copy the pointer offsets out first: following them appends to stream.ifds and
    // invalidates references into it.
    const auto pointer_of = [&](std::uint16_t tag) -> std::optional<std::uint32_t> {
      const auto* entry = stream.ifds[index].find(tag);
      if (entry == nullptr) {
        return std::nullopt;
      }
      return stream.unsigned_value(*entry);
    };
    std::vector<std::uint32_t> sub_ifds;
    if (options.follow_sub_ifds) {
      if (const auto* entry = stream.ifds[index].find(kTagSubIfds)) {
        for (std::uint32_t i = 0; i < std::min<std::uint32_t>(entry->count, 16); ++i) {
          if (const auto value = stream.unsigned_value(*entry, i)) {
            sub_ifds.push_back(*value);
          }
        }
      }
    }
    const auto exif = kind == IfdKind::Ifd0 ? pointer_of(kTagExifIfd) : std::nullopt;
    const auto gps = kind == IfdKind::Ifd0 ? pointer_of(kTagGpsIfd) : std::nullopt;
    const auto interop = kind == IfdKind::Exif ? pointer_of(kTagInteropIfd) : std::nullopt;
    if (kind == IfdKind::Ifd1) {
      const auto thumb_offset = pointer_of(kTagJpegInterchangeFormat);
      const auto thumb_length = pointer_of(kTagJpegInterchangeFormatLength);
      if (thumb_offset && thumb_length && *thumb_length > 0 && reader.has(*thumb_offset, *thumb_length)) {
        stream.thumbnail.assign(reader.bytes.begin() + static_cast<std::ptrdiff_t>(*thumb_offset),
                                reader.bytes.begin() + static_cast<std::ptrdiff_t>(*thumb_offset + *thumb_length));
      }
    }
    if (exif) {
      read_ifd(*exif, IfdKind::Exif);
    }
    if (gps) {
      read_ifd(*gps, IfdKind::Gps);
    }
    if (interop) {
      read_ifd(*interop, IfdKind::Interop);
    }
    for (const auto sub : sub_ifds) {
      read_ifd(sub, IfdKind::SubIfd);
    }
    return next;
  }
};

bool is_pointer_tag(IfdKind kind, std::uint16_t tag) noexcept {
  switch (kind) {
    case IfdKind::Ifd0:
      return tag == kTagExifIfd || tag == kTagGpsIfd || tag == kTagSubIfds;
    case IfdKind::Exif:
      return tag == kTagInteropIfd;
    case IfdKind::Ifd1:
      return tag == kTagJpegInterchangeFormat || tag == kTagJpegInterchangeFormatLength;
    default:
      return false;
  }
}

// Strip/tile layout tags describe image data write_tiff does not carry; writing
// them would point at nothing.
bool is_image_data_tag(std::uint16_t tag) noexcept {
  return tag == 273 || tag == 279 || tag == 324 || tag == 325;  // Strip/Tile Offsets/ByteCounts
}

}  // namespace

std::uint32_t tiff_type_size(std::uint16_t type) noexcept {
  switch (static_cast<TiffType>(type)) {
    case TiffType::Byte:
    case TiffType::Ascii:
    case TiffType::SByte:
    case TiffType::Undefined:
      return 1;
    case TiffType::Short:
    case TiffType::SShort:
      return 2;
    case TiffType::Long:
    case TiffType::SLong:
    case TiffType::Float:
    case TiffType::Ifd:
      return 4;
    case TiffType::Rational:
    case TiffType::SRational:
    case TiffType::Double:
      return 8;
  }
  return 0;
}

const TiffEntry* TiffIfd::find(std::uint16_t tag) const noexcept {
  for (const auto& entry : entries) {
    if (entry.tag == tag) {
      return &entry;
    }
  }
  return nullptr;
}

TiffEntry* TiffIfd::find(std::uint16_t tag) noexcept {
  for (auto& entry : entries) {
    if (entry.tag == tag) {
      return &entry;
    }
  }
  return nullptr;
}

void TiffIfd::erase(std::uint16_t tag) {
  std::erase_if(entries, [tag](const TiffEntry& entry) { return entry.tag == tag; });
}

const TiffIfd* TiffStream::find(IfdKind kind) const noexcept {
  for (const auto& ifd : ifds) {
    if (ifd.kind == kind) {
      return &ifd;
    }
  }
  return nullptr;
}

TiffIfd* TiffStream::find(IfdKind kind) noexcept {
  for (auto& ifd : ifds) {
    if (ifd.kind == kind) {
      return &ifd;
    }
  }
  return nullptr;
}

TiffIfd& TiffStream::find_or_add(IfdKind kind) {
  if (auto* ifd = find(kind)) {
    return *ifd;
  }
  ifds.push_back(TiffIfd{kind, {}});
  return ifds.back();
}

std::optional<std::uint32_t> TiffStream::unsigned_value(const TiffEntry& entry, std::uint32_t index) const {
  const auto size = tiff_type_size(entry.type);
  if (index >= entry.count || size == 0 || static_cast<std::uint64_t>(index + 1) * size > entry.data.size()) {
    return std::nullopt;
  }
  switch (static_cast<TiffType>(entry.type)) {
    case TiffType::Byte:
    case TiffType::Undefined:
    case TiffType::Short:
    case TiffType::Long:
    case TiffType::Ifd:
      return static_cast<std::uint32_t>(read_unsigned(entry.data, index * size, size, big_endian));
    case TiffType::SByte:
    case TiffType::SShort:
    case TiffType::SLong: {
      const auto value = signed_value(entry, index);
      if (value && *value >= 0) {
        return static_cast<std::uint32_t>(*value);
      }
      return std::nullopt;
    }
    default:
      return std::nullopt;
  }
}

std::optional<std::int64_t> TiffStream::signed_value(const TiffEntry& entry, std::uint32_t index) const {
  const auto size = tiff_type_size(entry.type);
  if (index >= entry.count || size == 0 || static_cast<std::uint64_t>(index + 1) * size > entry.data.size()) {
    return std::nullopt;
  }
  const auto raw = read_unsigned(entry.data, index * size, size, big_endian);
  switch (static_cast<TiffType>(entry.type)) {
    case TiffType::SByte:
      return static_cast<std::int8_t>(raw);
    case TiffType::SShort:
      return static_cast<std::int16_t>(raw);
    case TiffType::SLong:
      return static_cast<std::int32_t>(raw);
    case TiffType::Byte:
    case TiffType::Undefined:
    case TiffType::Short:
    case TiffType::Long:
    case TiffType::Ifd:
      return static_cast<std::int64_t>(raw);
    default:
      return std::nullopt;
  }
}

std::optional<std::pair<std::int64_t, std::int64_t>> TiffStream::rational_value(const TiffEntry& entry,
                                                                               std::uint32_t index) const {
  const auto type = static_cast<TiffType>(entry.type);
  if ((type != TiffType::Rational && type != TiffType::SRational) || index >= entry.count ||
      static_cast<std::uint64_t>(index + 1) * 8 > entry.data.size()) {
    return std::nullopt;
  }
  const auto numerator = read_unsigned(entry.data, index * 8, 4, big_endian);
  const auto denominator = read_unsigned(entry.data, index * 8 + 4, 4, big_endian);
  if (type == TiffType::SRational) {
    return std::pair{static_cast<std::int64_t>(static_cast<std::int32_t>(numerator)),
                     static_cast<std::int64_t>(static_cast<std::int32_t>(denominator))};
  }
  return std::pair{static_cast<std::int64_t>(numerator), static_cast<std::int64_t>(denominator)};
}

std::optional<double> TiffStream::real_value(const TiffEntry& entry, std::uint32_t index) const {
  const auto type = static_cast<TiffType>(entry.type);
  if (type == TiffType::Rational || type == TiffType::SRational) {
    const auto value = rational_value(entry, index);
    if (!value || value->second == 0) {
      return std::nullopt;
    }
    return static_cast<double>(value->first) / static_cast<double>(value->second);
  }
  if (type == TiffType::Float && index < entry.count && (index + 1) * 4ULL <= entry.data.size()) {
    const auto raw = static_cast<std::uint32_t>(read_unsigned(entry.data, index * 4, 4, big_endian));
    float value = 0.0F;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }
  if (type == TiffType::Double && index < entry.count && (index + 1) * 8ULL <= entry.data.size()) {
    const auto raw = read_unsigned(entry.data, index * 8, 8, big_endian);
    double value = 0.0;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }
  if (const auto value = signed_value(entry, index)) {
    return static_cast<double>(*value);
  }
  return std::nullopt;
}

std::string TiffStream::string_value(const TiffEntry& entry) const {
  std::string out;
  std::string current;
  for (const auto byte : entry.data) {
    if (byte == 0) {
      if (!current.empty()) {
        if (!out.empty()) {
          out += "; ";
        }
        out += current;
        current.clear();
      }
      continue;
    }
    current.push_back(static_cast<char>(byte));
  }
  if (!current.empty()) {
    if (!out.empty()) {
      out += "; ";
    }
    out += current;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
    out.pop_back();
  }
  return out;
}

namespace {

void replace_entry(TiffStream& stream, IfdKind kind, TiffEntry entry) {
  auto& ifd = stream.find_or_add(kind);
  ifd.erase(entry.tag);
  ifd.entries.push_back(std::move(entry));
}

}  // namespace

void TiffStream::set_ascii(IfdKind kind, std::uint16_t tag, const std::string& value) {
  TiffEntry entry{tag, static_cast<std::uint16_t>(TiffType::Ascii), 0, {}};
  entry.data.assign(value.begin(), value.end());
  entry.data.push_back(0);
  entry.count = static_cast<std::uint32_t>(entry.data.size());
  replace_entry(*this, kind, std::move(entry));
}

void TiffStream::set_short(IfdKind kind, std::uint16_t tag, std::uint16_t value) {
  TiffEntry entry{tag, static_cast<std::uint16_t>(TiffType::Short), 1, {}};
  append_unsigned(entry.data, value, 2, big_endian);
  replace_entry(*this, kind, std::move(entry));
}

void TiffStream::set_long(IfdKind kind, std::uint16_t tag, std::uint32_t value) {
  TiffEntry entry{tag, static_cast<std::uint16_t>(TiffType::Long), 1, {}};
  append_unsigned(entry.data, value, 4, big_endian);
  replace_entry(*this, kind, std::move(entry));
}

void TiffStream::set_rational(IfdKind kind, std::uint16_t tag, std::uint32_t numerator, std::uint32_t denominator) {
  TiffEntry entry{tag, static_cast<std::uint16_t>(TiffType::Rational), 1, {}};
  append_unsigned(entry.data, numerator, 4, big_endian);
  append_unsigned(entry.data, denominator, 4, big_endian);
  replace_entry(*this, kind, std::move(entry));
}

void TiffStream::set_undefined(IfdKind kind, std::uint16_t tag, std::span<const std::uint8_t> value) {
  TiffEntry entry{tag, static_cast<std::uint16_t>(TiffType::Undefined), static_cast<std::uint32_t>(value.size()), {}};
  entry.data.assign(value.begin(), value.end());
  replace_entry(*this, kind, std::move(entry));
}

std::optional<TiffStream> parse_tiff(std::span<const std::uint8_t> bytes, const TiffParseOptions& options) {
  if (bytes.size() < 8) {
    return std::nullopt;
  }
  Walk walk;
  walk.reader.bytes = bytes;
  walk.options = options;
  if (bytes[0] == 'I' && bytes[1] == 'I') {
    walk.reader.big_endian = false;
  } else if (bytes[0] == 'M' && bytes[1] == 'M') {
    walk.reader.big_endian = true;
  } else {
    return std::nullopt;
  }
  const auto magic = walk.reader.u16(2);
  // 42 = TIFF, 0x55 = Panasonic RW2, 0x4F52 'RO' / 0x5352 'RS' = Olympus ORF.
  if (magic != 42 && magic != 0x55 && magic != 0x4F52 && magic != 0x5352) {
    return std::nullopt;
  }
  walk.stream.big_endian = walk.reader.big_endian;
  const auto first = walk.reader.u32(4);
  const auto second = walk.read_ifd(first, IfdKind::Ifd0);
  if (second != 0) {
    auto next = walk.read_ifd(second, IfdKind::Ifd1);
    while (options.follow_page_chain && next != 0) {
      next = walk.read_ifd(next, IfdKind::Chained);
    }
  }
  if (walk.stream.ifds.empty()) {
    return std::nullopt;
  }
  return std::move(walk.stream);
}

namespace {

struct IfdWriter {
  std::vector<std::uint8_t>& out;
  bool big_endian;

  // Writes a directory at the current end of `out`, followed by its out-of-line
  // values. `pointers` are extra LONG entries whose value is patched later; returns
  // {directory offset, position of each pointer value, position of the next-IFD field}.
  struct Placed {
    std::size_t offset{0};
    std::vector<std::pair<std::uint16_t, std::size_t>> pointer_slots;
    std::size_t next_slot{0};
  };

  Placed write(const TiffIfd& ifd, const std::vector<std::uint16_t>& pointers) {
    std::vector<const TiffEntry*> entries;
    for (const auto& entry : ifd.entries) {
      if (!is_pointer_tag(ifd.kind, entry.tag) && !is_image_data_tag(entry.tag) &&
          tiff_type_size(entry.type) != 0) {
        entries.push_back(&entry);
      }
    }
    std::vector<TiffEntry> pointer_entries;
    pointer_entries.reserve(pointers.size());
    for (const auto tag : pointers) {
      pointer_entries.push_back(TiffEntry{tag, static_cast<std::uint16_t>(TiffType::Long), 1, {0, 0, 0, 0}});
    }
    for (const auto& entry : pointer_entries) {
      entries.push_back(&entry);
    }
    std::sort(entries.begin(), entries.end(),
              [](const TiffEntry* left, const TiffEntry* right) { return left->tag < right->tag; });

    Placed placed;
    if (out.size() % 2 != 0) {
      out.push_back(0);  // directories start on a word boundary
    }
    placed.offset = out.size();
    append_unsigned(out, entries.size(), 2, big_endian);
    std::vector<std::pair<std::size_t, const TiffEntry*>> deferred;
    for (const auto* entry : entries) {
      append_unsigned(out, entry->tag, 2, big_endian);
      append_unsigned(out, entry->type, 2, big_endian);
      append_unsigned(out, entry->count, 4, big_endian);
      const auto value_at = out.size();
      if (entry->data.size() <= 4) {
        out.insert(out.end(), entry->data.begin(), entry->data.end());
        out.insert(out.end(), 4 - entry->data.size(), 0);
      } else {
        append_unsigned(out, 0, 4, big_endian);
        deferred.emplace_back(value_at, entry);
      }
      if (std::find(pointers.begin(), pointers.end(), entry->tag) != pointers.end()) {
        placed.pointer_slots.emplace_back(entry->tag, value_at);
      }
    }
    placed.next_slot = out.size();
    append_unsigned(out, 0, 4, big_endian);
    for (const auto& [slot, entry] : deferred) {
      if (out.size() % 2 != 0) {
        out.push_back(0);
      }
      put_unsigned(out, slot, out.size(), 4, big_endian);
      out.insert(out.end(), entry->data.begin(), entry->data.end());
    }
    return placed;
  }

  void patch(std::size_t slot, std::size_t value) { put_unsigned(out, slot, value, 4, big_endian); }
};

std::size_t slot_for(const IfdWriter::Placed& placed, std::uint16_t tag) {
  for (const auto& [slot_tag, slot] : placed.pointer_slots) {
    if (slot_tag == tag) {
      return slot;
    }
  }
  return 0;
}

}  // namespace

std::vector<std::uint8_t> write_tiff(const TiffStream& stream) {
  std::vector<std::uint8_t> out;
  const bool be = stream.big_endian;
  out.push_back(be ? 'M' : 'I');
  out.push_back(be ? 'M' : 'I');
  append_unsigned(out, 42, 2, be);
  append_unsigned(out, 8, 4, be);
  IfdWriter writer{out, be};

  const auto non_empty = [&](IfdKind kind) -> const TiffIfd* {
    const auto* ifd = stream.find(kind);
    if (ifd == nullptr) {
      return nullptr;
    }
    const bool any = std::any_of(ifd->entries.begin(), ifd->entries.end(), [&](const TiffEntry& entry) {
      return !is_pointer_tag(kind, entry.tag) && !is_image_data_tag(entry.tag);
    });
    return any ? ifd : nullptr;
  };
  const auto* exif = non_empty(IfdKind::Exif);
  const auto* gps = non_empty(IfdKind::Gps);
  const auto* interop = exif != nullptr ? non_empty(IfdKind::Interop) : nullptr;
  const auto* ifd1 = stream.find(IfdKind::Ifd1);
  const bool has_thumbnail = ifd1 != nullptr && !stream.thumbnail.empty();

  static const TiffIfd empty_ifd0{IfdKind::Ifd0, {}};
  const auto* ifd0 = stream.find(IfdKind::Ifd0);
  std::vector<std::uint16_t> ifd0_pointers;
  if (exif != nullptr) {
    ifd0_pointers.push_back(kTagExifIfd);
  }
  if (gps != nullptr) {
    ifd0_pointers.push_back(kTagGpsIfd);
  }
  const auto placed0 = writer.write(ifd0 != nullptr ? *ifd0 : empty_ifd0, ifd0_pointers);
  if (exif != nullptr) {
    const auto placed = writer.write(*exif, interop != nullptr ? std::vector<std::uint16_t>{kTagInteropIfd}
                                                               : std::vector<std::uint16_t>{});
    writer.patch(slot_for(placed0, kTagExifIfd), placed.offset);
    if (interop != nullptr) {
      const auto placed_interop = writer.write(*interop, {});
      writer.patch(slot_for(placed, kTagInteropIfd), placed_interop.offset);
    }
  }
  if (gps != nullptr) {
    const auto placed = writer.write(*gps, {});
    writer.patch(slot_for(placed0, kTagGpsIfd), placed.offset);
  }
  if (has_thumbnail) {
    const auto placed1 =
        writer.write(*ifd1, {kTagJpegInterchangeFormat, kTagJpegInterchangeFormatLength});
    writer.patch(placed0.next_slot, placed1.offset);
    writer.patch(slot_for(placed1, kTagJpegInterchangeFormatLength), stream.thumbnail.size());
    writer.patch(slot_for(placed1, kTagJpegInterchangeFormat), out.size());
    out.insert(out.end(), stream.thumbnail.begin(), stream.thumbnail.end());
  }
  return out;
}

bool is_tiff_structure_tag(std::uint16_t tag) noexcept {
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

}  // namespace patchy::metadata
