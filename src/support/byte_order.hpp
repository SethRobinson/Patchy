#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Fixed-width big- and little-endian integer reads and writes over raw bytes,
// the primitives every file format in Patchy needs (AGENTS.md: serialization is
// fixed-width and explicit-endian, never a native struct or host-endian value).
// The span overloads take the byte offset the caller has already bounds-checked,
// so a reader walking a buffer keeps one `bytes` and advances `offset`. The PSD
// codec keeps its own positional, length-checked psd::BigEndianReader/Writer.

namespace patchy {

[[nodiscard]] constexpr std::uint16_t read_u16_be(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[1]));
}

[[nodiscard]] constexpr std::uint32_t read_u32_be(const std::uint8_t* bytes) noexcept {
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) | (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] constexpr std::uint64_t read_u64_be(const std::uint8_t* bytes) noexcept {
  return (static_cast<std::uint64_t>(read_u32_be(bytes)) << 32U) |
         static_cast<std::uint64_t>(read_u32_be(bytes + 4));
}

[[nodiscard]] constexpr std::uint16_t read_u16_le(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) |
                                    (static_cast<std::uint16_t>(bytes[1]) << 8U));
}

[[nodiscard]] constexpr std::uint32_t read_u32_le(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] constexpr std::uint64_t read_u64_le(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint64_t>(read_u32_le(bytes)) |
         (static_cast<std::uint64_t>(read_u32_le(bytes + 4)) << 32U);
}

// `offset` plus the value's width must lie within `bytes` (the caller checked).
[[nodiscard]] constexpr std::uint16_t read_u16_be(std::span<const std::uint8_t> bytes,
                                                  std::size_t offset) noexcept {
  return read_u16_be(bytes.data() + offset);
}
[[nodiscard]] constexpr std::uint32_t read_u32_be(std::span<const std::uint8_t> bytes,
                                                  std::size_t offset) noexcept {
  return read_u32_be(bytes.data() + offset);
}
[[nodiscard]] constexpr std::uint64_t read_u64_be(std::span<const std::uint8_t> bytes,
                                                  std::size_t offset) noexcept {
  return read_u64_be(bytes.data() + offset);
}
[[nodiscard]] constexpr std::uint16_t read_u16_le(std::span<const std::uint8_t> bytes,
                                                  std::size_t offset) noexcept {
  return read_u16_le(bytes.data() + offset);
}
[[nodiscard]] constexpr std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes,
                                                  std::size_t offset) noexcept {
  return read_u32_le(bytes.data() + offset);
}
[[nodiscard]] constexpr std::uint64_t read_u64_le(std::span<const std::uint8_t> bytes,
                                                  std::size_t offset) noexcept {
  return read_u64_le(bytes.data() + offset);
}

constexpr void write_u16_be(std::uint8_t* out, std::uint16_t value) noexcept {
  out[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  out[1] = static_cast<std::uint8_t>(value & 0xFFU);
}

constexpr void write_u32_be(std::uint8_t* out, std::uint32_t value) noexcept {
  out[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  out[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  out[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

constexpr void write_u64_be(std::uint8_t* out, std::uint64_t value) noexcept {
  write_u32_be(out, static_cast<std::uint32_t>(value >> 32U));
  write_u32_be(out + 4, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
}

constexpr void write_u16_le(std::uint8_t* out, std::uint16_t value) noexcept {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

constexpr void write_u32_le(std::uint8_t* out, std::uint32_t value) noexcept {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

constexpr void write_u64_le(std::uint8_t* out, std::uint64_t value) noexcept {
  write_u32_le(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
  write_u32_le(out + 4, static_cast<std::uint32_t>(value >> 32U));
}

inline void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

inline void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

inline void append_u64_be(std::vector<std::uint8_t>& out, std::uint64_t value) {
  append_u32_be(out, static_cast<std::uint32_t>(value >> 32U));
  append_u32_be(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
}

inline void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

inline void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

inline void append_u64_le(std::vector<std::uint8_t>& out, std::uint64_t value) {
  append_u32_le(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
  append_u32_le(out, static_cast<std::uint32_t>(value >> 32U));
}

}  // namespace patchy
