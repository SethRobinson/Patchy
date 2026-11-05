#pragma once

// Assembles the raw frames a SANE scanner backend delivers into one interleaved image
// (docs/import-scanner.md). Qt-free so the frame rules are pinned by
// core tests without a scanner or libsane.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::support {

// SANE_Frame values: one gray or interleaved RGB frame, or three single-channel passes.
enum class ScanFrameFormat : int { Gray = 0, Rgb = 1, Red = 2, Green = 3, Blue = 4 };

struct ScanFrameParameters {
  ScanFrameFormat format{ScanFrameFormat::Gray};
  bool last_frame{true};
  int bytes_per_line{0};
  int pixels_per_line{0};
  int lines{-1};  // -1: unknown until the backend reports end of frame (hand scanners)
  int depth{8};   // 1, 8 or 16 bits per sample
};

// Interleaved samples, top row first. bit_depth 8: one byte per sample. bit_depth 16: two
// bytes per sample in host byte order (SANE delivers 16-bit samples in host order).
// One-bit samples are expanded to 8 bits: in gray line art SANE's 1 is black, in color 1 is
// full intensity.
struct ScanImage {
  int width{0};
  int height{0};
  int channels{0};  // 1 (gray) or 3 (RGB)
  int bit_depth{0};
  std::vector<std::uint8_t> samples;
};

class ScanImageAssembler {
 public:
  // Throws std::invalid_argument for a depth, format or line stride SANE does not allow,
  // or for a pass that does not match the earlier passes of a three-pass scan.
  void begin_frame(const ScanFrameParameters& parameters);
  void append(std::span<const std::uint8_t> bytes);
  // Throws std::runtime_error when the frame delivered fewer lines than it announced.
  void end_frame();
  [[nodiscard]] bool complete() const noexcept { return complete_; }
  // Throws std::logic_error before the last frame ended.
  [[nodiscard]] ScanImage finish();

 private:
  void store_row(std::span<const std::uint8_t> row);

  ScanFrameParameters frame_{};
  bool in_frame_{false};
  bool complete_{false};
  bool three_pass_{false};
  int passes_seen_{0};
  int rows_in_frame_{0};
  std::vector<std::uint8_t> pending_row_;
  ScanImage image_{};
};

// SANE reports resolution as SANE_Int dots per inch or SANE_Fixed (16.16).
[[nodiscard]] double sane_resolution_to_dpi(std::int32_t word, bool is_fixed) noexcept;

// Why a scanner or camera call failed, in terms the user can act on. The dialog turns each
// cause into an explanation with a next step instead of showing only the library's text.
enum class AcquireFailureCause {
  Unknown,
  NetworkUnreachable,  // network scanner (airscan, escl, net) did not answer
  ConnectionLost,      // local device I/O error or timeout
  Busy,                // another program or job holds the device
  AccessDenied,        // device permissions
  Jammed,
  FeederEmpty,
  CoverOpen,
  UnsupportedSettings,
  OutOfMemory,
  CameraClaimed,       // the desktop or another program has the camera open
  CameraNotSupported,  // libgphoto2 has no driver for the model
  CorruptedData,
  ImageMissing,        // the file was removed from the camera after listing
};

// sane_status is a SANE_Status value; device_name is the SANE device name ("airscan:...").
[[nodiscard]] AcquireFailureCause classify_sane_failure(int sane_status, std::string_view device_name) noexcept;
// gphoto_status is a negative libgphoto2 result code.
[[nodiscard]] AcquireFailureCause classify_gphoto_failure(int gphoto_status) noexcept;
// True for SANE backends that reach the device over the network.
[[nodiscard]] bool is_network_scanner(std::string_view device_name) noexcept;
// The address from a network backend's model text ("... ip=192.168.1.20"), or empty.
[[nodiscard]] std::string network_scanner_address(std::string_view model);

}  // namespace patchy::support
