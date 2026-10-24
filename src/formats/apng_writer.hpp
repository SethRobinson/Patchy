#pragma once

#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <memory>
#include <vector>

// Animated PNG writer (GitHub issue 366): 8-bit RGBA frames into the PNG third
// edition's acTL / fcTL / fdAT chunks. The first frame is the IDAT default image,
// so a viewer without APNG support shows frame 1. Later frames store only the
// rectangle that differs from the frame before (dispose none, blend source), and
// rows take the adaptive PNG filter with the smallest sum of absolute values.
// Qt-free and deterministic: miniz deflate at a fixed level, big-endian fields.
// The reader is src/ui/apng_reader; docs/animation.md.
namespace patchy::apng {

// Document metadata key holding the play count an animated PNG opened with, or the
// one last exported, so the next export offers it again (like webp.loop_count).
inline constexpr const char* kLoopCountMetadata = "apng.loop_count";

class AnimationEncoder {
 public:
  // `play_count` is acTL num_plays: 0 loops forever, otherwise total plays.
  // `dots_per_meter_*` > 0 writes a pHYs chunk. Throws std::runtime_error on a
  // size outside 1..2^31-1.
  AnimationEncoder(std::int32_t width, std::int32_t height, std::uint32_t play_count,
                   std::int32_t dots_per_meter_x = 0, std::int32_t dots_per_meter_y = 0);
  ~AnimationEncoder();
  AnimationEncoder(const AnimationEncoder&) = delete;
  AnimationEncoder& operator=(const AnimationEncoder&) = delete;

  // `rgba` is an 8-bit RGBA buffer of the encoder's size (straight alpha). Delays
  // above 65.535 s are stored at centisecond, then whole-second precision.
  void add(const PixelBuffer& rgba, std::uint32_t delay_ms);
  // The whole file. Throws std::runtime_error when no frame was added.
  [[nodiscard]] std::vector<std::uint8_t> finish();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace patchy::apng
