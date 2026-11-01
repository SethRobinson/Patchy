#pragma once

// The link between Patchy and its GIMP plug-in helper, patchy-gimp-host
// (docs/gimp-plugins.md, "Parts"). Patchy's own protocol, unrelated to GIMP's: the
// helper's stdin carries Patchy's requests and its stdout the helper's reports, as
// frames {u32 type, u32 length, payload}, little-endian. Pixels travel through a shared
// file both sides map (layout in RunRequest). Shared verbatim by both sides; Qt-free.

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::gimp::link {

inline constexpr std::uint32_t kLinkVersion = 1;

enum class Frame : std::uint32_t {
  // Patchy -> helper
  QueryRequest = 1,
  RunRequest = 2,
  Cancel = 3,
  // helper -> Patchy
  Hello = 100,
  Log = 101,
  Progress = 102,
  Message = 103,
  Update = 104,
  PluginWindow = 105,
  Unsupported = 106,
  QueryResult = 107,
  RunResult = 108,
};

class LinkError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class Writer {
public:
  void u8(std::uint8_t v) { out_.push_back(v); }
  void u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void u64(std::uint64_t v) {
    u32(static_cast<std::uint32_t>(v));
    u32(static_cast<std::uint32_t>(v >> 32));
  }
  void f64(double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    u64(bits);
  }
  void str(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    out_.insert(out_.end(), s.begin(), s.end());
  }
  void bytes(std::span<const std::uint8_t> b) {
    u32(static_cast<std::uint32_t>(b.size()));
    out_.insert(out_.end(), b.begin(), b.end());
  }
  void strings(const std::vector<std::string>& list) {
    u32(static_cast<std::uint32_t>(list.size()));
    for (const auto& s : list) str(s);
  }
  [[nodiscard]] std::vector<std::uint8_t> frame(Frame type) const {
    std::vector<std::uint8_t> out;
    out.reserve(out_.size() + 8);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(type) >> (8 * i)));
    const auto n = static_cast<std::uint32_t>(out_.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
    out.insert(out.end(), out_.begin(), out_.end());
    return out;
  }

private:
  std::vector<std::uint8_t> out_;
};

class Reader {
public:
  explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}
  std::uint8_t u8() {
    need(1);
    return data_[at_++];
  }
  std::uint32_t u32() {
    need(4);
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t{data_[at_ + i]} << (8 * i);
    at_ += 4;
    return v;
  }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  std::uint64_t u64() {
    const std::uint64_t lo = u32();
    return lo | (std::uint64_t{u32()} << 32);
  }
  double f64() {
    const auto bits = u64();
    double v = 0;
    std::memcpy(&v, &bits, sizeof v);
    return v;
  }
  std::string str() {
    const auto n = u32();
    need(n);
    std::string s(reinterpret_cast<const char*>(data_.data() + at_), n);
    at_ += n;
    return s;
  }
  std::vector<std::uint8_t> bytes() {
    const auto n = u32();
    need(n);
    std::vector<std::uint8_t> b(data_.begin() + static_cast<std::ptrdiff_t>(at_),
                                data_.begin() + static_cast<std::ptrdiff_t>(at_ + n));
    at_ += n;
    return b;
  }
  std::vector<std::string> strings() {
    const auto n = u32();
    if (n > 1u << 20) throw LinkError("link list too long");
    std::vector<std::string> list;
    for (std::uint32_t i = 0; i < n; ++i) list.push_back(str());
    return list;
  }

private:
  void need(std::size_t n) const {
    if (n > data_.size() - at_) throw LinkError("link frame ends early");
  }
  std::span<const std::uint8_t> data_;
  std::size_t at_{0};
};

// How to start one plug-in file (Patchy resolves interpreters and the environment).
struct LaunchSpec {
  std::string program;                    // UTF-8 path
  std::vector<std::string> leading_args;  // the script for interpreted plug-ins
  std::string working_dir;
  std::vector<std::string> environment;   // NAME=value, replacing the variables named
  std::vector<std::string> unset;         // variables to drop
  std::string data_dir;                   // GIMP's shared data (share/gimp/3.0), for gimprc paths
  std::string profile_dir;                // Patchy's GIMP settings folder (the plug-in's GIMP3_DIRECTORY)
};

inline void write_launch(Writer& w, const LaunchSpec& l) {
  w.str(l.program);
  w.strings(l.leading_args);
  w.str(l.working_dir);
  w.strings(l.environment);
  w.strings(l.unset);
  w.str(l.data_dir);
  w.str(l.profile_dir);
}

inline LaunchSpec read_launch(Reader& r) {
  LaunchSpec l;
  l.program = r.str();
  l.leading_args = r.strings();
  l.working_dir = r.str();
  l.environment = r.strings();
  l.unset = r.strings();
  l.data_dir = r.str();
  l.profile_dir = r.str();
  return l;
}

struct QueryRequest {
  std::vector<LaunchSpec> plugins;
  std::uint32_t timeout_ms{30000};
  bool trace{false};
};

inline std::vector<std::uint8_t> encode(const QueryRequest& q) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(q.plugins.size()));
  for (const auto& p : q.plugins) write_launch(w, p);
  w.u32(q.timeout_ms);
  w.u8(q.trace ? 1 : 0);
  return w.frame(Frame::QueryRequest);
}

inline QueryRequest decode_query_request(std::span<const std::uint8_t> payload) {
  Reader r(payload);
  QueryRequest q;
  const auto n = r.u32();
  for (std::uint32_t i = 0; i < n; ++i) q.plugins.push_back(read_launch(r));
  q.timeout_ms = r.u32();
  q.trace = r.u8() != 0;
  return q;
}

// Shared pixel file layout: [input][output][mask], each tightly packed rows of the layer.
struct RunRequest {
  LaunchSpec launch;
  std::string procedure;
  std::vector<std::uint8_t> install;  // the procedure's PROC_INSTALL message (GIMP wire bytes)
  std::int32_t run_mode{0};           // GIMP RunMode
  std::vector<std::uint8_t> arguments;  // optional: GIMP wire params after the image arguments
  std::uint32_t argument_count{0};
  // Image and layer.
  std::int32_t image_width{0};
  std::int32_t image_height{0};
  std::int32_t layer_x{0};
  std::int32_t layer_y{0};
  std::int32_t layer_width{0};
  std::int32_t layer_height{0};
  std::uint32_t bit_depth{8};  // 8, 16 (integer, perceptual) or 32 (linear float)
  std::uint32_t channels{4};   // 3 or 4
  bool protect_alpha{false};
  std::string layer_name;
  std::string image_name;
  double resolution_x{72};
  double resolution_y{72};
  // Selection, in layer pixels (empty bounds: no selection).
  bool has_mask{false};
  std::int32_t mask_x{0};
  std::int32_t mask_y{0};
  std::int32_t mask_width{0};
  std::int32_t mask_height{0};
  // The shared pixel file.
  std::string pixel_file;
  std::uint64_t pixel_file_size{0};
  std::uint64_t input_offset{0};
  std::uint64_t output_offset{0};
  std::uint64_t mask_offset{0};
  // Context.
  double foreground[4]{0, 0, 0, 1};  // non-linear sRGB, 0..1
  double background[4]{1, 1, 1, 1};
  std::uint64_t parent_window{0};    // HWND on Windows, 0 for none
  std::string language;              // "de", for the plug-in's own strings
  std::string icon_theme_dir;
  std::uint32_t timeout_ms{0};       // 0: no limit (interactive runs)
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> stored_data;  // gimp-pdb-set-data
  bool trace{false};
  std::string wire_log;  // optional path: every byte both ways, for scripts/dev/gimp/wire_dump.py
  // The ICC profile GIMP colors carry (sRGB). Plug-ins crash on a color without one.
  std::vector<std::uint8_t> color_profile;
};

inline std::vector<std::uint8_t> encode(const RunRequest& q) {
  Writer w;
  write_launch(w, q.launch);
  w.str(q.procedure);
  w.bytes(q.install);
  w.i32(q.run_mode);
  w.bytes(q.arguments);
  w.u32(q.argument_count);
  w.i32(q.image_width);
  w.i32(q.image_height);
  w.i32(q.layer_x);
  w.i32(q.layer_y);
  w.i32(q.layer_width);
  w.i32(q.layer_height);
  w.u32(q.bit_depth);
  w.u32(q.channels);
  w.u8(q.protect_alpha ? 1 : 0);
  w.str(q.layer_name);
  w.str(q.image_name);
  w.f64(q.resolution_x);
  w.f64(q.resolution_y);
  w.u8(q.has_mask ? 1 : 0);
  w.i32(q.mask_x);
  w.i32(q.mask_y);
  w.i32(q.mask_width);
  w.i32(q.mask_height);
  w.str(q.pixel_file);
  w.u64(q.pixel_file_size);
  w.u64(q.input_offset);
  w.u64(q.output_offset);
  w.u64(q.mask_offset);
  for (double v : q.foreground) w.f64(v);
  for (double v : q.background) w.f64(v);
  w.u64(q.parent_window);
  w.str(q.language);
  w.str(q.icon_theme_dir);
  w.u32(q.timeout_ms);
  w.u32(static_cast<std::uint32_t>(q.stored_data.size()));
  for (const auto& [name, data] : q.stored_data) {
    w.str(name);
    w.bytes(data);
  }
  w.u8(q.trace ? 1 : 0);
  w.str(q.wire_log);
  w.bytes(q.color_profile);
  return w.frame(Frame::RunRequest);
}

inline RunRequest decode_run_request(std::span<const std::uint8_t> payload) {
  Reader r(payload);
  RunRequest q;
  q.launch = read_launch(r);
  q.procedure = r.str();
  q.install = r.bytes();
  q.run_mode = r.i32();
  q.arguments = r.bytes();
  q.argument_count = r.u32();
  q.image_width = r.i32();
  q.image_height = r.i32();
  q.layer_x = r.i32();
  q.layer_y = r.i32();
  q.layer_width = r.i32();
  q.layer_height = r.i32();
  q.bit_depth = r.u32();
  q.channels = r.u32();
  q.protect_alpha = r.u8() != 0;
  q.layer_name = r.str();
  q.image_name = r.str();
  q.resolution_x = r.f64();
  q.resolution_y = r.f64();
  q.has_mask = r.u8() != 0;
  q.mask_x = r.i32();
  q.mask_y = r.i32();
  q.mask_width = r.i32();
  q.mask_height = r.i32();
  q.pixel_file = r.str();
  q.pixel_file_size = r.u64();
  q.input_offset = r.u64();
  q.output_offset = r.u64();
  q.mask_offset = r.u64();
  for (double& v : q.foreground) v = r.f64();
  for (double& v : q.background) v = r.f64();
  q.parent_window = r.u64();
  q.language = r.str();
  q.icon_theme_dir = r.str();
  q.timeout_ms = r.u32();
  const auto n = r.u32();
  for (std::uint32_t i = 0; i < n; ++i) {
    auto name = r.str();
    auto data = r.bytes();
    q.stored_data.emplace_back(std::move(name), std::move(data));
  }
  q.trace = r.u8() != 0;
  q.wire_log = r.str();
  q.color_profile = r.bytes();
  return q;
}

enum class RunStatus : std::uint32_t {
  Ok = 0,         // the procedure returned success
  Cancelled = 1,  // the user cancelled (in the plug-in or in Patchy)
  Error = 2,      // the plug-in failed, crashed, called something Patchy lacks, or timed out
};

struct RunResult {
  RunStatus status{RunStatus::Error};
  std::string message;                 // English, for logs; the code decides the user text
  std::string code;                    // "unsupported-procedure", "crashed", "timeout", ...
  std::string detail;                  // the procedure name for unsupported-procedure
  bool changed{false};
  std::int32_t changed_x{0};
  std::int32_t changed_y{0};
  std::int32_t changed_width{0};
  std::int32_t changed_height{0};
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> stored_data;
  std::vector<std::string> unsupported_calls;
  // Colors the plug-in set as GIMP's foreground or background (non-linear sRGB, 0..1);
  // Patchy takes them over, as GIMP does (Border Average sets the foreground).
  bool foreground_changed{false};
  double foreground[4]{0, 0, 0, 1};
  bool background_changed{false};
  double background[4]{1, 1, 1, 1};
};

inline std::vector<std::uint8_t> encode(const RunResult& r) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(r.status));
  w.str(r.message);
  w.str(r.code);
  w.str(r.detail);
  w.u8(r.changed ? 1 : 0);
  w.i32(r.changed_x);
  w.i32(r.changed_y);
  w.i32(r.changed_width);
  w.i32(r.changed_height);
  w.u32(static_cast<std::uint32_t>(r.stored_data.size()));
  for (const auto& [name, data] : r.stored_data) {
    w.str(name);
    w.bytes(data);
  }
  w.strings(r.unsupported_calls);
  w.u8(r.foreground_changed ? 1 : 0);
  for (double v : r.foreground) w.f64(v);
  w.u8(r.background_changed ? 1 : 0);
  for (double v : r.background) w.f64(v);
  return w.frame(Frame::RunResult);
}

inline RunResult decode_run_result(std::span<const std::uint8_t> payload) {
  Reader rd(payload);
  RunResult r;
  r.status = static_cast<RunStatus>(rd.u32());
  r.message = rd.str();
  r.code = rd.str();
  r.detail = rd.str();
  r.changed = rd.u8() != 0;
  r.changed_x = rd.i32();
  r.changed_y = rd.i32();
  r.changed_width = rd.i32();
  r.changed_height = rd.i32();
  const auto n = rd.u32();
  for (std::uint32_t i = 0; i < n; ++i) {
    auto name = rd.str();
    auto data = rd.bytes();
    r.stored_data.emplace_back(std::move(name), std::move(data));
  }
  r.unsupported_calls = rd.strings();
  r.foreground_changed = rd.u8() != 0;
  for (double& v : r.foreground) v = rd.f64();
  r.background_changed = rd.u8() != 0;
  for (double& v : r.background) v = rd.f64();
  return r;
}

// Progress: fraction < 0 means "busy, no percentage".
struct Progress {
  double fraction{-1};
  std::string text;
};

inline std::vector<std::uint8_t> encode(const Progress& p) {
  Writer w;
  w.f64(p.fraction);
  w.str(p.text);
  return w.frame(Frame::Progress);
}

inline Progress decode_progress(std::span<const std::uint8_t> payload) {
  Reader r(payload);
  Progress p;
  p.fraction = r.f64();
  p.text = r.str();
  return p;
}

struct Rect {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

inline std::vector<std::uint8_t> encode_rect(Frame type, const Rect& rect, std::uint32_t flags = 0) {
  Writer w;
  w.i32(rect.x);
  w.i32(rect.y);
  w.i32(rect.width);
  w.i32(rect.height);
  w.u32(flags);
  return w.frame(type);
}

inline std::pair<Rect, std::uint32_t> decode_rect(std::span<const std::uint8_t> payload) {
  Reader r(payload);
  Rect rect;
  rect.x = r.i32();
  rect.y = r.i32();
  rect.width = r.i32();
  rect.height = r.i32();
  const auto flags = r.u32();
  return {rect, flags};
}

inline std::vector<std::uint8_t> encode_text(Frame type, const std::string& text) {
  Writer w;
  w.str(text);
  return w.frame(type);
}

inline std::string decode_text(std::span<const std::uint8_t> payload) {
  Reader r(payload);
  return r.str();
}

inline std::vector<std::uint8_t> encode_hello() {
  Writer w;
  w.u32(kLinkVersion);
  return w.frame(Frame::Hello);
}

// Incremental frame splitter for a byte stream.
class FrameBuffer {
public:
  void append(const std::uint8_t* data, std::size_t n) { buffer_.insert(buffer_.end(), data, data + n); }
  // The next whole frame, if one has arrived.
  std::optional<std::pair<Frame, std::vector<std::uint8_t>>> next() {
    if (buffer_.size() - at_ < 8) return std::nullopt;
    std::uint32_t type = 0;
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i) type |= std::uint32_t{buffer_[at_ + i]} << (8 * i);
    for (int i = 0; i < 4; ++i) n |= std::uint32_t{buffer_[at_ + 4 + i]} << (8 * i);
    if (n > (256u << 20)) throw LinkError("link frame too large");
    if (buffer_.size() - at_ - 8 < n) return std::nullopt;
    std::vector<std::uint8_t> payload(buffer_.begin() + static_cast<std::ptrdiff_t>(at_ + 8),
                                      buffer_.begin() + static_cast<std::ptrdiff_t>(at_ + 8 + n));
    at_ += 8 + n;
    if (at_ > (1u << 20) && at_ * 2 > buffer_.size()) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(at_));
      at_ = 0;
    }
    return std::make_pair(static_cast<Frame>(type), std::move(payload));
  }

private:
  std::vector<std::uint8_t> buffer_;
  std::size_t at_{0};
};

}  // namespace patchy::gimp::link
