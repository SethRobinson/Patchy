#pragma once

// Little-endian binary reader/writer shared by the flat image formats (BMP, ICO, TGA, PCX,
// Aseprite). PSD and IFF/ILBM are big-endian and use psd_binary.hpp instead. Keeping all
// serialization explicit fixed-width like this is what makes the file formats byte-identical
// across platforms (see AGENTS.md).

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace patchy {

class LittleEndianReader {
public:
  explicit LittleEndianReader(std::span<const std::uint8_t> bytes,
                              const char* underrun_message = "Data ended unexpectedly")
      : bytes_(bytes), underrun_message_(underrun_message) {}

  [[nodiscard]] std::size_t position() const noexcept {
    return offset_;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

  [[nodiscard]] std::uint8_t read_u8() {
    require(1);
    return bytes_[offset_++];
  }

  [[nodiscard]] std::uint16_t read_u16() {
    require(2);
    const auto value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes_[offset_]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes_[offset_ + 1U]) << 8U));
    offset_ += 2;
    return value;
  }

  [[nodiscard]] std::uint32_t read_u32() {
    require(4);
    const auto value = static_cast<std::uint32_t>(bytes_[offset_]) |
                       (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 8U) |
                       (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 16U) |
                       (static_cast<std::uint32_t>(bytes_[offset_ + 3U]) << 24U);
    offset_ += 4;
    return value;
  }

  [[nodiscard]] std::int32_t read_i32() {
    return static_cast<std::int32_t>(read_u32());
  }

  [[nodiscard]] std::uint64_t read_u64() {
    const auto low = read_u32();
    const auto high = read_u32();
    return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
  }

  void skip(std::size_t count) {
    require(count);
    offset_ += count;
  }

  void seek(std::size_t offset) {
    if (offset > bytes_.size()) {
      throw std::runtime_error(underrun_message_);
    }
    offset_ = offset;
  }

private:
  void require(std::size_t count) const {
    if (count > remaining()) {
      throw std::runtime_error(underrun_message_);
    }
  }

  std::span<const std::uint8_t> bytes_;
  const char* underrun_message_;
  std::size_t offset_{0};
};

class LittleEndianWriter {
public:
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
    return bytes_;
  }

  [[nodiscard]] std::vector<std::uint8_t>& bytes() noexcept {
    return bytes_;
  }

  void write_u8(std::uint8_t value) {
    bytes_.push_back(value);
  }

  void write_u16(std::uint16_t value) {
    bytes_.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  }

  void write_u32(std::uint32_t value) {
    bytes_.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
  }

  void write_i32(std::int32_t value) {
    write_u32(static_cast<std::uint32_t>(value));
  }

  void write_bytes(std::span<const std::uint8_t> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

private:
  std::vector<std::uint8_t> bytes_;
};

}  // namespace patchy
