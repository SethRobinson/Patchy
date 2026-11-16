#include "formats/pdf_hidden_text_font.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace patchy::pdf {

namespace {

// Big-endian fields, as every sfnt table stores them.
class Table {
public:
  void u16(std::uint32_t value) {
    bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  }
  void i16(int value) { u16(static_cast<std::uint32_t>(value) & 0xFFFFU); }
  void u32(std::uint32_t value) {
    u16(value >> 16U);
    u16(value & 0xFFFFU);
  }
  void u8(std::uint32_t value) { bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFU)); }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
  std::vector<std::uint8_t> bytes_;
};

std::uint32_t tag(const char* name) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(name[0])) << 24U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(name[1])) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(name[2])) << 8U) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(name[3]));
}

// The sum of a table's bytes as big-endian 32-bit words, the last word padded with zeros.
std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) {
  std::uint32_t sum = 0;
  for (std::size_t index = 0; index < bytes.size(); index += 4) {
    std::uint32_t word = 0;
    for (std::size_t byte = 0; byte < 4; ++byte) {
      word = (word << 8U) | (index + byte < bytes.size() ? bytes[index + byte] : 0U);
    }
    sum += word;
  }
  return sum;
}

}  // namespace

std::vector<std::uint8_t> hidden_text_font() {
  constexpr int em = kHiddenTextFontUnitsPerEm;

  // The one glyph: a square from the baseline to one em up, one em wide.
  Table glyf;
  glyf.i16(1);  // one contour
  glyf.i16(0);
  glyf.i16(0);
  glyf.i16(em);
  glyf.i16(em);
  glyf.u16(3);  // the contour ends at point 3
  glyf.u16(0);  // no instructions
  for (int point = 0; point < 4; ++point) {
    glyf.u8(0x01);  // on the curve, both coordinates as 16-bit deltas
  }
  for (const int dx : {0, 0, em, 0}) {
    glyf.i16(dx);
  }
  for (const int dy : {0, em, 0, -em}) {
    glyf.i16(dy);
  }
  const auto glyph_length = static_cast<std::uint32_t>(glyf.bytes().size());

  Table head;
  head.u32(0x00010000U);  // version 1.0
  head.u32(0x00010000U);  // font revision 1.0
  head.u32(0U);           // checkSumAdjustment, filled in below
  head.u32(0x5F0F3CF5U);  // magic
  head.u16(0x0003U);      // baseline at y = 0, left side bearing at x = 0
  head.u16(static_cast<std::uint32_t>(em));
  for (int field = 0; field < 4; ++field) {
    head.u32(0U);  // created and modified: no dates, so the file is always the same bytes
  }
  head.i16(0);
  head.i16(0);
  head.i16(em);
  head.i16(em);
  head.u16(0U);  // regular
  head.u16(8U);  // smallest readable size
  head.i16(2);   // direction hint
  head.i16(0);   // short offsets in loca
  head.i16(0);

  Table hhea;
  hhea.u32(0x00010000U);
  hhea.i16(em);  // ascender
  hhea.i16(0);   // descender
  hhea.i16(0);   // line gap
  hhea.u16(static_cast<std::uint32_t>(em));  // widest advance
  hhea.i16(0);
  hhea.i16(0);
  hhea.i16(em);  // x max extent
  hhea.i16(1);   // caret slope: upright
  hhea.i16(0);
  hhea.i16(0);
  for (int reserved = 0; reserved < 4; ++reserved) {
    hhea.i16(0);
  }
  hhea.i16(0);
  hhea.u16(1U);  // one horizontal metric

  Table hmtx;
  hmtx.u16(static_cast<std::uint32_t>(em));
  hmtx.i16(0);

  Table loca;
  loca.u16(0U);
  loca.u16(glyph_length / 2U);  // short offsets count 2-byte units

  Table maxp;
  maxp.u32(0x00010000U);
  maxp.u16(1U);  // glyphs
  maxp.u16(4U);  // points
  maxp.u16(1U);  // contours
  maxp.u16(0U);
  maxp.u16(0U);
  maxp.u16(1U);  // zones
  for (int field = 0; field < 8; ++field) {
    maxp.u16(0U);
  }

  // Tables in tag order, which is also the order the directory must list them in.
  const std::array<std::pair<const char*, const Table*>, 6> tables{{
      {"glyf", &glyf}, {"head", &head}, {"hhea", &hhea}, {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}}};
  Table font;
  font.u32(0x00010000U);  // TrueType outlines
  font.u16(static_cast<std::uint32_t>(tables.size()));
  font.u16(64U);  // searchRange: 16 x the largest power of two not above 6
  font.u16(2U);   // entrySelector
  font.u16(32U);  // rangeShift: 6 x 16 - 64
  std::uint32_t offset = 12U + 16U * static_cast<std::uint32_t>(tables.size());
  std::size_t head_offset = 0;
  for (const auto& [name, table] : tables) {
    const auto length = static_cast<std::uint32_t>(table->bytes().size());
    font.u32(tag(name));
    font.u32(checksum(table->bytes()));
    font.u32(offset);
    font.u32(length);
    if (table == &head) {
      head_offset = offset;
    }
    offset += (length + 3U) / 4U * 4U;
  }
  auto bytes = font.bytes();
  for (const auto& [name, table] : tables) {
    bytes.insert(bytes.end(), table->bytes().begin(), table->bytes().end());
    while (bytes.size() % 4U != 0U) {
      bytes.push_back(0U);
    }
  }
  // The whole file sums to 0xB1B0AFBA once head's adjustment is in place.
  const std::uint32_t adjustment = 0xB1B0AFBAU - checksum(bytes);
  for (std::size_t byte = 0; byte < 4; ++byte) {
    bytes[head_offset + 8U + byte] = static_cast<std::uint8_t>((adjustment >> (24U - 8U * byte)) & 0xFFU);
  }
  return bytes;
}

}  // namespace patchy::pdf
