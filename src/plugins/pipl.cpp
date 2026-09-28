#include "plugins/pipl.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>

namespace patchy::pipl {

namespace {

using Bytes = std::span<const std::uint8_t>;

std::uint16_t read_u16(Bytes bytes, std::size_t offset) {
  if (offset + 2 > bytes.size()) {
    return 0;
  }
  return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U));
}

std::uint32_t read_u32(Bytes bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    return 0;
  }
  return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

// A property key or vendor code: four bytes stored least-significant first.
std::uint32_t read_fourcc_le(Bytes bytes, std::size_t offset) { return read_u32(bytes, offset); }

// Pascal string (length byte, then characters). Bytes above 0x7F are treated
// as Latin-1, which is what the Windows resource compilers emit for the
// accented names seen in the wild.
std::string pascal_string_utf8(Bytes data) {
  if (data.empty()) {
    return {};
  }
  const std::size_t length = std::min<std::size_t>(data[0], data.size() - 1);
  std::string out;
  out.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    const auto byte = data[1 + i];
    if (byte < 0x80) {
      out.push_back(static_cast<char>(byte));
    } else {
      out.push_back(static_cast<char>(0xC0 | (byte >> 6U)));
      out.push_back(static_cast<char>(0x80 | (byte & 0x3FU)));
    }
  }
  return out;
}

std::string c_string(Bytes data) {
  std::string out;
  for (const auto byte : data) {
    if (byte == 0) {
      break;
    }
    out.push_back(static_cast<char>(byte));
  }
  return out;
}

constexpr std::uint32_t kVendorAdobe = fourcc('8', 'B', 'I', 'M');
constexpr std::uint32_t kKeyKind = fourcc('k', 'i', 'n', 'd');
constexpr std::uint32_t kKeyName = fourcc('n', 'a', 'm', 'e');
constexpr std::uint32_t kKeyCategory = fourcc('c', 'a', 't', 'g');
constexpr std::uint32_t kKeyEntryPoint32 = fourcc('w', 'x', '8', '6');
constexpr std::uint32_t kKeyEntryPoint64 = fourcc('8', '6', '6', '4');
constexpr std::uint32_t kKeyVersion = fourcc('v', 'e', 'r', 's');
constexpr std::uint32_t kKeyModes = fourcc('m', 'o', 'd', 'e');
constexpr std::uint32_t kKeyCaseInfo = fourcc('f', 'i', 'c', 'i');

struct Section {
  std::uint32_t virtual_address{0};
  std::uint32_t virtual_size{0};
  std::uint32_t raw_pointer{0};
  std::uint32_t raw_size{0};
};

std::optional<std::size_t> rva_to_offset(const std::vector<Section>& sections, std::uint32_t rva) {
  for (const auto& section : sections) {
    const auto span = std::max(section.virtual_size, section.raw_size);
    if (rva >= section.virtual_address && rva < section.virtual_address + span) {
      return static_cast<std::size_t>(section.raw_pointer) + (rva - section.virtual_address);
    }
  }
  return std::nullopt;
}

struct ResourceWalker {
  Bytes image;
  const std::vector<Section>& sections;
  std::size_t resource_base{0};
  std::vector<Bytes> pipl_blobs;

  bool name_is_pipl(std::uint32_t name_field) const {
    if ((name_field & 0x80000000U) == 0) {
      return false;
    }
    const auto offset = resource_base + (name_field & 0x7FFFFFFFU);
    const auto length = read_u16(image, offset);
    if (length != 4) {
      return false;
    }
    const char expected[] = {'P', 'I', 'P', 'L'};
    for (std::size_t i = 0; i < 4; ++i) {
      const auto unit = read_u16(image, offset + 2 + i * 2);
      const auto upper = (unit >= 'a' && unit <= 'z') ? static_cast<std::uint16_t>(unit - 32) : unit;
      if (upper != static_cast<std::uint16_t>(expected[i])) {
        return false;
      }
    }
    return true;
  }

  void collect_leaf(std::uint32_t data_field) {
    const auto entry = resource_base + data_field;
    const auto data_rva = read_u32(image, entry);
    const auto size = read_u32(image, entry + 4);
    const auto offset = rva_to_offset(sections, data_rva);
    if (!offset.has_value() || *offset >= image.size()) {
      return;
    }
    const auto available = std::min<std::size_t>(size, image.size() - *offset);
    pipl_blobs.push_back(image.subspan(*offset, available));
  }

  // Walks one directory level; depth 0 is the type level, where only the PIPL
  // type is followed. Deeper levels (name, language) are followed entirely.
  void walk(std::uint32_t directory_offset, int depth) {
    if (depth > 3) {
      return;
    }
    const auto directory = resource_base + directory_offset;
    const auto named = read_u16(image, directory + 12);
    const auto ids = read_u16(image, directory + 14);
    const std::size_t count = static_cast<std::size_t>(named) + ids;
    for (std::size_t i = 0; i < count && i < 4096; ++i) {
      const auto entry = directory + 16 + i * 8;
      if (entry + 8 > image.size()) {
        return;
      }
      const auto name_field = read_u32(image, entry);
      const auto data_field = read_u32(image, entry + 4);
      if (depth == 0 && !name_is_pipl(name_field)) {
        continue;
      }
      if ((data_field & 0x80000000U) != 0) {
        walk(data_field & 0x7FFFFFFFU, depth + 1);
      } else {
        collect_leaf(data_field);
      }
    }
  }
};

}  // namespace

FilterCaseInfo PiplInfo::case_info_for(int filter_case) const noexcept {
  if (filter_case < 1 || filter_case > 7) {
    return {};
  }
  if (has_case_info) {
    return case_info[static_cast<std::size_t>(filter_case - 1)];
  }
  // No property: the flat-image cases are handled plainly and the transparency
  // cases are not offered (the plug-in never asked for a transparency plane).
  FilterCaseInfo fallback;
  if (filter_case == kFilterCaseFlatImageNoSelection || filter_case == kFilterCaseFlatImageWithSelection) {
    fallback.input_handling = kDataHandlingNone;
    fallback.output_handling = kDataHandlingNone;
  }
  return fallback;
}

bool PiplInfo::supports_filter_case(int filter_case) const noexcept {
  return case_info_for(filter_case).input_handling != kDataHandlingCantFilter;
}

PiplInfo parse_pipl_blob(Bytes blob) {
  PiplInfo info;
  // Windows layout: int16 reserved (1), int32 version (0), int32 count.
  if (blob.size() < 10) {
    return info;
  }
  const auto count = read_u32(blob, 6);
  if (count == 0 || count > 4096) {
    return info;
  }
  std::size_t offset = 10;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (offset + 16 > blob.size()) {
      break;
    }
    const auto vendor = read_fourcc_le(blob, offset);
    const auto key = read_fourcc_le(blob, offset + 4);
    const auto length = read_u32(blob, offset + 12);
    const auto data_offset = offset + 16;
    if (length > blob.size() - data_offset) {
      break;
    }
    const auto data = blob.subspan(data_offset, length);
    info.found = true;
    if (vendor == kVendorAdobe) {
      if (key == kKeyKind) {
        info.kind = read_fourcc_le(data, 0);
      } else if (key == kKeyName) {
        info.name = pascal_string_utf8(data);
      } else if (key == kKeyCategory) {
        info.category = pascal_string_utf8(data);
      } else if (key == kKeyEntryPoint32) {
        info.entry_point_32 = c_string(data);
      } else if (key == kKeyEntryPoint64) {
        info.entry_point_64 = c_string(data);
      } else if (key == kKeyVersion) {
        info.version = read_u32(data, 0);
      } else if (key == kKeyModes) {
        info.supported_modes = read_u32(data, 0);
      } else if (key == kKeyCaseInfo && data.size() >= 28) {
        info.has_case_info = true;
        for (std::size_t c = 0; c < 7; ++c) {
          info.case_info[c] = {data[c * 4], data[c * 4 + 1], data[c * 4 + 2], data[c * 4 + 3]};
        }
      }
    }
    offset = data_offset + ((static_cast<std::size_t>(length) + 3U) & ~static_cast<std::size_t>(3U));
  }
  return info;
}

PiplInfo read_pipl_from_pe_bytes(Bytes image) {
  if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z') {
    return {};
  }
  const auto pe = read_u32(image, 0x3C);
  if (static_cast<std::size_t>(pe) + 24 > image.size() || image[pe] != 'P' || image[pe + 1] != 'E' ||
      image[pe + 2] != 0 || image[pe + 3] != 0) {
    return {};
  }
  const auto section_count = read_u16(image, pe + 6);
  const auto optional_size = read_u16(image, pe + 20);
  const std::size_t optional = static_cast<std::size_t>(pe) + 24;
  const auto magic = read_u16(image, optional);
  const std::size_t directories = optional + (magic == 0x20B ? 112 : 96);
  const auto resource_rva = read_u32(image, directories + 2 * 8);
  if (resource_rva == 0) {
    return {};
  }
  std::vector<Section> sections;
  const std::size_t table = optional + optional_size;
  for (std::size_t i = 0; i < section_count && i < 96; ++i) {
    const auto header = table + i * 40;
    if (header + 40 > image.size()) {
      break;
    }
    sections.push_back({read_u32(image, header + 12), read_u32(image, header + 8), read_u32(image, header + 20),
                        read_u32(image, header + 16)});
  }
  const auto base = rva_to_offset(sections, resource_rva);
  if (!base.has_value() || *base >= image.size()) {
    return {};
  }
  ResourceWalker walker{image, sections, *base, {}};
  walker.walk(0, 0);
  PiplInfo first;
  for (const auto& blob : walker.pipl_blobs) {
    auto info = parse_pipl_blob(blob);
    if (!info.found) {
      continue;
    }
    if (info.is_filter()) {
      return info;
    }
    if (!first.found) {
      first = info;
    }
  }
  return first;
}

PiplInfo read_pipl_from_pe_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  // Only the headers and the resource section are read: a startup scan over a
  // folder of plug-ins costs a few KB per file, not their full size.
  std::vector<std::uint8_t> prefix(4096);
  input.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
  prefix.resize(static_cast<std::size_t>(input.gcount()));
  if (prefix.size() < 0x40 || prefix[0] != 'M' || prefix[1] != 'Z') {
    return {};
  }
  const auto pe = read_u32(prefix, 0x3C);
  if (static_cast<std::size_t>(pe) + 24 > prefix.size()) {
    return {};
  }
  const auto section_count = read_u16(prefix, pe + 6);
  const auto optional_size = read_u16(prefix, pe + 20);
  const std::size_t optional = static_cast<std::size_t>(pe) + 24;
  const auto magic = read_u16(prefix, optional);
  const std::size_t directories = optional + (magic == 0x20B ? 112 : 96);
  const auto resource_rva = read_u32(prefix, directories + 2 * 8);
  const std::size_t table = optional + optional_size;
  std::uint32_t raw_pointer = 0;
  std::uint32_t raw_size = 0;
  for (std::size_t i = 0; i < section_count && i < 96; ++i) {
    const auto header = table + i * 40;
    if (header + 40 > prefix.size()) {
      break;
    }
    const auto virtual_address = read_u32(prefix, header + 12);
    const auto span = std::max(read_u32(prefix, header + 8), read_u32(prefix, header + 16));
    if (resource_rva != 0 && resource_rva >= virtual_address && resource_rva < virtual_address + span) {
      raw_pointer = read_u32(prefix, header + 20);
      raw_size = read_u32(prefix, header + 16);
      break;
    }
  }
  if (raw_size == 0 || raw_size > (64U << 20U)) {
    return {};
  }
  // A sparse image: headers in place, the resource section in place, zeros
  // between (nothing reads them). The walker then works unchanged.
  const std::size_t end = static_cast<std::size_t>(raw_pointer) + raw_size;
  std::vector<std::uint8_t> image(std::max(end, prefix.size()), 0);
  std::copy(prefix.begin(), prefix.end(), image.begin());
  input.clear();
  input.seekg(static_cast<std::streamoff>(raw_pointer), std::ios::beg);
  input.read(reinterpret_cast<char*>(image.data() + raw_pointer), static_cast<std::streamsize>(raw_size));
  image.resize(static_cast<std::size_t>(raw_pointer) + static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), 0)));
  if (image.size() < prefix.size()) {
    image.resize(prefix.size());
  }
  return read_pipl_from_pe_bytes(image);
}

std::vector<std::uint8_t> build_pipl_blob(const std::vector<PiplProperty>& properties) {
  std::vector<std::uint8_t> out;
  const auto put_u16 = [&out](std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
  };
  const auto put_u32 = [&out](std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  };
  put_u16(1);
  put_u32(0);
  put_u32(static_cast<std::uint32_t>(properties.size()));
  for (const auto& property : properties) {
    put_u32(kVendorAdobe);
    put_u32(property.key);
    put_u32(0);
    put_u32(static_cast<std::uint32_t>(property.data.size()));
    out.insert(out.end(), property.data.begin(), property.data.end());
    // The data is padded to a multiple of four bytes relative to the property,
    // not to the blob (the 10-byte header leaves properties 2-byte aligned).
    for (std::size_t padding = (4U - property.data.size() % 4U) % 4U; padding > 0; --padding) {
      out.push_back(0);
    }
  }
  return out;
}

}  // namespace patchy::pipl
