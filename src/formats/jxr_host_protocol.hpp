#pragma once

// The pipe protocol between Patchy's Linux JPEG XR backend (jxr_document_io_linux.cpp) and
// patchy-jxr-host (jxr_host/main.cpp), which runs the distribution's jxrlib in its own
// process (docs/jxr.md, Linux). Header-only, Qt-free and jxrlib-free so both sides and the
// core tests share it.
//
// One request, one reply, each a message: a little-endian u64 byte count, then the body.
//   request: u32 kMagic, u32 Operation, then
//     Probe:  nothing
//     Decode: blob(file bytes)
//     Encode: i32 width, i32 height, u8 has_alpha, f64 ppi_x, f64 ppi_y, i32 quality,
//             u8 lossless, blob(RGBA8, width * height * 4)
//   reply:   u32 kMagic, u32 Status, text(detail, English, for logs), then on Ok
//     Decode: i32 width, i32 height, f64 dpi_x, f64 dpi_y, u32 frames, u8 is_float,
//             u32 bits per channel, blob(RGBA float when is_float, else RGBA8; straight alpha)
//     Encode: blob(.jxr bytes)
// A blob or text is a u64 byte count and the bytes.

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace patchy::jxr::host {

inline constexpr std::uint32_t kMagic = 0x52584A50;  // "PJXR"
inline constexpr std::uint64_t kMaximumPixels = 268'435'456;  // 256 Mpx, the WIC reader's ceiling
// Largest message either side accepts: a 256 Mpx RGBA float frame plus headers.
inline constexpr std::uint64_t kMaximumMessageBytes = kMaximumPixels * 16U + 4096U;

enum class Operation : std::uint32_t { Probe = 1, Decode = 2, Encode = 3 };

enum class Status : std::uint32_t {
  Ok = 0,
  LibraryMissing = 1,     // libjxrglue.so.0 did not load (or lacks a function)
  DecodeFailed = 2,       // jxrlib rejected the file
  UnsupportedFormat = 3,  // CMYK, n-channel, RGBE, packed 5/10/565-bit, 1-bit
  BadDimensions = 4,      // zero or above kMaximumPixels
  EncodeFailed = 5,
  BadRequest = 6,
};

// Patchy's 1-100 quality onto jxrlib's default quantizer index. jxrlib has no single
// "quality" knob (WIC maps its ImageQuality through a private table), so this is a
// monotonic approximation: QP 1 is lossless, quality 90 lands near QP 13, 50 near 61, 1 at
// 120 (docs/jxr.md, encoder). Lossless always means QP 1.
[[nodiscard]] inline std::uint8_t quality_to_qp(int quality, bool lossless) {
  if (lossless) {
    return 1;
  }
  const int clamped = std::clamp(quality, 1, 100);
  const double qp = 1.0 + std::round(static_cast<double>(100 - clamped) * 1.2);
  return static_cast<std::uint8_t>(std::clamp(qp, 2.0, 255.0));
}

class Writer {
public:
  void u8(std::uint8_t value) { bytes_.push_back(value); }
  void u32(std::uint32_t value) { put(&value, sizeof(value)); }
  void i32(std::int32_t value) { put(&value, sizeof(value)); }
  void u64(std::uint64_t value) { put(&value, sizeof(value)); }
  void f64(double value) { put(&value, sizeof(value)); }
  void blob(const std::uint8_t* data, std::size_t size) {
    u64(size);
    put(data, size);
  }
  void text(const std::string& value) { blob(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()); }
  void append(const std::vector<std::uint8_t>& other) { bytes_.insert(bytes_.end(), other.begin(), other.end()); }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
  void put(const void* data, std::size_t size) {
    const auto* start = static_cast<const std::uint8_t*>(data);
    bytes_.insert(bytes_.end(), start, start + size);
  }
  std::vector<std::uint8_t> bytes_;
};

// Reads fields in order; any overrun turns ok() false and returns zeros from then on.
class Reader {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
  std::uint8_t u8() { return get<std::uint8_t>(); }
  std::uint32_t u32() { return get<std::uint32_t>(); }
  std::int32_t i32() { return get<std::int32_t>(); }
  std::uint64_t u64() { return get<std::uint64_t>(); }
  double f64() { return get<double>(); }
  std::vector<std::uint8_t> blob() {
    const std::uint64_t size = u64();
    if (!ok_ || size > bytes_.size() - offset_) {
      ok_ = false;
      return {};
    }
    std::vector<std::uint8_t> out(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                  bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
    offset_ += size;
    return out;
  }
  std::string text() {
    const auto raw = blob();
    return std::string(raw.begin(), raw.end());
  }
  [[nodiscard]] bool ok() const { return ok_; }

private:
  template <typename T>
  T get() {
    T value{};
    if (!ok_ || sizeof(T) > bytes_.size() - offset_) {
      ok_ = false;
      return value;
    }
    std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
    offset_ += sizeof(T);
    return value;
  }
  std::span<const std::uint8_t> bytes_;
  std::size_t offset_{0};
  bool ok_{true};
};

// Blocking whole-buffer I/O on a pipe or socket, retrying EINTR.
[[nodiscard]] inline bool write_all(int fd, const std::uint8_t* data, std::size_t size) {
  while (size > 0) {
    const ssize_t written = ::write(fd, data, size);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return false;
    }
    data += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

[[nodiscard]] inline bool read_all(int fd, std::uint8_t* data, std::size_t size) {
  while (size > 0) {
    const ssize_t got = ::read(fd, data, size);
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got <= 0) {
      return false;
    }
    data += got;
    size -= static_cast<std::size_t>(got);
  }
  return true;
}

[[nodiscard]] inline bool write_message(int fd, const std::vector<std::uint8_t>& body) {
  const std::uint64_t size = body.size();
  return write_all(fd, reinterpret_cast<const std::uint8_t*>(&size), sizeof(size)) &&
         write_all(fd, body.data(), body.size());
}

[[nodiscard]] inline bool read_message(int fd, std::vector<std::uint8_t>& body) {
  std::uint64_t size = 0;
  if (!read_all(fd, reinterpret_cast<std::uint8_t*>(&size), sizeof(size)) || size > kMaximumMessageBytes) {
    return false;
  }
  body.resize(static_cast<std::size_t>(size));
  return read_all(fd, body.data(), body.size());
}

}  // namespace patchy::jxr::host
