#pragma once

// Wire protocol between Patchy and the out-of-process legacy filter host
// (patchy-8bf-host32.exe / patchy-8bf-host64.exe). Qt-free and header-only so
// both sides compile the same definitions. Every message is a little-endian
// {uint32 type, uint32 payload length, payload}. Pixel data never travels over
// the pipe: Patchy publishes it in a named file mapping described by the Run
// message. See docs/plugins.md.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace patchy::legacy_host {

inline constexpr std::uint32_t kProtocolVersion = 4;

enum MessageType : std::uint32_t {
  kMessageHello = 1,     // both directions: uint32 protocol version, uint32 pointer size in bytes
  kMessageRun = 2,       // Patchy -> host: RunRequest
  kMessageProgress = 3,  // host -> Patchy: int32 done, int32 total
  kMessageCancel = 4,    // Patchy -> host: no payload
  kMessageResult = 5,    // host -> Patchy: RunResult
};

enum RunStatus : std::int32_t {
  kRunOk = 0,
  kRunCancelled = 1,
  kRunError = 2,
};

// Flags in RunRequest::flags.
inline constexpr std::uint32_t kRunShowDialog = 1U << 0U;
// The plug-in's alpha writes are discarded (protected transparency).
inline constexpr std::uint32_t kRunProtectAlpha = 1U << 1U;

struct RunRequest {
  std::uint32_t flags{0};
  std::uint64_t parent_window{0};       // HWND that owns the plug-in's dialogs, 0 for none
  std::u16string plugin_path;           // the .8bf file
  std::string entry_point;              // exported symbol, empty for the conventional names
  std::u16string mapping_name;          // named file mapping holding the pixel regions
  std::uint64_t mapping_size{0};
  std::uint64_t input_offset{0};        // width * height * planes bytes, interleaved
  std::uint64_t output_offset{0};       // same size; Patchy pre-fills it with the input
  std::uint64_t mask_offset{0};         // width * height bytes when has_mask, unused otherwise
  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t planes{0};               // 3 (RGB) or 4 (RGB + transparency)
  std::int32_t filter_case{1};
  std::int32_t filter_top{0};           // filterRect in image (layer) coordinates
  std::int32_t filter_left{0};
  std::int32_t filter_bottom{0};
  std::int32_t filter_right{0};
  std::uint8_t has_mask{0};
  std::uint8_t case_input_handling{1};  // FilterCaseInfo of the chosen case
  std::uint8_t case_output_handling{1};
  std::uint8_t case_flags1{0};
  std::uint8_t foreground[3]{0, 0, 0};
  std::uint8_t background[3]{255, 255, 255};
  std::int32_t resolution_fixed{72 << 16};  // image resolution in dpi, 16.16 fixed
  std::string document_title;           // UTF-8
  std::vector<std::uint8_t> parameters;  // the plug-in's parameter block from a previous run
  // The largest screen the plug-in is told about (docs/plugins.md, the virtual
  // screen): the work area of parent_window's monitor, capped to this when positive.
  std::int32_t screen_max_width{0};
  std::int32_t screen_max_height{0};
  // Title of the frame window a full-screen plug-in canvas is placed in
  // ("<plug-in> via Patchy", already translated).
  std::u16string window_title;
};

struct RunResult {
  std::int32_t status{kRunOk};
  std::string message;                   // UTF-8, plug-in supplied or host generated
  std::vector<std::uint8_t> parameters;  // the plug-in's parameter block after the run
};

// --- serialization -----------------------------------------------------------

class Writer {
public:
  void u8(std::uint8_t value) { bytes_.push_back(value); }
  void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
  void u32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
  void u64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
      bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
  void bytes(const std::vector<std::uint8_t>& value) {
    u32(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }
  void utf8(const std::string& value) {
    u32(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }
  void utf16(const std::u16string& value) {
    u32(static_cast<std::uint32_t>(value.size()));
    for (const auto unit : value) {
      bytes_.push_back(static_cast<std::uint8_t>(unit & 0xFFU));
      bytes_.push_back(static_cast<std::uint8_t>(unit >> 8U));
    }
  }
  [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return bytes_; }
  [[nodiscard]] std::vector<std::uint8_t> take() noexcept { return std::move(bytes_); }

private:
  std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
  Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  [[nodiscard]] bool ok() const noexcept { return ok_; }
  std::uint8_t u8() {
    if (!need(1)) {
      return 0;
    }
    return data_[offset_++];
  }
  std::uint32_t u32() {
    if (!need(4)) {
      return 0;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(data_[offset_ + static_cast<std::size_t>(i)]) << (8 * i);
    }
    offset_ += 4;
    return value;
  }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  std::uint64_t u64() {
    if (!need(8)) {
      return 0;
    }
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
      value |= static_cast<std::uint64_t>(data_[offset_ + static_cast<std::size_t>(i)]) << (8 * i);
    }
    offset_ += 8;
    return value;
  }
  std::vector<std::uint8_t> bytes() {
    const auto count = u32();
    if (!need(count)) {
      return {};
    }
    std::vector<std::uint8_t> out(data_ + offset_, data_ + offset_ + count);
    offset_ += count;
    return out;
  }
  std::string utf8() {
    const auto count = u32();
    if (!need(count)) {
      return {};
    }
    std::string out(reinterpret_cast<const char*>(data_ + offset_), count);
    offset_ += count;
    return out;
  }
  std::u16string utf16() {
    const auto count = u32();
    if (!need(static_cast<std::size_t>(count) * 2)) {
      return {};
    }
    std::u16string out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      out.push_back(static_cast<char16_t>(data_[offset_] | (static_cast<std::uint16_t>(data_[offset_ + 1]) << 8U)));
      offset_ += 2;
    }
    return out;
  }

private:
  bool need(std::size_t count) noexcept {
    if (!ok_ || count > size_ - offset_) {
      ok_ = false;
      return false;
    }
    return true;
  }
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t offset_{0};
  bool ok_{true};
};

inline std::vector<std::uint8_t> encode_run_request(const RunRequest& request) {
  Writer w;
  w.u32(request.flags);
  w.u64(request.parent_window);
  w.utf16(request.plugin_path);
  w.utf8(request.entry_point);
  w.utf16(request.mapping_name);
  w.u64(request.mapping_size);
  w.u64(request.input_offset);
  w.u64(request.output_offset);
  w.u64(request.mask_offset);
  w.i32(request.width);
  w.i32(request.height);
  w.i32(request.planes);
  w.i32(request.filter_case);
  w.i32(request.filter_top);
  w.i32(request.filter_left);
  w.i32(request.filter_bottom);
  w.i32(request.filter_right);
  w.u8(request.has_mask);
  w.u8(request.case_input_handling);
  w.u8(request.case_output_handling);
  w.u8(request.case_flags1);
  for (const auto v : request.foreground) {
    w.u8(v);
  }
  for (const auto v : request.background) {
    w.u8(v);
  }
  w.i32(request.resolution_fixed);
  w.utf8(request.document_title);
  w.bytes(request.parameters);
  w.i32(request.screen_max_width);
  w.i32(request.screen_max_height);
  w.utf16(request.window_title);
  return w.take();
}

inline bool decode_run_request(const std::uint8_t* data, std::size_t size, RunRequest& request) {
  Reader r(data, size);
  request.flags = r.u32();
  request.parent_window = r.u64();
  request.plugin_path = r.utf16();
  request.entry_point = r.utf8();
  request.mapping_name = r.utf16();
  request.mapping_size = r.u64();
  request.input_offset = r.u64();
  request.output_offset = r.u64();
  request.mask_offset = r.u64();
  request.width = r.i32();
  request.height = r.i32();
  request.planes = r.i32();
  request.filter_case = r.i32();
  request.filter_top = r.i32();
  request.filter_left = r.i32();
  request.filter_bottom = r.i32();
  request.filter_right = r.i32();
  request.has_mask = r.u8();
  request.case_input_handling = r.u8();
  request.case_output_handling = r.u8();
  request.case_flags1 = r.u8();
  for (auto& v : request.foreground) {
    v = r.u8();
  }
  for (auto& v : request.background) {
    v = r.u8();
  }
  request.resolution_fixed = r.i32();
  request.document_title = r.utf8();
  request.parameters = r.bytes();
  request.screen_max_width = r.i32();
  request.screen_max_height = r.i32();
  request.window_title = r.utf16();
  return r.ok();
}

inline std::vector<std::uint8_t> encode_run_result(const RunResult& result) {
  Writer w;
  w.i32(result.status);
  w.utf8(result.message);
  w.bytes(result.parameters);
  return w.take();
}

inline bool decode_run_result(const std::uint8_t* data, std::size_t size, RunResult& result) {
  Reader r(data, size);
  result.status = r.i32();
  result.message = r.utf8();
  result.parameters = r.bytes();
  return r.ok();
}

// Frames a message: header (type, length) followed by the payload.
inline std::vector<std::uint8_t> frame_message(std::uint32_t type, const std::vector<std::uint8_t>& payload) {
  Writer w;
  w.u32(type);
  w.u32(static_cast<std::uint32_t>(payload.size()));
  auto out = w.take();
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

}  // namespace patchy::legacy_host
