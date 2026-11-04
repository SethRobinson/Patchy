#pragma once

#include "formats/format_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::jxl {

// JPEG XL (ISO/IEC 18181), read and write on every platform through the vendored libjxl
// (src/formats/libjxl, built by cmake/patchy_jxl.cmake). Design, color rules and the
// encoder constraints are in docs/jxl.md; the binding legal conditions are in
// docs/legal-licensing.md. The one that shapes this API: the encoder always runs with
// patches and dots off, so no option here can turn them on.

inline constexpr const char* kLoopCountMetadata = "jxl.loop_count";

// Lowercase extensions (no dot); the single source of truth for the registry, the dialog
// filters and the writer branch.
[[nodiscard]] const std::vector<std::string>& jxl_extensions();
[[nodiscard]] bool is_jxl_extension(std::string_view extension);

// A bare codestream (FF 0A) or the ISOBMFF container (the 12-byte "JXL " signature box).
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes);

struct ReadOptions {
  // Names an animation frame layer; the UI passes translated "Frame N" text. Unset, frames
  // are named "Frame N <delay>s" in English.
  std::function<std::string(int index, std::uint32_t delay_ms)> frame_name;
};

struct ReadResult {
  FormatReadResult read;
  // True when the pixels are in the file's own color space, so its profile still describes
  // them (8 and 16-bit RGB). 32-bit opens and gray files are converted (to linear sRGB and
  // to sRGB respectively) and leave it false.
  bool pixels_in_source_space{false};
  // libjxl applies the codestream orientation while decoding.
  bool orientation_applied{false};
  std::uint32_t frame_count{1};
};

// Decodes at the file's depth while deep editing is on (8, 16 or 32-bit float; float and
// HDR files land as linear 32-bit), and at 8 bits otherwise. A still image is one
// "Background" layer; an animation becomes one full-canvas layer per frame, frame 1 on top,
// with the loop count in kLoopCountMetadata. Throws std::runtime_error with a user-facing
// message for damaged, unsupported or oversized files.
[[nodiscard]] ReadResult read_jxl_detailed(std::span<const std::uint8_t> bytes, const ReadOptions& options = {});
// The registry's FormatReadFn.
[[nodiscard]] FormatReadResult read_jxl(std::span<const std::uint8_t> bytes);
[[nodiscard]] FormatReadResult read_jxl_file(const std::filesystem::path& path);

// Metadata blocks of a file for File Info (docs/file-info.md), without decoding pixels.
struct FileMetadata {
  std::vector<std::uint8_t> exif;  // TIFF stream (the Exif box minus its offset field)
  std::vector<std::uint8_t> xmp;
  // The original color profile, synthesized by libjxl from an encoded color space when the
  // file carries no ICC; empty when that space is plain sRGB.
  std::vector<std::uint8_t> icc;
  struct Box {
    std::string type;
    std::uint64_t offset{0};
    std::uint64_t size{0};
  };
  std::vector<Box> boxes;  // container files only, in file order
  bool container{false};
};
// Never throws; damaged input returns whatever was read.
[[nodiscard]] FileMetadata read_metadata(std::span<const std::uint8_t> bytes);

struct WriteOptions {
  // 1-100 on libjxl's quality scale (JxlEncoderDistanceFromQuality); ignored when lossless.
  int quality{90};
  bool lossless{false};
  // 1-9, libjxl's encoder effort.
  int effort{7};
  // Written as Exif and xml boxes when non-empty. `exif` is a TIFF stream.
  std::vector<std::uint8_t> exif;
  std::vector<std::uint8_t> xmp;
};

// Encodes the flattened document at its depth: 8-bit as 8-bit samples with the document's
// embedded RGB profile (sRGB without one), 16-bit as 16-bit samples, 32-bit as linear-sRGB
// float. Alpha is written only when some pixel is not opaque.
[[nodiscard]] std::vector<std::uint8_t> write_jxl(const Document& document, const WriteOptions& options);
// The registry's FormatWriteFn: default options.
[[nodiscard]] std::vector<std::uint8_t> write_jxl(const Document& document);
void write_jxl_file(const Document& document, const std::filesystem::path& path, const WriteOptions& options = {});

// Animated JPEG XL from full-canvas RGBA frames, all at one depth (RGBA8, RGBA16 or RGBA
// float, the PixelBuffer formats of an 8, 16 or 32-bit document). `loops` 0 plays forever.
// An ICC profile applies to 8 and 16-bit frames only.
class AnimationEncoder {
public:
  AnimationEncoder(int width, int height, BitDepth depth, std::uint16_t loops, const WriteOptions& options,
                   std::vector<std::uint8_t> icc_profile = {});
  ~AnimationEncoder();
  AnimationEncoder(const AnimationEncoder&) = delete;
  AnimationEncoder& operator=(const AnimationEncoder&) = delete;
  void add(const PixelBuffer& rgba, std::uint32_t duration_ms);
  [[nodiscard]] std::vector<std::uint8_t> finish();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// The frame settings every encode applies, exposed so a test proves the legal rule holds:
// patches and dots are always 0 (docs/legal-licensing.md, JPEG XL condition 3).
struct EncoderToolSettings {
  std::int64_t patches{-1};
  std::int64_t dots{-1};
};
[[nodiscard]] EncoderToolSettings last_encoder_tool_settings() noexcept;

}  // namespace patchy::jxl
