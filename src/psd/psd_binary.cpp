#include "psd/psd_binary.hpp"

#include <array>
#include <stdexcept>

namespace photoslop::psd {

BigEndianReader::BigEndianReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

std::size_t BigEndianReader::position() const noexcept {
  return offset_;
}

std::size_t BigEndianReader::remaining() const noexcept {
  return bytes_.size() - offset_;
}

std::uint8_t BigEndianReader::read_u8() {
  require(1);
  return bytes_[offset_++];
}

std::uint16_t BigEndianReader::read_u16() {
  require(2);
  const auto value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes_[offset_]) << 8U) |
                                               static_cast<std::uint16_t>(bytes_[offset_ + 1]));
  offset_ += 2;
  return value;
}

std::uint32_t BigEndianReader::read_u32() {
  require(4);
  const auto value = (static_cast<std::uint32_t>(bytes_[offset_]) << 24U) |
                     (static_cast<std::uint32_t>(bytes_[offset_ + 1]) << 16U) |
                     (static_cast<std::uint32_t>(bytes_[offset_ + 2]) << 8U) |
                     static_cast<std::uint32_t>(bytes_[offset_ + 3]);
  offset_ += 4;
  return value;
}

std::uint64_t BigEndianReader::read_u64() {
  const auto high = static_cast<std::uint64_t>(read_u32());
  const auto low = static_cast<std::uint64_t>(read_u32());
  return (high << 32U) | low;
}

std::vector<std::uint8_t> BigEndianReader::read_bytes(std::size_t count) {
  require(count);
  std::vector<std::uint8_t> result(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                   bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + count));
  offset_ += count;
  return result;
}

void BigEndianReader::skip(std::size_t count) {
  require(count);
  offset_ += count;
}

void BigEndianReader::require(std::size_t count) const {
  if (count > remaining()) {
    throw std::runtime_error("Unexpected end of PSD data");
  }
}

const std::vector<std::uint8_t>& BigEndianWriter::bytes() const noexcept {
  return bytes_;
}

std::vector<std::uint8_t>& BigEndianWriter::bytes() noexcept {
  return bytes_;
}

void BigEndianWriter::write_u8(std::uint8_t value) {
  bytes_.push_back(value);
}

void BigEndianWriter::write_u16(std::uint16_t value) {
  bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void BigEndianWriter::write_u32(std::uint32_t value) {
  bytes_.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  bytes_.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void BigEndianWriter::write_u64(std::uint64_t value) {
  write_u32(static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
  write_u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
}

void BigEndianWriter::write_bytes(std::span<const std::uint8_t> bytes) {
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

Header read_header(BigEndianReader& reader) {
  const auto signature = reader.read_bytes(4);
  if (signature != std::vector<std::uint8_t>{'8', 'B', 'P', 'S'}) {
    throw std::runtime_error("Not a PSD/PSB file");
  }

  const auto version = reader.read_u16();
  if (version != 1 && version != 2) {
    throw std::runtime_error("Unsupported PSD/PSB version");
  }

  reader.skip(6);

  Header header;
  header.large_document = version == 2;
  header.channels = reader.read_u16();
  header.height = reader.read_u32();
  header.width = reader.read_u32();
  header.depth = reader.read_u16();
  header.color_mode = reader.read_u16();
  return header;
}

void write_header(BigEndianWriter& writer, const Header& header) {
  constexpr std::array<std::uint8_t, 4> signature{'8', 'B', 'P', 'S'};
  writer.write_bytes(signature);
  writer.write_u16(header.large_document ? 2 : 1);
  writer.write_u32(0);
  writer.write_u16(0);
  writer.write_u16(header.channels);
  writer.write_u32(header.height);
  writer.write_u32(header.width);
  writer.write_u16(header.depth);
  writer.write_u16(header.color_mode);
}

std::string color_mode_name(std::uint16_t mode) {
  switch (mode) {
    case 0:
      return "Bitmap";
    case 1:
      return "Grayscale";
    case 2:
      return "Indexed";
    case 3:
      return "RGB";
    case 4:
      return "CMYK";
    case 7:
      return "Multichannel";
    case 8:
      return "Duotone";
    case 9:
      return "Lab";
    default:
      return "Unknown";
  }
}

}  // namespace photoslop::psd
