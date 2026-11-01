#pragma once

// Encoding and decoding of GIMP 3 wire messages (docs/gimp-plugins.md, "Protocol").
// Every number is big-endian; a string is a u32 byte count including its NUL (0 for
// NULL) and the bytes; arrays are a u32 count and their elements. Messages carry no
// length, so a decoder reads from a stream exactly as far as the grammar says. The
// grammar is the one observed in GIMP 3.2.6 traffic; scripts/dev/gimp/wire_dump.py
// mirrors it.

#include "plugins/gimp/gimp_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace patchy::gimp {

class WireError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// A blocking byte source: read_exact fills the whole span or throws WireError.
class ByteSource {
public:
  virtual ~ByteSource() = default;
  virtual void read_exact(std::span<std::uint8_t> out) = 0;
};

class SpanSource final : public ByteSource {
public:
  explicit SpanSource(std::span<const std::uint8_t> data) : data_(data) {}
  void read_exact(std::span<std::uint8_t> out) override;
  [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - at_; }

private:
  std::span<const std::uint8_t> data_;
  std::size_t at_{0};
};

// Wraps a read function that returns the number of bytes read, 0 at end of stream
// and a negative value on error.
class FunctionSource final : public ByteSource {
public:
  explicit FunctionSource(std::function<std::ptrdiff_t(std::uint8_t*, std::size_t)> read) : read_(std::move(read)) {}
  void read_exact(std::span<std::uint8_t> out) override;

private:
  std::function<std::ptrdiff_t(std::uint8_t*, std::size_t)> read_;
};

// Upper bounds a decoder accepts before allocating; anything larger is a broken stream.
inline constexpr std::uint32_t kMaxWireString = 64u << 20;
inline constexpr std::uint32_t kMaxWireCount = 1u << 20;
inline constexpr std::uint32_t kMaxTileBytes = 4096u * 4096u * 16u;

[[nodiscard]] Message read_message(ByteSource& source);
void append_message(std::vector<std::uint8_t>& out, const Message& message);
[[nodiscard]] std::vector<std::uint8_t> encode_message(const Message& message);

// Parameter helpers for the PDB table.
[[nodiscard]] std::string describe_param(const Param& param);
[[nodiscard]] std::string describe_message(const Message& message);

}  // namespace patchy::gimp
