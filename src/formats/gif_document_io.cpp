#include "formats/gif_document_io.hpp"

#include "formats/binary_le.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "core/palette.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace patchy::gif {

namespace {

// LZW code stream packed LSB-first into 255-byte sub-blocks.
class SubBlockBitWriter {
public:
  explicit SubBlockBitWriter(LittleEndianWriter& out) : out_(out) {}

  void write_code(std::uint16_t code, int width) {
    bit_buffer_ |= static_cast<std::uint32_t>(code) << bit_count_;
    bit_count_ += width;
    while (bit_count_ >= 8) {
      push_byte(static_cast<std::uint8_t>(bit_buffer_ & 0xffU));
      bit_buffer_ >>= 8U;
      bit_count_ -= 8;
    }
  }

  void finish() {
    if (bit_count_ > 0) {
      push_byte(static_cast<std::uint8_t>(bit_buffer_ & 0xffU));
      bit_buffer_ = 0;
      bit_count_ = 0;
    }
    flush_block();
    out_.write_u8(0);  // block terminator
  }

private:
  void push_byte(std::uint8_t byte) {
    block_.push_back(byte);
    if (block_.size() == 255) {
      flush_block();
    }
  }

  void flush_block() {
    if (block_.empty()) {
      return;
    }
    out_.write_u8(static_cast<std::uint8_t>(block_.size()));
    out_.write_bytes(block_);
    block_.clear();
  }

  LittleEndianWriter& out_;
  std::vector<std::uint8_t> block_;
  std::uint32_t bit_buffer_{0};
  int bit_count_{0};
};

[[nodiscard]] int color_table_size_bits(std::size_t palette_size) {
  int bits = 1;
  while ((std::size_t{1} << bits) < palette_size) {
    ++bits;
  }
  return std::max(1, bits);  // GIF tables hold at least 2 entries
}

// Color table padded with black to the declared power-of-two entry count.
void write_color_table(LittleEndianWriter& writer, std::span<const RgbColor> palette, int size_bits) {
  const auto table_entries = std::size_t{1} << size_bits;
  for (std::size_t i = 0; i < table_entries; ++i) {
    if (i < palette.size()) {
      writer.write_u8(palette[i].red);
      writer.write_u8(palette[i].green);
      writer.write_u8(palette[i].blue);
    } else {
      writer.write_u8(0);
      writer.write_u8(0);
      writer.write_u8(0);
    }
  }
}

void validate_frame_dimensions(std::int32_t width, std::int32_t height) {
  if (width <= 0 || height <= 0 || width > 0xffff || height > 0xffff) {
    throw std::runtime_error("GIF dimensions must be between 1 and 65535");
  }
}

void validate_indexed_image(std::int32_t width, std::int32_t height, std::span<const RgbColor> palette,
                            std::span<const std::uint8_t> indexes) {
  if (palette.empty() || palette.size() > 256) {
    throw std::runtime_error("GIF palettes must hold 1 to 256 colors");
  }
  if (indexes.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    throw std::runtime_error("GIF index data does not match the image dimensions");
  }
  for (const auto index : indexes) {
    if (index >= palette.size()) {
      throw std::runtime_error("GIF index references a missing palette color");
    }
  }
}

void encode_lzw(LittleEndianWriter& out, std::span<const std::uint8_t> indexes, int min_code_size) {
  SubBlockBitWriter bits(out);
  const auto clear_code = static_cast<std::uint16_t>(1U << min_code_size);
  const auto end_code = static_cast<std::uint16_t>(clear_code + 1);

  std::unordered_map<std::uint32_t, std::uint16_t> dictionary;
  auto next_code = static_cast<std::uint16_t>(clear_code + 2);
  int code_width = min_code_size + 1;
  const auto reset_dictionary = [&] {
    dictionary.clear();
    next_code = static_cast<std::uint16_t>(clear_code + 2);
    code_width = min_code_size + 1;
  };

  bits.write_code(clear_code, code_width);
  if (indexes.empty()) {
    bits.write_code(end_code, code_width);
    bits.finish();
    return;
  }

  std::uint16_t prefix = indexes[0];
  for (std::size_t i = 1; i < indexes.size(); ++i) {
    const auto symbol = indexes[i];
    const auto key = (static_cast<std::uint32_t>(prefix) << 8U) | symbol;
    const auto found = dictionary.find(key);
    if (found != dictionary.end()) {
      prefix = found->second;
      continue;
    }
    bits.write_code(prefix, code_width);
    // Width sync with decoders: a decoder assigns its matching entry one code LATER than
    // the encoder (it needs the next code's first symbol), so it grows the code width when
    // ITS next slot hits 2^width — which is exactly when the value just assigned here
    // equals 2^width. Checking after the increment desyncs by one entry (verified against
    // Pillow and Qt's qgif). At 12 bits the table clears instead of growing.
    dictionary.emplace(key, next_code);
    if (next_code == (1U << code_width) && code_width < 12) {
      ++code_width;
    }
    ++next_code;
    if (next_code >= 4096) {
      bits.write_code(clear_code, code_width);
      reset_dictionary();
    }
    prefix = symbol;
  }
  bits.write_code(prefix, code_width);
  bits.write_code(end_code, code_width);
  bits.finish();
}

}  // namespace

std::vector<std::uint8_t> encode(std::int32_t width, std::int32_t height, std::span<const RgbColor> palette,
                                 std::span<const std::uint8_t> indexes, int transparent_index) {
  validate_frame_dimensions(width, height);
  validate_indexed_image(width, height, palette, indexes);

  LittleEndianWriter writer;
  for (const char c : {'G', 'I', 'F', '8', '9', 'a'}) {
    writer.write_u8(static_cast<std::uint8_t>(c));
  }
  const auto size_bits = color_table_size_bits(palette.size());
  writer.write_u16(static_cast<std::uint16_t>(width));
  writer.write_u16(static_cast<std::uint16_t>(height));
  writer.write_u8(static_cast<std::uint8_t>(0x80U | 0x70U | static_cast<unsigned>(size_bits - 1)));
  writer.write_u8(0);  // background color index
  writer.write_u8(0);  // pixel aspect ratio
  write_color_table(writer, palette, size_bits);
  if (transparent_index >= 0 && static_cast<std::size_t>(transparent_index) < palette.size()) {
    writer.write_u8(0x21);  // extension introducer
    writer.write_u8(0xf9);  // graphic control label
    writer.write_u8(4);
    writer.write_u8(0x01);  // transparency flag
    writer.write_u16(0);    // delay
    writer.write_u8(static_cast<std::uint8_t>(transparent_index));
    writer.write_u8(0);
  }
  writer.write_u8(0x2c);  // image descriptor
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u16(static_cast<std::uint16_t>(width));
  writer.write_u16(static_cast<std::uint16_t>(height));
  writer.write_u8(0);  // no local table, not interlaced

  const int min_code_size = std::max(2, size_bits);
  writer.write_u8(static_cast<std::uint8_t>(min_code_size));
  encode_lzw(writer, indexes, min_code_size);
  writer.write_u8(0x3b);  // trailer
  return std::move(writer.bytes());
}

std::vector<std::uint8_t> encode_animation(std::int32_t width, std::int32_t height,
                                           std::span<const GifFrame> frames) {
  validate_frame_dimensions(width, height);
  if (frames.empty()) {
    throw std::runtime_error("GIF animations need at least one frame");
  }
  for (const auto& frame : frames) {
    validate_indexed_image(width, height, frame.palette, frame.indexes);
    if (frame.transparent_index >= static_cast<int>(frame.palette.size())) {
      throw std::runtime_error("GIF transparent index references a missing palette color");
    }
  }

  LittleEndianWriter writer;
  for (const char c : {'G', 'I', 'F', '8', '9', 'a'}) {
    writer.write_u8(static_cast<std::uint8_t>(c));
  }
  const auto global_bits = color_table_size_bits(frames[0].palette.size());
  writer.write_u16(static_cast<std::uint16_t>(width));
  writer.write_u16(static_cast<std::uint16_t>(height));
  writer.write_u8(static_cast<std::uint8_t>(0x80U | 0x70U | static_cast<unsigned>(global_bits - 1)));
  writer.write_u8(0);  // background color index
  writer.write_u8(0);  // pixel aspect ratio
  write_color_table(writer, frames[0].palette, global_bits);

  // NETSCAPE2.0 application extension: loop count 0 = forever.
  writer.write_u8(0x21);
  writer.write_u8(0xff);
  writer.write_u8(11);
  for (const char c : {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'}) {
    writer.write_u8(static_cast<std::uint8_t>(c));
  }
  writer.write_u8(3);  // sub-block: id 1 + loop count u16
  writer.write_u8(1);
  writer.write_u16(0);
  writer.write_u8(0);  // block terminator

  for (std::size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const auto& frame = frames[frame_index];
    const bool transparent = frame.transparent_index >= 0;
    writer.write_u8(0x21);  // extension introducer
    writer.write_u8(0xf9);  // graphic control label
    writer.write_u8(4);
    // Disposal 2 (restore to background) so every full-canvas frame displays alone.
    writer.write_u8(static_cast<std::uint8_t>(0x08U | (transparent ? 0x01U : 0x00U)));
    writer.write_u16(frame.delay_cs);
    writer.write_u8(transparent ? static_cast<std::uint8_t>(frame.transparent_index) : 0);
    writer.write_u8(0);

    writer.write_u8(0x2c);  // image descriptor
    writer.write_u16(0);
    writer.write_u16(0);
    writer.write_u16(static_cast<std::uint16_t>(width));
    writer.write_u16(static_cast<std::uint16_t>(height));
    const auto frame_bits = color_table_size_bits(frame.palette.size());
    if (frame_index == 0) {
      writer.write_u8(0);  // the global table is frame 1's palette
    } else {
      writer.write_u8(static_cast<std::uint8_t>(0x80U | static_cast<unsigned>(frame_bits - 1)));
      write_color_table(writer, frame.palette, frame_bits);
    }
    const int min_code_size = std::max(2, frame_bits);
    writer.write_u8(static_cast<std::uint8_t>(min_code_size));
    encode_lzw(writer, frame.indexes, min_code_size);
  }
  writer.write_u8(0x3b);  // trailer
  return std::move(writer.bytes());
}

void write_animation_file(std::int32_t width, std::int32_t height, std::span<const GifFrame> frames,
                          const std::filesystem::path& path) {
  formats::write_file_bytes(path, encode_animation(width, height, frames), "GIF");
}

std::optional<std::uint16_t> parse_layer_name_delay_cs(std::string_view layer_name) {
  while (!layer_name.empty() && (layer_name.back() == ' ' || layer_name.back() == '\t')) {
    layer_name.remove_suffix(1);
  }
  if (layer_name.empty() || layer_name.back() != 's') {
    return std::nullopt;
  }
  const auto separator = layer_name.find_last_of(" \t");
  auto token = separator == std::string_view::npos ? layer_name : layer_name.substr(separator + 1);
  token.remove_suffix(1);  // the trailing 's'
  const auto dot = token.find('.');
  if (dot != std::string_view::npos && token.find('.', dot + 1) != std::string_view::npos) {
    return std::nullopt;
  }
  const auto int_part = dot == std::string_view::npos ? token : token.substr(0, dot);
  const auto fraction = dot == std::string_view::npos ? std::string_view{} : token.substr(dot + 1);
  if (int_part.empty() && fraction.empty()) {
    return std::nullopt;
  }
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  for (const char c : int_part) {
    if (!is_digit(c)) {
      return std::nullopt;
    }
  }
  for (const char c : fraction) {
    if (!is_digit(c)) {
      return std::nullopt;
    }
  }
  std::uint64_t centiseconds = 0;
  for (const char c : int_part) {
    centiseconds = centiseconds * 10 + static_cast<std::uint64_t>(c - '0');
    if (centiseconds > 0xffff) {
      centiseconds = 0x10000;  // clamps below; stops the accumulator from overflowing
      break;
    }
  }
  centiseconds *= 100;
  if (!fraction.empty()) {
    centiseconds += static_cast<std::uint64_t>(fraction[0] - '0') * 10;
  }
  if (fraction.size() >= 2) {
    centiseconds += static_cast<std::uint64_t>(fraction[1] - '0');
  }
  if (fraction.size() >= 3 && fraction[2] >= '5') {
    ++centiseconds;  // round half-up from the third fraction digit
  }
  return static_cast<std::uint16_t>(std::min<std::uint64_t>(centiseconds, 0xffff));
}

std::string format_delay_seconds_token(std::uint16_t delay_cs) {
  const auto whole = std::to_string(delay_cs / 100);
  const auto remainder = delay_cs % 100;
  if (remainder == 0) {
    return whole + "s";
  }
  if (remainder % 10 == 0) {
    return whole + "." + std::to_string(remainder / 10) + "s";
  }
  auto fraction = std::to_string(remainder);
  if (fraction.size() == 1) {
    fraction.insert(fraction.begin(), '0');
  }
  return whole + "." + fraction + "s";
}

std::vector<std::uint8_t> write(const Document& document) {
  const auto indexed =
      document.palette_editing().has_value() && !document.palette_editing()->palette.colors.empty()
          ? indexed_flatten_for_palette_mode(document)
          : indexed_flatten_quantized(document, 256, kQuantizeAlphaThreshold);
  return encode(indexed.width, indexed.height, indexed.palette, indexed.indexes, indexed.transparent_index);
}

void write_file(const Document& document, const std::filesystem::path& path) {
  formats::write_file_bytes(path, write(document), "GIF");
}

}  // namespace patchy::gif
