#pragma once

// Assembles the raw frames a SANE scanner backend delivers into one interleaved image
// (docs/import.md, Linux scanner acquisition). Qt-free so the frame rules are pinned by
// core tests without a scanner or libsane.

#include <cstddef>
#include <cstdint>
#include <span>
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
// One-bit line art is expanded to 8-bit gray (SANE's 1 is black).
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

}  // namespace patchy::support
