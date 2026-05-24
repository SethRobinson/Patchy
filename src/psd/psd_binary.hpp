#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace photoslop::psd {

class BigEndianReader {
public:
  explicit BigEndianReader(std::span<const std::uint8_t> bytes);

  [[nodiscard]] std::size_t position() const noexcept;
  [[nodiscard]] std::size_t remaining() const noexcept;

  [[nodiscard]] std::uint8_t read_u8();
  [[nodiscard]] std::uint16_t read_u16();
  [[nodiscard]] std::uint32_t read_u32();
  [[nodiscard]] std::uint64_t read_u64();
  [[nodiscard]] std::vector<std::uint8_t> read_bytes(std::size_t count);

  void skip(std::size_t count);

private:
  void require(std::size_t count) const;

  std::span<const std::uint8_t> bytes_;
  std::size_t offset_{0};
};

class BigEndianWriter {
public:
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t>& bytes() noexcept;

  void write_u8(std::uint8_t value);
  void write_u16(std::uint16_t value);
  void write_u32(std::uint32_t value);
  void write_u64(std::uint64_t value);
  void write_bytes(std::span<const std::uint8_t> bytes);

private:
  std::vector<std::uint8_t> bytes_;
};

struct Header {
  bool large_document{false};
  std::uint16_t channels{0};
  std::uint32_t height{0};
  std::uint32_t width{0};
  std::uint16_t depth{0};
  std::uint16_t color_mode{0};
};

[[nodiscard]] Header read_header(BigEndianReader& reader);
void write_header(BigEndianWriter& writer, const Header& header);

[[nodiscard]] std::string color_mode_name(std::uint16_t mode);

}  // namespace photoslop::psd
