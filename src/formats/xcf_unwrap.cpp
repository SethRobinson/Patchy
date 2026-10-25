// The compressed wrappers GIMP saves .xcf files in (docs/xcf.md, "Container"):
// gzip through the vendored miniz, and bzip2 and xz through Patchy's own decoders, written
// from the published bzip2 format description and the xz file format and LZMA2
// specifications. Decode only: Patchy never writes these wrappers.

#include "formats/miniz/miniz.h"
#include "formats/xcf_internal.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace patchy::xcf::detail {

namespace {

void reserve_for(std::vector<std::uint8_t>& out, std::size_t input_size, std::uint64_t limit) {
  const auto guess = std::min<std::uint64_t>(static_cast<std::uint64_t>(input_size) * 4ULL, limit);
  out.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(guess, 1ULL << 30U)));
}

// --- gzip ---------------------------------------------------------------------------------

[[nodiscard]] std::size_t gzip_body_offset(std::span<const std::uint8_t> member) {
  if (member.size() < 18 || member[0] != 0x1F || member[1] != 0x8B || member[2] != 8) {
    throw ImportError("not a gzip member");
  }
  const auto flags = member[3];
  std::size_t position = 10;
  if ((flags & 0x04U) != 0) {  // FEXTRA
    if (position + 2 > member.size()) {
      throw ImportError("gzip header is truncated");
    }
    position += 2 + (static_cast<std::size_t>(member[position]) | (static_cast<std::size_t>(member[position + 1]) << 8U));
  }
  for (const auto bit : {0x08U, 0x10U}) {  // FNAME, FCOMMENT
    if ((flags & bit) != 0) {
      while (position < member.size() && member[position] != 0) {
        ++position;
      }
      ++position;
    }
  }
  if ((flags & 0x02U) != 0) {  // FHCRC
    position += 2;
  }
  if (position >= member.size()) {
    throw ImportError("gzip header is truncated");
  }
  return position;
}

// --- bzip2 --------------------------------------------------------------------------------

class BitReader {
public:
  explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::uint32_t bits(int count) {
    while (available_ < count) {
      if (position_ >= bytes_.size()) {
        throw ImportError("bzip2 stream is truncated");
      }
      buffer_ = (buffer_ << 8U) | bytes_[position_++];
      available_ += 8;
    }
    available_ -= count;
    return static_cast<std::uint32_t>((buffer_ >> available_) & ((1ULL << count) - 1ULL));
  }
  [[nodiscard]] bool bit() { return bits(1) != 0; }
  // Drops the bits left in the current byte (streams start byte-aligned).
  void align() { available_ -= available_ % 8; }
  [[nodiscard]] bool at_end() const noexcept { return position_ >= bytes_.size() && available_ < 8; }
  [[nodiscard]] std::size_t byte_position() const noexcept { return position_ - static_cast<std::size_t>(available_ / 8); }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_{0};
  std::uint64_t buffer_{0};
  int available_{0};
};

struct HuffmanTable {
  // Canonical code: for each length, the first code and the symbols in order.
  std::array<std::int32_t, 22> limit{};
  std::array<std::int32_t, 22> base{};
  std::array<std::uint16_t, 258> symbols{};
  int min_length{0};
  int max_length{0};
};

[[nodiscard]] HuffmanTable build_table(const std::array<std::uint8_t, 258>& lengths, int alphabet) {
  HuffmanTable table;
  table.min_length = 32;
  table.max_length = 0;
  for (int i = 0; i < alphabet; ++i) {
    table.min_length = std::min<int>(table.min_length, lengths[static_cast<std::size_t>(i)]);
    table.max_length = std::max<int>(table.max_length, lengths[static_cast<std::size_t>(i)]);
  }
  if (table.min_length < 1 || table.max_length > 20) {
    throw ImportError("bzip2 code lengths are out of range");
  }
  int index = 0;
  int code = 0;
  for (int length = table.min_length; length <= table.max_length; ++length) {
    table.base[static_cast<std::size_t>(length)] = code - index;
    for (int symbol = 0; symbol < alphabet; ++symbol) {
      if (lengths[static_cast<std::size_t>(symbol)] == length) {
        table.symbols[static_cast<std::size_t>(index++)] = static_cast<std::uint16_t>(symbol);
        ++code;
      }
    }
    table.limit[static_cast<std::size_t>(length)] = code - 1;
    code <<= 1;
  }
  return table;
}

[[nodiscard]] int decode_symbol(BitReader& reader, const HuffmanTable& table) {
  int length = table.min_length;
  int code = static_cast<int>(reader.bits(length));
  while (true) {
    if (code <= table.limit[static_cast<std::size_t>(length)]) {
      const int index = code - table.base[static_cast<std::size_t>(length)];
      if (index < 0 || index >= 258) {
        throw ImportError("bzip2 Huffman code is invalid");
      }
      return table.symbols[static_cast<std::size_t>(index)];
    }
    if (++length > table.max_length) {
      throw ImportError("bzip2 Huffman code is invalid");
    }
    code = (code << 1) | static_cast<int>(reader.bits(1));
  }
}

[[nodiscard]] std::array<std::uint32_t, 256> bzip2_crc_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i << 24U;
    for (int k = 0; k < 8; ++k) {
      c = (c & 0x80000000U) != 0 ? (c << 1U) ^ 0x04C11DB7U : c << 1U;
    }
    table[i] = c;
  }
  return table;
}

// One block: Huffman and MTF decode, the inverse Burrows-Wheeler transform, then the
// initial run-length stage (four equal bytes and a repeat count).
void decode_bzip2_block(BitReader& reader, std::uint32_t max_block, std::vector<std::uint8_t>& out,
                        std::uint64_t limit) {
  static const auto crc_table = bzip2_crc_table();
  const auto stored_crc = reader.bits(32);
  if (reader.bit()) {
    throw ImportError("randomized bzip2 blocks are not supported");
  }
  const auto origin = reader.bits(24);
  std::array<std::uint8_t, 256> in_use{};
  int used = 0;
  const auto ranges = reader.bits(16);
  for (int range = 0; range < 16; ++range) {
    if ((ranges & (0x8000U >> range)) != 0) {
      const auto bits = reader.bits(16);
      for (int k = 0; k < 16; ++k) {
        if ((bits & (0x8000U >> k)) != 0) {
          in_use[static_cast<std::size_t>(used++)] = static_cast<std::uint8_t>(range * 16 + k);
        }
      }
    }
  }
  if (used == 0) {
    throw ImportError("bzip2 block uses no symbols");
  }
  const int alphabet = used + 2;
  const int groups = static_cast<int>(reader.bits(3));
  if (groups < 2 || groups > 6) {
    throw ImportError("bzip2 block has a bad table count");
  }
  const auto selector_count = reader.bits(15);
  if (selector_count == 0) {
    throw ImportError("bzip2 block has no selectors");
  }
  std::vector<std::uint8_t> selectors(selector_count);
  std::array<std::uint8_t, 6> mtf_groups{0, 1, 2, 3, 4, 5};
  for (auto& selector : selectors) {
    int index = 0;
    while (reader.bit()) {
      if (++index >= groups) {
        throw ImportError("bzip2 selector is out of range");
      }
    }
    const auto value = mtf_groups[static_cast<std::size_t>(index)];
    for (int k = index; k > 0; --k) {
      mtf_groups[static_cast<std::size_t>(k)] = mtf_groups[static_cast<std::size_t>(k - 1)];
    }
    mtf_groups[0] = value;
    selector = value;
  }
  std::array<HuffmanTable, 6> tables;
  for (int group = 0; group < groups; ++group) {
    std::array<std::uint8_t, 258> lengths{};
    int length = static_cast<int>(reader.bits(5));
    for (int symbol = 0; symbol < alphabet; ++symbol) {
      while (true) {
        if (length < 1 || length > 20) {
          throw ImportError("bzip2 code length is out of range");
        }
        if (!reader.bit()) {
          break;
        }
        length += reader.bit() ? -1 : 1;
      }
      lengths[static_cast<std::size_t>(symbol)] = static_cast<std::uint8_t>(length);
    }
    tables[static_cast<std::size_t>(group)] = build_table(lengths, alphabet);
  }

  // Symbols -> the block's bytes before the BWT inverse.
  std::vector<std::uint8_t> block;
  block.reserve(max_block);
  std::array<std::uint8_t, 256> mtf{};
  for (int i = 0; i < 256; ++i) {
    mtf[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  std::array<std::uint32_t, 256> counts{};
  std::size_t selector_index = 0;
  int left_in_group = 0;
  const HuffmanTable* table = nullptr;
  std::uint32_t run = 0;
  std::uint32_t run_weight = 1;
  const auto flush_run = [&] {
    if (run == 0) {
      return;
    }
    if (block.size() + run > max_block) {
      throw ImportError("bzip2 block is longer than its size");
    }
    const auto value = in_use[mtf[0]];
    block.insert(block.end(), run, value);
    counts[value] += run;
    run = 0;
    run_weight = 1;
  };
  while (true) {
    if (left_in_group == 0) {
      if (selector_index >= selectors.size()) {
        throw ImportError("bzip2 block runs out of selectors");
      }
      table = &tables[selectors[selector_index++]];
      left_in_group = 50;
    }
    --left_in_group;
    const int symbol = decode_symbol(reader, *table);
    if (symbol <= 1) {  // RUNA, RUNB
      if (run_weight > max_block) {
        throw ImportError("bzip2 run is too long");
      }
      run += run_weight << static_cast<unsigned>(symbol);
      run_weight <<= 1U;
      continue;
    }
    flush_run();
    if (symbol == alphabet - 1) {  // end of block
      break;
    }
    const int index = symbol - 1;
    if (index >= used) {
      throw ImportError("bzip2 MTF index is out of range");
    }
    const auto value = mtf[static_cast<std::size_t>(index)];
    for (int k = index; k > 0; --k) {
      mtf[static_cast<std::size_t>(k)] = mtf[static_cast<std::size_t>(k - 1)];
    }
    mtf[0] = value;
    if (block.size() >= max_block) {
      throw ImportError("bzip2 block is longer than its size");
    }
    const auto byte = in_use[value];
    block.push_back(byte);
    ++counts[byte];
  }
  if (block.empty() || origin >= block.size()) {
    throw ImportError("bzip2 block origin is out of range");
  }
  // Inverse BWT.
  std::array<std::uint32_t, 256> starts{};
  std::uint32_t sum = 0;
  for (int i = 0; i < 256; ++i) {
    starts[static_cast<std::size_t>(i)] = sum;
    sum += counts[static_cast<std::size_t>(i)];
  }
  std::vector<std::uint32_t> next(block.size());
  for (std::uint32_t i = 0; i < block.size(); ++i) {
    next[starts[block[i]]++] = i;
  }
  std::uint32_t position = next[origin];
  std::uint32_t crc = 0xFFFFFFFFU;
  int same = 0;
  int previous = -1;
  for (std::size_t produced = 0; produced < block.size(); ++produced) {
    const auto byte = block[position];
    position = next[position];
    if (same == 4) {
      // A repeat count after four equal bytes.
      if (out.size() + byte > limit) {
        throw SizeLimitError("bzip2 data expands past the limit");
      }
      for (int k = 0; k < byte; ++k) {
        out.push_back(static_cast<std::uint8_t>(previous));
        crc = (crc << 8U) ^ crc_table[((crc >> 24U) ^ static_cast<std::uint32_t>(previous)) & 0xFFU];
      }
      same = 0;
      previous = -1;
      continue;
    }
    same = byte == previous ? same + 1 : 1;
    previous = byte;
    if (out.size() >= limit) {
      throw SizeLimitError("bzip2 data expands past the limit");
    }
    out.push_back(byte);
    crc = (crc << 8U) ^ crc_table[((crc >> 24U) ^ byte) & 0xFFU];
  }
  if ((~crc) != stored_crc && out.size() < limit) {
    throw ImportError("bzip2 block checksum mismatch");
  }
}

// --- xz / LZMA2 ---------------------------------------------------------------------------

class ByteCursor {
public:
  explicit ByteCursor(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
  [[nodiscard]] std::uint8_t byte() {
    if (position_ >= bytes_.size()) {
      throw ImportError("xz stream is truncated");
    }
    return bytes_[position_++];
  }
  [[nodiscard]] std::uint64_t vli() {
    std::uint64_t value = 0;
    for (int shift = 0; shift < 63; shift += 7) {
      const auto b = byte();
      value |= static_cast<std::uint64_t>(b & 0x7FU) << static_cast<unsigned>(shift);
      if ((b & 0x80U) == 0) {
        return value;
      }
    }
    throw ImportError("xz number is too long");
  }
  void skip(std::uint64_t count) {
    if (count > bytes_.size() - position_) {
      throw ImportError("xz stream is truncated");
    }
    position_ += static_cast<std::size_t>(count);
  }
  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  void seek(std::size_t position) {
    if (position > bytes_.size()) {
      throw ImportError("xz stream is truncated");
    }
    position_ = position;
  }
  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_{0};
};

class RangeDecoder {
public:
  void init(ByteCursor& input, std::size_t end) {
    input_ = &input;
    end_ = end;
    if (next() != 0) {
      throw ImportError("LZMA range coder starts badly");
    }
    code_ = 0;
    range_ = 0xFFFFFFFFU;
    for (int i = 0; i < 4; ++i) {
      code_ = (code_ << 8U) | next();
    }
    if (code_ == range_) {
      throw ImportError("LZMA range coder starts badly");
    }
  }
  [[nodiscard]] unsigned bit(std::uint16_t& probability) {
    const std::uint32_t bound = (range_ >> 11U) * probability;
    unsigned result = 0;
    if (code_ < bound) {
      range_ = bound;
      probability = static_cast<std::uint16_t>(probability + ((2048U - probability) >> 5U));
    } else {
      range_ -= bound;
      code_ -= bound;
      probability = static_cast<std::uint16_t>(probability - (probability >> 5U));
      result = 1;
    }
    normalize();
    return result;
  }
  [[nodiscard]] std::uint32_t direct(int count) {
    std::uint32_t result = 0;
    for (int i = 0; i < count; ++i) {
      range_ >>= 1U;
      code_ -= range_;
      const std::uint32_t t = 0U - (code_ >> 31U);
      code_ += range_ & t;
      result = (result << 1U) + (t + 1U);
      normalize();
    }
    return result;
  }
  [[nodiscard]] bool finished_cleanly() const noexcept { return code_ == 0; }

private:
  [[nodiscard]] std::uint8_t next() {
    if (input_->position() >= end_) {
      throw ImportError("LZMA chunk is truncated");
    }
    return input_->byte();
  }
  void normalize() {
    if (range_ < (1U << 24U)) {
      range_ <<= 8U;
      code_ = (code_ << 8U) | next();
    }
  }

  ByteCursor* input_{nullptr};
  std::size_t end_{0};
  std::uint32_t range_{0};
  std::uint32_t code_{0};
};

constexpr std::uint16_t kProbabilityInit = 1024;

struct LengthCoder {
  std::uint16_t choice{kProbabilityInit};
  std::uint16_t choice2{kProbabilityInit};
  std::array<std::array<std::uint16_t, 8>, 16> low{};
  std::array<std::array<std::uint16_t, 8>, 16> mid{};
  std::array<std::uint16_t, 256> high{};

  void reset() {
    choice = choice2 = kProbabilityInit;
    for (auto& set : low) set.fill(kProbabilityInit);
    for (auto& set : mid) set.fill(kProbabilityInit);
    high.fill(kProbabilityInit);
  }
};

[[nodiscard]] unsigned bit_tree(RangeDecoder& rc, std::uint16_t* probabilities, int bits) {
  unsigned m = 1;
  for (int i = 0; i < bits; ++i) {
    m = (m << 1U) + rc.bit(probabilities[m]);
  }
  return m - (1U << static_cast<unsigned>(bits));
}

[[nodiscard]] unsigned reverse_tree(RangeDecoder& rc, std::uint16_t* probabilities, int bits) {
  unsigned m = 1;
  unsigned symbol = 0;
  for (int i = 0; i < bits; ++i) {
    const unsigned bit = rc.bit(probabilities[m]);
    m = (m << 1U) + bit;
    symbol |= bit << static_cast<unsigned>(i);
  }
  return symbol;
}

[[nodiscard]] unsigned decode_length(RangeDecoder& rc, LengthCoder& coder, unsigned pos_state) {
  if (rc.bit(coder.choice) == 0) {
    return bit_tree(rc, coder.low[pos_state].data(), 3);
  }
  if (rc.bit(coder.choice2) == 0) {
    return 8 + bit_tree(rc, coder.mid[pos_state].data(), 3);
  }
  return 16 + bit_tree(rc, coder.high.data(), 8);
}

class LzmaDecoder {
public:
  void set_properties(std::uint8_t properties) {
    if (properties >= 9 * 5 * 5) {
      throw ImportError("LZMA properties are invalid");
    }
    lc_ = properties % 9;
    properties /= 9;
    lp_ = properties % 5;
    pb_ = properties / 5;
    if (lc_ + lp_ > 4) {
      throw ImportError("LZMA2 literal properties are invalid");
    }
    literals_.assign(static_cast<std::size_t>(0x300U) << static_cast<unsigned>(lc_ + lp_), kProbabilityInit);
  }
  void reset_state() {
    state_ = 0;
    reps_.fill(0);
    std::fill(literals_.begin(), literals_.end(), kProbabilityInit);
    for (auto& row : is_match_) row.fill(kProbabilityInit);
    for (auto& row : is_rep0_long_) row.fill(kProbabilityInit);
    is_rep_.fill(kProbabilityInit);
    is_rep_g0_.fill(kProbabilityInit);
    is_rep_g1_.fill(kProbabilityInit);
    is_rep_g2_.fill(kProbabilityInit);
    for (auto& row : pos_slot_) row.fill(kProbabilityInit);
    pos_special_.fill(kProbabilityInit);
    align_.fill(kProbabilityInit);
    length_.reset();
    rep_length_.reset();
  }
  void set_dictionary_limit(std::uint64_t size) { dictionary_size_ = size; }
  [[nodiscard]] bool has_properties() const noexcept { return !literals_.empty(); }

  // Decodes `size` bytes from the chunk into `out` (which is also the dictionary).
  void decode(RangeDecoder& rc, std::vector<std::uint8_t>& out, std::size_t dictionary_start, std::uint64_t size) {
    const std::uint64_t target = out.size() + size;
    const unsigned pos_mask = (1U << static_cast<unsigned>(pb_)) - 1U;
    const unsigned lp_mask = (1U << static_cast<unsigned>(lp_)) - 1U;
    const auto window = [&](std::uint64_t distance) -> std::uint8_t {
      if (distance >= out.size() - dictionary_start || distance >= dictionary_size_) {
        throw ImportError("LZMA distance is past the dictionary");
      }
      return out[out.size() - 1 - static_cast<std::size_t>(distance)];
    };
    while (out.size() < target) {
      const unsigned pos_state = static_cast<unsigned>(out.size()) & pos_mask;
      if (rc.bit(is_match_[state_][pos_state]) == 0) {
        const unsigned previous = out.size() > dictionary_start ? out.back() : 0;
        const auto context = ((static_cast<unsigned>(out.size()) & lp_mask) << static_cast<unsigned>(lc_)) +
                             (previous >> static_cast<unsigned>(8 - lc_));
        auto* probabilities = literals_.data() + static_cast<std::size_t>(0x300U) * context;
        unsigned symbol = 1;
        if (state_ >= 7) {
          unsigned match = window(reps_[0]);
          while (symbol < 0x100) {
            const unsigned match_bit = (match >> 7U) & 1U;
            match <<= 1U;
            const unsigned bit = rc.bit(probabilities[((1U + match_bit) << 8U) + symbol]);
            symbol = (symbol << 1U) | bit;
            if (match_bit != bit) {
              break;
            }
          }
        }
        while (symbol < 0x100) {
          symbol = (symbol << 1U) | rc.bit(probabilities[symbol]);
        }
        out.push_back(static_cast<std::uint8_t>(symbol));
        state_ = state_ < 4 ? 0 : (state_ < 10 ? state_ - 3 : state_ - 6);
        continue;
      }
      unsigned length = 0;
      if (rc.bit(is_rep_[state_]) != 0) {
        if (out.size() == dictionary_start) {
          throw ImportError("LZMA repeat before any data");
        }
        if (rc.bit(is_rep_g0_[state_]) == 0) {
          if (rc.bit(is_rep0_long_[state_][pos_state]) == 0) {
            state_ = state_ < 7 ? 9 : 11;
            out.push_back(window(reps_[0]));
            continue;
          }
        } else {
          std::uint32_t distance = 0;
          if (rc.bit(is_rep_g1_[state_]) == 0) {
            distance = reps_[1];
          } else {
            if (rc.bit(is_rep_g2_[state_]) == 0) {
              distance = reps_[2];
            } else {
              distance = reps_[3];
              reps_[3] = reps_[2];
            }
            reps_[2] = reps_[1];
          }
          reps_[1] = reps_[0];
          reps_[0] = distance;
        }
        length = decode_length(rc, rep_length_, pos_state);
        state_ = state_ < 7 ? 8 : 11;
      } else {
        reps_[3] = reps_[2];
        reps_[2] = reps_[1];
        reps_[1] = reps_[0];
        length = decode_length(rc, length_, pos_state);
        state_ = state_ < 7 ? 7 : 10;
        const unsigned length_state = std::min(length, 3U);
        const unsigned slot = bit_tree(rc, pos_slot_[length_state].data(), 6);
        std::uint32_t distance = slot;
        if (slot >= 4) {
          const unsigned direct_bits = (slot >> 1U) - 1U;
          distance = (2U | (slot & 1U)) << direct_bits;
          if (slot < 14) {
            distance += reverse_tree(rc, pos_special_.data() + distance - slot, static_cast<int>(direct_bits));
          } else {
            distance += rc.direct(static_cast<int>(direct_bits - 4)) << 4U;
            distance += reverse_tree(rc, align_.data(), 4);
          }
          if (distance == 0xFFFFFFFFU) {
            throw ImportError("LZMA2 chunks have no end marker");
          }
        }
        reps_[0] = distance;
      }
      length += 2;
      if (out.size() + length > target) {
        throw ImportError("LZMA match runs past the chunk");
      }
      const auto distance = reps_[0];
      (void)window(distance);
      for (unsigned i = 0; i < length; ++i) {
        out.push_back(out[out.size() - 1 - distance]);
      }
    }
  }

private:
  int lc_{0};
  int lp_{0};
  int pb_{0};
  unsigned state_{0};
  std::array<std::uint32_t, 4> reps_{};
  std::uint64_t dictionary_size_{0};
  std::vector<std::uint16_t> literals_;
  std::array<std::array<std::uint16_t, 16>, 12> is_match_{};
  std::array<std::array<std::uint16_t, 16>, 12> is_rep0_long_{};
  std::array<std::uint16_t, 12> is_rep_{};
  std::array<std::uint16_t, 12> is_rep_g0_{};
  std::array<std::uint16_t, 12> is_rep_g1_{};
  std::array<std::uint16_t, 12> is_rep_g2_{};
  std::array<std::array<std::uint16_t, 64>, 4> pos_slot_{};
  std::array<std::uint16_t, 115> pos_special_{};
  std::array<std::uint16_t, 16> align_{};
  LengthCoder length_;
  LengthCoder rep_length_;
};

// A big-endian 16-bit value, its two bytes read in order (two reads in one expression would
// be unsequenced).
[[nodiscard]] std::size_t read_be16(ByteCursor& input) {
  const std::size_t high = input.byte();
  const std::size_t low = input.byte();
  return (high << 8U) | low;
}

// One LZMA2 stream (the payload of an xz block) appended to `out`.
void decode_lzma2(ByteCursor& input, std::size_t end, std::uint64_t dictionary_size, std::vector<std::uint8_t>& out,
                  std::uint64_t limit) {
  LzmaDecoder decoder;
  decoder.set_dictionary_limit(dictionary_size);
  std::size_t dictionary_start = out.size();
  bool need_dictionary_reset = true;
  bool need_properties = true;
  while (true) {
    if (input.position() >= end) {
      throw ImportError("LZMA2 stream is truncated");
    }
    const auto control = input.byte();
    if (control == 0) {
      return;
    }
    if (control == 1 || control == 2) {
      if (control == 1) {
        dictionary_start = out.size();
        need_dictionary_reset = false;
      } else if (need_dictionary_reset) {
        throw ImportError("LZMA2 stream needs a dictionary reset");
      }
      const std::size_t size = read_be16(input) + 1U;
      if (out.size() + size > limit && limit >= (1ULL << 20U)) {
        throw SizeLimitError("xz data expands past the limit");
      }
      if (input.position() + size > end) {
        throw ImportError("LZMA2 chunk is truncated");
      }
      const auto bytes = input.bytes().subspan(input.position(), size);
      out.insert(out.end(), bytes.begin(), bytes.end());
      input.skip(size);
      continue;
    }
    if (control < 0x80) {
      throw ImportError("LZMA2 control byte is invalid");
    }
    const std::uint64_t unpacked = (static_cast<std::uint64_t>(control & 0x1FU) << 16U) + read_be16(input) + 1U;
    const std::size_t packed = read_be16(input) + 1U;
    const unsigned reset = (control >> 5U) & 3U;
    if (reset == 3) {
      dictionary_start = out.size();
      need_dictionary_reset = false;
    } else if (need_dictionary_reset) {
      throw ImportError("LZMA2 stream needs a dictionary reset");
    }
    if (reset >= 2) {
      decoder.set_properties(input.byte());
      need_properties = false;
    } else if (need_properties) {
      throw ImportError("LZMA2 chunk has no properties");
    }
    if (reset >= 1) {
      decoder.reset_state();
    }
    // A prefix request (a sniff, a limit under 1 MiB) decodes the chunk that reaches it and
    // stops there; LZMA2 chunks hold at most 2 MiB.
    const bool prefix = limit < (1ULL << 20U);
    if (!prefix && out.size() + unpacked > limit) {
      throw SizeLimitError("xz data expands past the limit");
    }
    const auto chunk_end = input.position() + packed;
    if (chunk_end > end) {
      throw ImportError("LZMA2 chunk is truncated");
    }
    RangeDecoder rc;
    rc.init(input, chunk_end);
    decoder.decode(rc, out, dictionary_start, unpacked);
    input.seek(chunk_end);
    if (prefix && out.size() >= limit) {
      throw SizeLimitError("xz prefix reached");
    }
  }
}

[[nodiscard]] std::size_t check_size(unsigned type) {
  if (type == 0) return 0;
  if (type <= 3) return 4;
  if (type <= 6) return 8;
  if (type <= 9) return 16;
  if (type <= 12) return 32;
  return 64;
}

}  // namespace

std::vector<std::uint8_t> decompress_gzip(std::span<const std::uint8_t> bytes, std::uint64_t limit) {
  std::vector<std::uint8_t> out;
  reserve_for(out, bytes.size(), limit);
  std::size_t position = 0;
  bool first = true;
  while (position + 18 <= bytes.size() && bytes[position] == 0x1F && bytes[position + 1] == 0x8B) {
    const auto member = bytes.subspan(position);
    const auto body = gzip_body_offset(member);
    mz_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
      throw ImportError("inflate could not start");
    }
    const auto input = member.subspan(body);
    std::size_t fed = 0;
    std::array<std::uint8_t, 1U << 16U> buffer{};
    int status = MZ_OK;
    while (true) {
      if (stream.avail_in == 0 && fed < input.size()) {
        const auto chunk = std::min<std::size_t>(input.size() - fed, 1U << 30U);
        stream.next_in = input.data() + fed;
        stream.avail_in = static_cast<unsigned int>(chunk);
        fed += chunk;
      }
      stream.next_out = buffer.data();
      stream.avail_out = static_cast<unsigned int>(buffer.size());
      status = mz_inflate(&stream, MZ_NO_FLUSH);
      const auto produced = buffer.size() - stream.avail_out;
      const auto room = limit > out.size() ? limit - out.size() : 0;
      out.insert(out.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(std::min<std::uint64_t>(produced, room)));
      if (produced > room) {
        mz_inflateEnd(&stream);
        if (limit < (1ULL << 20U)) {
          return out;  // a sniff asked for the first bytes only
        }
        throw SizeLimitError("gzip data expands past the limit");
      }
      if (status == MZ_STREAM_END) {
        break;
      }
      if (status != MZ_OK && status != MZ_BUF_ERROR) {
        mz_inflateEnd(&stream);
        throw ImportError("gzip data is damaged");
      }
      if (stream.avail_in == 0 && produced == 0) {
        mz_inflateEnd(&stream);
        if (limit < (1ULL << 20U)) {
          return out;
        }
        throw ImportError("gzip data is truncated");
      }
    }
    const auto consumed = body + (fed - stream.avail_in);
    mz_inflateEnd(&stream);
    position += consumed + 8;  // CRC32 and ISIZE
    first = false;
  }
  if (first) {
    throw ImportError("not a gzip stream");
  }
  return out;
}

std::vector<std::uint8_t> decompress_bzip2(std::span<const std::uint8_t> bytes, std::uint64_t limit) {
  std::vector<std::uint8_t> out;
  reserve_for(out, bytes.size(), limit);
  BitReader reader(bytes);
  bool first = true;
  while (true) {
    reader.align();
    if (reader.at_end() && !first) {
      break;
    }
    if (reader.bits(8) != 'B' || reader.bits(8) != 'Z' || reader.bits(8) != 'h') {
      if (first) {
        throw ImportError("not a bzip2 stream");
      }
      break;  // trailing bytes after the last stream
    }
    const auto level = reader.bits(8);
    if (level < '1' || level > '9') {
      throw ImportError("bzip2 block size is invalid");
    }
    const auto max_block = (level - '0') * 100000U;
    first = false;
    while (true) {
      const auto magic_high = reader.bits(24);
      const auto magic_low = reader.bits(24);
      if (magic_high == 0x314159U && magic_low == 0x265359U) {
        try {
          decode_bzip2_block(reader, max_block, out, limit);
        } catch (const SizeLimitError&) {
          if (limit < (1ULL << 20U)) {
            return out;
          }
          throw;
        }
        if (out.size() >= limit && limit < (1ULL << 20U)) {
          return out;
        }
        continue;
      }
      if (magic_high == 0x177245U && magic_low == 0x385090U) {
        (void)reader.bits(32);  // stream CRC
        break;
      }
      throw ImportError("bzip2 block magic is invalid");
    }
    if (reader.at_end()) {
      break;
    }
  }
  return out;
}

std::vector<std::uint8_t> decompress_xz(std::span<const std::uint8_t> bytes, std::uint64_t limit) {
  std::vector<std::uint8_t> out;
  reserve_for(out, bytes.size(), limit);
  ByteCursor input(bytes);
  bool first = true;
  while (input.position() < input.size()) {
    // Stream padding: zero bytes in groups of four between streams.
    if (!first && bytes[input.position()] == 0) {
      input.skip(1);
      continue;
    }
    if (input.size() - input.position() < 12 ||
        std::memcmp(bytes.data() + input.position(), "\xFD" "7zXZ\0", 6) != 0) {
      if (first) {
        throw ImportError("not an xz stream");
      }
      break;
    }
    input.skip(6);
    if (input.byte() != 0) {
      throw ImportError("xz stream flags are invalid");
    }
    const unsigned check_type = input.byte() & 0x0FU;
    input.skip(4);  // header CRC32
    first = false;
    while (true) {
      const auto header_size_byte = input.byte();
      if (header_size_byte == 0) {
        // The index: records, padding and CRC32, then the 12-byte stream footer.
        const auto records = input.vli();
        for (std::uint64_t i = 0; i < records; ++i) {
          (void)input.vli();
          (void)input.vli();
        }
        while ((input.position() % 4) != 0) {
          input.skip(1);
        }
        input.skip(4);
        input.skip(12);
        break;
      }
      const std::size_t header_start = input.position() - 1;
      const std::size_t header_size = (static_cast<std::size_t>(header_size_byte) + 1U) * 4U;
      const auto flags = input.byte();
      const unsigned filters = (flags & 3U) + 1U;
      if ((flags & 0x40U) != 0) {
        (void)input.vli();
      }
      if ((flags & 0x80U) != 0) {
        (void)input.vli();
      }
      std::uint64_t dictionary_size = 0;
      bool lzma2 = false;
      for (unsigned f = 0; f < filters; ++f) {
        const auto id = input.vli();
        const auto size = input.vli();
        if (id == 0x21 && f + 1 == filters && size == 1) {
          const auto bits = input.byte() & 0x3FU;
          if (bits > 40) {
            throw ImportError("xz dictionary size is invalid");
          }
          dictionary_size = bits == 40 ? 0xFFFFFFFFULL
                                       : (static_cast<std::uint64_t>(2U | (bits & 1U)) << (bits / 2U + 11U));
          lzma2 = true;
        } else {
          throw ImportError("xz filter " + std::to_string(id) + " is not supported");
        }
      }
      if (!lzma2) {
        throw ImportError("xz block has no LZMA2 filter");
      }
      input.seek(header_start + header_size);
      try {
        decode_lzma2(input, input.size(), dictionary_size, out, limit);
      } catch (const SizeLimitError&) {
        if (limit < (1ULL << 20U)) {
          return out;
        }
        throw;
      } catch (const ImportError&) {
        if (limit < (1ULL << 20U) && out.size() >= 9) {
          return out;  // a sniff reads only the first chunk
        }
        throw;
      }
      while ((input.position() % 4) != 0) {
        input.skip(1);
      }
      input.skip(check_size(check_type));
    }
  }
  return out;
}

}  // namespace patchy::xcf::detail
