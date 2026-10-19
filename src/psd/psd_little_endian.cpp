#include "psd/psd_little_endian.hpp"

#include "psd/psd_io_internal.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace patchy::psd {
namespace {

// A malformed or unrecognized structure; every converter catches it and drops the block.
class WalkError : public std::runtime_error {
 public:
  WalkError() : std::runtime_error("Photoshop layer data in little-endian byte order is malformed") {}
};

// Walks a byte range of `data` as little-endian fields, rewriting each one big-endian in
// place as it is read. Raw byte runs (pixel samples, ASCII ids, embedded files) are
// skipped untouched.
class Walker {
 public:
  Walker(std::vector<std::uint8_t>& data, std::size_t begin, std::size_t end) : data_(&data), position_(begin), end_(end) {}

  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] std::size_t end() const noexcept { return end_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return end_ - position_; }

  void require(std::uint64_t count) const {
    if (count > remaining()) {
      throw WalkError();
    }
  }

  std::uint8_t u8() {
    require(1);
    return (*data_)[position_++];
  }

  std::uint16_t u16() { return static_cast<std::uint16_t>(swap(2)); }
  std::uint32_t u32() { return static_cast<std::uint32_t>(swap(4)); }
  std::uint64_t u64() { return swap(8); }

  // A four-character code: little-endian on the wire, so "MIB8" becomes "8BIM".
  std::string ostype() {
    require(4);
    swap(4);
    const auto* bytes = data_->data() + position_ - 4U;
    return std::string(reinterpret_cast<const char*>(bytes), 4U);
  }

  void skip(std::uint64_t count) {
    require(count);
    position_ += static_cast<std::size_t>(count);
  }

  // A child walker over the next `count` bytes; this walker moves past them.
  Walker sub(std::uint64_t count) {
    require(count);
    Walker child(*data_, position_, position_ + static_cast<std::size_t>(count));
    position_ += static_cast<std::size_t>(count);
    return child;
  }

  // The rest must be zero padding: anything else means the layout was not what the walk
  // expected, and a guessed swap would be worse than dropping the block.
  void finish_with_padding() const {
    for (auto index = position_; index < end_; ++index) {
      if ((*data_)[index] != 0U) {
        throw WalkError();
      }
    }
  }

 private:
  std::uint64_t swap(std::size_t width) {
    require(width);
    auto* bytes = data_->data() + position_;
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
      value |= static_cast<std::uint64_t>(bytes[index]) << (8U * index);
    }
    std::reverse(bytes, bytes + width);
    position_ += width;
    return value;
  }

  std::vector<std::uint8_t>* data_;
  std::size_t position_;
  std::size_t end_;
};

constexpr int kMaxDescriptorDepth = 64;

void walk_unicode(Walker& walker) {
  const auto units = walker.u32();
  walker.require(static_cast<std::uint64_t>(units) * 2U);
  for (std::uint32_t index = 0; index < units; ++index) {
    (void)walker.u16();
  }
}

void walk_pascal(Walker& walker) {
  walker.skip(walker.u8());
}

// Class, key and enum ids: a u32 length, then that many ASCII bytes, or a four-character
// code when the length is zero.
void walk_id(Walker& walker) {
  const auto length = walker.u32();
  if (length == 0U) {
    (void)walker.ostype();
  } else {
    walker.skip(length);
  }
}

void walk_descriptor(Walker& walker, int depth);

// Mirrors read_descriptor_value (psd_descriptor.cpp) type for type.
void walk_value(Walker& walker, const std::string& type, int depth) {
  if (depth > kMaxDescriptorDepth) {
    throw WalkError();
  }
  if (type == "bool") {
    (void)walker.u8();
  } else if (type == "long") {
    (void)walker.u32();
  } else if (type == "comp" || type == "doub") {
    (void)walker.u64();
  } else if (type == "UntF") {
    (void)walker.ostype();
    (void)walker.u64();
  } else if (type == "TEXT") {
    walk_unicode(walker);
  } else if (type == "enum") {
    walk_id(walker);
    walk_id(walker);
  } else if (type == "Objc" || type == "GlbO") {
    walk_descriptor(walker, depth + 1);
  } else if (type == "VlLs") {
    const auto count = walker.u32();
    walker.require(static_cast<std::uint64_t>(count) * 4U);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto item_type = walker.ostype();
      walk_value(walker, item_type, depth + 1);
    }
  } else if (type == "tdta" || type == "alis") {
    walker.skip(walker.u32());
  } else if (type == "type" || type == "GlbC") {
    walk_unicode(walker);
    walk_id(walker);
  } else if (type == "UnFl") {
    (void)walker.ostype();
    const auto count = walker.u32();
    walker.require(static_cast<std::uint64_t>(count) * 8U);
    for (std::uint32_t index = 0; index < count; ++index) {
      (void)walker.u64();
    }
  } else if (type == "ObAr") {
    (void)walker.u32();
    walk_descriptor(walker, depth + 1);
  } else if (type == "obj ") {
    const auto count = walker.u32();
    walker.require(static_cast<std::uint64_t>(count) * 4U);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto form = walker.ostype();
      walk_unicode(walker);
      walk_id(walker);
      if (form == "prop") {
        walk_id(walker);
      } else if (form == "Enmr") {
        walk_id(walker);
        walk_id(walker);
      } else if (form == "rele" || form == "Idnt" || form == "indx") {
        (void)walker.u32();
      } else if (form == "name") {
        walk_unicode(walker);
      } else if (form != "Clss") {
        throw WalkError();
      }
    }
  } else {
    throw WalkError();
  }
}

void walk_descriptor(Walker& walker, int depth) {
  if (depth > kMaxDescriptorDepth) {
    throw WalkError();
  }
  walk_unicode(walker);
  walk_id(walker);
  const auto count = walker.u32();
  walker.require(static_cast<std::uint64_t>(count) * 8U);
  for (std::uint32_t index = 0; index < count; ++index) {
    walk_id(walker);
    const auto type = walker.ostype();
    walk_value(walker, type, depth + 1);
  }
}

// A descriptor version (16) and its descriptor.
void walk_versioned_descriptor(Walker& walker) {
  if (walker.u32() != 16U) {
    throw WalkError();
  }
  walk_descriptor(walker, 0);
}

// Runs `walk` over a copy of `payload`; the converted copy when the walk consumed the
// payload exactly (trailing zero padding allowed), else nullopt.
template <typename Walk>
std::optional<std::vector<std::uint8_t>> convert(std::span<const std::uint8_t> payload, Walk&& walk) {
  std::vector<std::uint8_t> data(payload.begin(), payload.end());
  try {
    Walker walker(data, 0, data.size());
    walk(walker);
    walker.finish_with_padding();
    return data;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// The shapes most descriptor-carrying blocks take, tried in turn for keys with no table
// entry: a bare descriptor version (SoCo, GdFl, PtFl, vstk, CgEd, vibA, blwh, artb, artd,
// abdd, cinf, ...), a u32 block version first (lfx2, lmfx, vogk), or a u16 one (clrL).
std::optional<std::vector<std::uint8_t>> convert_descriptor_block(std::span<const std::uint8_t> payload) {
  if (auto bare = convert(payload, [](Walker& walker) { walk_versioned_descriptor(walker); })) {
    return bare;
  }
  if (auto with_u32 = convert(payload, [](Walker& walker) {
        (void)walker.u32();
        walk_versioned_descriptor(walker);
      })) {
    return with_u32;
  }
  return convert(payload, [](Walker& walker) {
    (void)walker.u16();
    walk_versioned_descriptor(walker);
  });
}

void walk_u16_run(Walker& walker) {
  while (walker.remaining() >= 2U) {
    (void)walker.u16();
  }
}

// Path records (vmsk, vsms): 26 bytes each, a u16 selector first.
void walk_path_records(Walker& walker) {
  while (walker.remaining() >= 26U) {
    auto record = walker.sub(26U);
    const auto selector = record.u16();
    switch (selector) {
      case 0:
      case 3:
        // Length record: knot count, combine op, a constant field, then the
        // subpath/shape-group index at +12 (psd_vector.cpp parse_records).
        (void)record.u16();
        (void)record.u16();
        (void)record.u16();
        (void)record.u32();
        (void)record.u32();
        record.finish_with_padding();
        break;
      case 1:
      case 2:
      case 4:
      case 5:
        for (int index = 0; index < 6; ++index) {
          (void)record.u32();
        }
        break;
      case 6:
        record.finish_with_padding();
        break;
      case 7:
        // Clipboard record: a fixed-point rectangle and resolution.
        for (int index = 0; index < 5; ++index) {
          (void)record.u32();
        }
        record.finish_with_padding();
        break;
      case 8:
        (void)record.u16();
        record.finish_with_padding();
        break;
      default:
        throw WalkError();
    }
  }
}

void walk_curves(Walker& walker) {
  // curv: a zero prefix byte, u16 version, u32 channel bitmap, one (count, points) curve
  // per set bit, then the 'Crv ' extension: u16 version, u32 count, (channel, curve) pairs.
  if (walker.u8() != 0U) {
    throw WalkError();
  }
  (void)walker.u16();
  const auto bitmap = walker.u32();
  const auto walk_curve = [&walker] {
    const auto points = walker.u16();
    walker.require(static_cast<std::uint64_t>(points) * 4U);
    for (std::uint16_t index = 0; index < points; ++index) {
      (void)walker.u16();
      (void)walker.u16();
    }
  };
  for (int bit = 0; bit < 32; ++bit) {
    if ((bitmap & (1U << static_cast<unsigned>(bit))) != 0U) {
      walk_curve();
    }
  }
  if (walker.remaining() >= 4U) {
    if (walker.ostype() != "Crv ") {
      throw WalkError();
    }
    (void)walker.u16();
    const auto count = walker.u32();
    walker.require(static_cast<std::uint64_t>(count) * 4U);
    for (std::uint32_t index = 0; index < count; ++index) {
      (void)walker.u16();
      walk_curve();
    }
  }
}

void walk_levels(Walker& walker) {
  // levl: u16 version, 29 five-u16 records, then optionally 'Lvls', u16 version, u16
  // total record count and the records past 29.
  (void)walker.u16();
  for (int index = 0; index < 29 * 5; ++index) {
    (void)walker.u16();
  }
  if (walker.remaining() >= 4U) {
    if (walker.ostype() != "Lvls") {
      throw WalkError();
    }
    (void)walker.u16();
    const auto total = walker.u16();
    if (total < 29U) {
      throw WalkError();
    }
    for (int index = 0; index < (total - 29) * 5; ++index) {
      (void)walker.u16();
    }
  }
}

void walk_placed_layer(Walker& walker) {
  // PlLd / plLd (parse_plld_block): 'plcL', version, uuid, page, total pages, anti-alias,
  // layer type, eight transform doubles, warp version, warp descriptor.
  if (walker.ostype() != "plcL") {
    throw WalkError();
  }
  (void)walker.u32();
  walk_pascal(walker);
  for (int index = 0; index < 4; ++index) {
    (void)walker.u32();
  }
  for (int index = 0; index < 8; ++index) {
    (void)walker.u64();
  }
  (void)walker.u32();
  walk_versioned_descriptor(walker);
}

void walk_type_tool(Walker& walker) {
  // TySh (extract_type_tool_geometry): u16 version, six transform doubles, u16 text
  // version, the text descriptor, u16 warp version, the warp descriptor, four i32 bounds.
  (void)walker.u16();
  for (int index = 0; index < 6; ++index) {
    (void)walker.u64();
  }
  (void)walker.u16();
  walk_versioned_descriptor(walker);
  (void)walker.u16();
  walk_versioned_descriptor(walker);
  for (int index = 0; index < 4; ++index) {
    (void)walker.u32();
  }
}

void walk_metadata_items(Walker& walker) {
  // shmd: u32 count; per item sig, key, copy-on-sheet byte, 3 padding bytes, u32 length,
  // and a versioned descriptor in that length.
  const auto count = walker.u32();
  walker.require(static_cast<std::uint64_t>(count) * 16U);
  for (std::uint32_t index = 0; index < count; ++index) {
    if (walker.ostype() != "8BIM") {
      throw WalkError();
    }
    (void)walker.ostype();
    walker.skip(4U);
    auto item = walker.sub(walker.u32());
    walk_versioned_descriptor(item);
    item.finish_with_padding();
  }
}

// Mirrors parse_link_element (psd_smart_objects.cpp). The embedded file stays untouched.
void walk_link_elements(Walker& walker) {
  while (walker.remaining() >= 8U) {
    const auto length = walker.u64();
    auto element = walker.sub(length);
    const auto kind = element.ostype();
    if (kind != "liFD" && kind != "liFE") {
      throw WalkError();
    }
    const auto version = element.u32();
    if (version < 1U || version > 32U) {
      throw WalkError();
    }
    walk_pascal(element);
    walk_unicode(element);
    (void)element.ostype();
    (void)element.ostype();
    const auto data_size = element.u64();
    if (element.u8() != 0U) {
      walk_versioned_descriptor(element);
    }
    if (kind == "liFD") {
      element.skip(data_size);
    } else {
      walk_versioned_descriptor(element);
      if (version > 3U) {
        (void)element.u32();
        element.skip(4U);
        (void)element.u64();
        (void)element.u64();
      }
    }
    if (version >= 5U) {
      walk_unicode(element);
    }
    if (version >= 6U) {
      (void)element.u64();
    }
    if (version >= 7U) {
      (void)element.u8();
    }
    element.finish_with_padding();
    walker.skip(std::min<std::uint64_t>((4U - (length % 4U)) % 4U, walker.remaining()));
  }
  walker.finish_with_padding();
}

// Mirrors parse_single_pattern / read_plane (psd_patterns.cpp).
void walk_patterns(Walker& walker) {
  while (walker.remaining() >= 16U) {
    const auto length = walker.u32();
    auto pattern = walker.sub(length);
    const auto version = pattern.u32();
    const auto mode = pattern.u32();
    (void)pattern.u16();
    (void)pattern.u16();
    walk_unicode(pattern);
    walk_pascal(pattern);
    if (version == 1U && mode == 2U) {
      pattern.skip(256U * 3U);
    }
    if (pattern.u32() != 3U) {
      throw WalkError();
    }
    auto array = pattern.sub(pattern.u32());
    for (int index = 0; index < 4; ++index) {
      (void)array.u32();
    }
    const auto channels = array.u32();
    if (channels > 64U) {
      throw WalkError();
    }
    for (std::uint32_t slot = 0; slot < channels + 2U && array.remaining() > 0U; ++slot) {
      if (array.u32() == 0U) {
        continue;
      }
      const auto plane_length = array.u32();
      if (plane_length == 0U) {
        continue;
      }
      auto plane = array.sub(plane_length);
      (void)plane.u32();
      const auto top = static_cast<std::int32_t>(plane.u32());
      (void)plane.u32();
      const auto bottom = static_cast<std::int32_t>(plane.u32());
      (void)plane.u32();
      (void)plane.u16();
      const auto compression = plane.u8();
      if (compression == 1U) {
        const auto rows = static_cast<std::int64_t>(bottom) - static_cast<std::int64_t>(top);
        if (rows < 0) {
          throw WalkError();
        }
        plane.require(static_cast<std::uint64_t>(rows) * 2U);
        for (std::int64_t row = 0; row < rows; ++row) {
          (void)plane.u16();
        }
      }
      plane.skip(plane.remaining());
    }
    array.finish_with_padding();
    pattern.finish_with_padding();
    walker.skip(std::min<std::uint64_t>((4U - ((4U + length) % 4U)) % 4U, walker.remaining()));
  }
  walker.finish_with_padding();
}

bool is_one_byte_flag_block(std::string_view key) noexcept {
  return key == "clbl" || key == "infx" || key == "knko" || key == "tsly" || key == "iOpa" ||
         key == "lmgm" || key == "vmgm";
}

bool is_u32_block(std::string_view key) noexcept {
  return key == "lyid" || key == "lspf" || key == "lyvr" || key == "ffxi" || key == "sn2P" || key == "brst";
}

}  // namespace

std::string little_endian_ostype(std::span<const std::uint8_t, 4> bytes) {
  return std::string{static_cast<char>(bytes[3]), static_cast<char>(bytes[2]), static_cast<char>(bytes[1]),
                     static_cast<char>(bytes[0])};
}

std::optional<std::vector<std::uint8_t>> layer_block_to_big_endian(std::string_view key,
                                                                   std::span<const std::uint8_t> payload,
                                                                   const LittleEndianSource& /*source*/) {
  if (is_one_byte_flag_block(key)) {
    return std::vector<std::uint8_t>(payload.begin(), payload.end());
  }
  if (is_u32_block(key)) {
    return convert(payload, [](Walker& walker) {
      while (walker.remaining() >= 4U) {
        (void)walker.u32();
      }
    });
  }
  if (key == "luni") {
    return convert(payload, walk_unicode);
  }
  if (key == "lnsr") {
    return convert(payload, [](Walker& walker) { (void)walker.ostype(); });
  }
  if (key == "lclr" || key == "hue2" || key == "selc" || key == "mixr" || key == "post" || key == "thrs") {
    // All-u16 payloads (hue2: version, colorize byte pair, then u16 values to the end;
    // the byte pair swaps as a u16 of its own only if it were one, so it is skipped).
    return convert(payload, [key](Walker& walker) {
      if (key == "hue2") {
        (void)walker.u16();
        walker.skip(2U);
      }
      walk_u16_run(walker);
    });
  }
  if (key == "blnc") {
    return convert(payload, [](Walker& walker) {
      for (int index = 0; index < 9; ++index) {
        (void)walker.u16();
      }
    });
  }
  if (key == "brit") {
    return convert(payload, [](Walker& walker) {
      (void)walker.u16();
      (void)walker.u16();
      (void)walker.u16();
      walker.skip(std::min<std::size_t>(walker.remaining(), 2U));
    });
  }
  if (key == "expA") {
    return convert(payload, [](Walker& walker) {
      (void)walker.u16();
      (void)walker.u32();
      (void)walker.u32();
      (void)walker.u32();
    });
  }
  if (key == "nvrt") {
    return convert(payload, [](Walker&) {});
  }
  if (key == "levl") {
    return convert(payload, walk_levels);
  }
  if (key == "curv") {
    return convert(payload, walk_curves);
  }
  if (key == "fxrp") {
    return convert(payload, [](Walker& walker) {
      (void)walker.u64();
      (void)walker.u64();
    });
  }
  if (key == "lsct" || key == "lsdk") {
    return convert(payload, [](Walker& walker) {
      (void)walker.u32();
      if (walker.remaining() >= 8U) {
        if (walker.ostype() != "8BIM") {
          throw WalkError();
        }
        (void)walker.ostype();
      }
      if (walker.remaining() >= 4U) {
        (void)walker.u32();
      }
    });
  }
  if (key == "TySh") {
    return convert(payload, walk_type_tool);
  }
  if (key == "vmsk" || key == "vsms") {
    return convert(payload, [](Walker& walker) {
      (void)walker.u32();
      (void)walker.u32();
      walk_path_records(walker);
    });
  }
  if (key == "vscg") {
    return convert(payload, [](Walker& walker) {
      (void)walker.ostype();
      walk_versioned_descriptor(walker);
    });
  }
  if (key == "SoLd" || key == "SoLE") {
    return convert(payload, [](Walker& walker) {
      if (walker.ostype() != "soLD") {
        throw WalkError();
      }
      (void)walker.u32();
      walk_versioned_descriptor(walker);
    });
  }
  if (key == "PlLd" || key == "plLd") {
    return convert(payload, walk_placed_layer);
  }
  if (key == "shmd") {
    return convert(payload, walk_metadata_items);
  }
  // Blocks known to hold something other than a descriptor, which the generic attempt
  // below must never reinterpret: the legacy effects and type records (lfx2 and TySh
  // carry the same content), and smart filter caches.
  if (key == "lrFX" || key == "tySh" || key == "FEid" || key == "FXid" || key == "grdm" || key == "phfl") {
    return std::nullopt;
  }
  return convert_descriptor_block(payload);
}

std::optional<std::vector<std::uint8_t>> document_block_to_big_endian(std::string_view key,
                                                                      std::span<const std::uint8_t> payload,
                                                                      const LittleEndianSource& /*source*/) {
  if (key == "LMsk" || key == "FMsk") {
    // Overlay color space, four color components and opacity (u16 each), then the kind
    // byte (LMsk) and padding.
    return convert(payload, [](Walker& walker) {
      for (int index = 0; index < 6; ++index) {
        (void)walker.u16();
      }
      walker.skip(std::min<std::size_t>(walker.remaining(), 1U));
    });
  }
  if (key == "Patt" || key == "Pat2" || key == "Pat3") {
    return convert(payload, walk_patterns);
  }
  if (key == "lnk2" || key == "lnkD" || key == "lnk3" || key == "lnkE") {
    return convert(payload, walk_link_elements);
  }
  if (key == "FEid" || key == "FXid" || key == "Mt16" || key == "Mt32" || key == "Mtrn" || key == "Alph" ||
      key == "Anno" || key == "Txt2") {
    return std::nullopt;
  }
  return convert_descriptor_block(payload);
}

std::vector<std::uint8_t> layer_info_to_big_endian(std::span<const std::uint8_t> payload,
                                                   const LittleEndianSource& source,
                                                   std::vector<std::string>& dropped) {
  std::vector<std::uint8_t> data(payload.begin(), payload.end());
  std::vector<std::uint8_t> out;
  out.reserve(data.size());
  try {
    Walker walker(data, 0, data.size());
    const auto count_raw = static_cast<std::int16_t>(walker.u16());
    const auto count = static_cast<std::size_t>(std::abs(static_cast<int>(count_raw)));
    out.insert(out.end(), data.begin(), data.begin() + 2);

    struct Channel {
      std::uint16_t id{0};
      std::uint64_t length{0};
    };
    struct Record {
      std::int64_t height{0};
      std::optional<std::int64_t> mask_height;
      std::optional<std::int64_t> real_mask_height;
      std::vector<Channel> channels;
    };
    std::vector<Record> records;
    records.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      Record record;
      const auto record_start = walker.position();
      const auto top = static_cast<std::int32_t>(walker.u32());
      (void)walker.u32();
      const auto bottom = static_cast<std::int32_t>(walker.u32());
      (void)walker.u32();
      record.height = static_cast<std::int64_t>(bottom) - static_cast<std::int64_t>(top);
      const auto channel_count = walker.u16();
      walker.require(static_cast<std::uint64_t>(channel_count) * (source.large_document ? 10U : 6U));
      for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
        const auto id = walker.u16();
        const auto length = source.large_document ? walker.u64() : walker.u32();
        record.channels.push_back(Channel{id, length});
      }
      const auto signature = walker.ostype();
      if (signature != "8BIM" && signature != "8B64") {
        throw WalkError();
      }
      (void)walker.ostype();  // blend mode key
      walker.skip(4U);        // opacity, clipping, flags, filler
      out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(record_start),
                 data.begin() + static_cast<std::ptrdiff_t>(walker.position()));

      auto extra = walker.sub(walker.u32());
      std::vector<std::uint8_t> rebuilt;
      const auto copy_walked = [&data, &rebuilt](std::size_t from, std::size_t to) {
        rebuilt.insert(rebuilt.end(), data.begin() + static_cast<std::ptrdiff_t>(from),
                       data.begin() + static_cast<std::ptrdiff_t>(to));
      };
      if (extra.remaining() >= 8U) {
        // Mask data (read_layer_record's field order).
        const auto mask_start = extra.position();
        const auto mask_length = extra.u32();
        auto mask = extra.sub(mask_length);
        if (mask_length >= 18U) {
          const auto mask_top = static_cast<std::int32_t>(mask.u32());
          (void)mask.u32();
          const auto mask_bottom = static_cast<std::int32_t>(mask.u32());
          (void)mask.u32();
          record.mask_height = static_cast<std::int64_t>(mask_bottom) - static_cast<std::int64_t>(mask_top);
          (void)mask.u8();
          const auto flags = mask.u8();
          const bool parameters_only = (flags & 0x18U) == 0x10U;
          if (mask_length >= 36U && !parameters_only && mask.remaining() >= 18U) {
            (void)mask.u8();
            (void)mask.u8();
            const auto real_top = static_cast<std::int32_t>(mask.u32());
            (void)mask.u32();
            const auto real_bottom = static_cast<std::int32_t>(mask.u32());
            (void)mask.u32();
            record.real_mask_height = static_cast<std::int64_t>(real_bottom) - static_cast<std::int64_t>(real_top);
          }
          if ((flags & 0x10U) != 0U && mask.remaining() > 0U) {
            const auto parameters = mask.u8();
            if ((parameters & 0x01U) != 0U && mask.remaining() > 0U) {
              (void)mask.u8();
            }
            if ((parameters & 0x02U) != 0U && mask.remaining() >= 8U) {
              (void)mask.u64();
            }
            if ((parameters & 0x04U) != 0U && mask.remaining() > 0U) {
              (void)mask.u8();
            }
            if ((parameters & 0x08U) != 0U && mask.remaining() >= 8U) {
              (void)mask.u64();
            }
          }
        }
        // Blending ranges are byte quadruples (black low/high, white low/high).
        const auto ranges_length = extra.u32();
        extra.skip(ranges_length);
        if (extra.remaining() > 0U) {
          const auto name_length = extra.u8();
          const auto padded = ((1U + static_cast<std::size_t>(name_length) + 3U) / 4U) * 4U;
          extra.skip(padded - 1U);
        }
        copy_walked(mask_start, extra.position());
        while (extra.remaining() >= 12U) {
          const auto block_start = extra.position();
          const auto block_signature = extra.ostype();
          if (block_signature != "8BIM" && block_signature != "8B64") {
            break;
          }
          const auto key = extra.ostype();
          const bool wide = block_signature == "8B64" || (source.large_document && tagged_block_length_is_u64(key));
          if (wide && extra.remaining() < 8U) {
            break;
          }
          const auto length = wide ? extra.u64() : static_cast<std::uint64_t>(extra.u32());
          if (length > extra.remaining()) {
            break;
          }
          const auto payload_start = extra.position();
          extra.skip(length);
          const std::span<const std::uint8_t> block_payload(data.data() + payload_start,
                                                            static_cast<std::size_t>(length));
          if (auto converted = layer_block_to_big_endian(key, block_payload, source); converted.has_value()) {
            copy_walked(block_start, payload_start);
            rebuilt.insert(rebuilt.end(), converted->begin(), converted->end());
          } else {
            dropped.push_back(key);
          }
        }
      } else {
        copy_walked(extra.position(), extra.end());
      }
      const auto rebuilt_length = static_cast<std::uint32_t>(rebuilt.size());
      for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((rebuilt_length >> static_cast<unsigned>(shift)) & 0xFFU));
      }
      out.insert(out.end(), rebuilt.begin(), rebuilt.end());
      records.push_back(std::move(record));
    }

    // Channel image data: the compression code and RLE row counts swap; samples stay.
    const auto channel_data_start = walker.position();
    for (const auto& record : records) {
      for (const auto& channel : record.channels) {
        if (channel.length == 0U) {
          continue;
        }
        auto plane = walker.sub(channel.length);
        std::optional<std::int64_t> rows = record.height;
        if (channel.id == 0xFFFEU && record.mask_height.has_value()) {
          rows = record.mask_height;
        } else if (channel.id == 0xFFFDU) {
          rows = record.real_mask_height;  // no real mask: the reader skips the plane
        }
        if (!rows.has_value()) {
          continue;
        }
        const auto compression = plane.u16();
        if (compression == 1U) {
          if (*rows < 0) {
            throw WalkError();
          }
          plane.require(static_cast<std::uint64_t>(*rows) * (source.large_document ? 4U : 2U));
          for (std::int64_t row = 0; row < *rows; ++row) {
            if (source.large_document) {
              (void)plane.u32();
            } else {
              (void)plane.u16();
            }
          }
        }
      }
    }
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(channel_data_start), data.end());
  } catch (const WalkError&) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's Photoshop layer records are damaged"));
  }
  return out;
}

}  // namespace patchy::psd
