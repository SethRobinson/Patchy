#pragma once

#include "formats/format_registry.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::raw {

// Camera raw import (Canon CR2/CR3, Nikon NEF, Sony ARW, Fuji RAF, DNG, ...), backed by the
// vendored LibRaw (src/formats/libraw). Raw files are read-only sources: the develop step
// converts 12-16 bit sensor data to an 8-bit sRGB document, so every raw-precision decision
// (white balance, exposure, highlight recovery, demosaic, denoise) has to be made here.
// This header stays LibRaw-free like the rest of the formats API.

enum class WhiteBalanceMode {
  AsShot,  // the multipliers the camera recorded (default)
  Auto,    // gray-world estimate over the sensor data
  Custom   // explicit temperature/tint
};

enum class HighlightMode {
  Clip,    // clip channels to solid white (default)
  Unclip,  // leave unclipped: blown areas keep whatever hue survives
  Blend,   // blend clipped/unclipped for soft highlight rolloff
  Rebuild  // reconstruct blown channels from the surviving ones (best for skies)
};

// Values map onto LibRaw's user_qual list; only algorithms built into the stock tarball.
enum class DemosaicAlgorithm {
  Linear,      // bilinear: fastest, softest
  Vng,         // Variable Number of Gradients
  Ppg,         // Patterned Pixel Grouping
  Ahd,         // Adaptive Homogeneity-Directed (default)
  Dcb,         // DCB
  Dht,         // DHT: often best on high-ISO/noisy files
  ModifiedAhd  // AAHD (modified AHD)
};

enum class FbddNoiseReduction {
  Off,
  Light,
  Full
};

enum class NoiseReductionMode { Auto, Manual, Off };
enum class DevelopQuality { Final, Draft };
inline constexpr int kProcessingVersion = 3;

struct DevelopOptions {
  DevelopQuality quality{DevelopQuality::Final};
  // Called on the worker at decoder checkpoints. Must not throw.
  std::function<bool()> cancelled;
  // Monotone completed-stage estimate, 0..100, delivered on the worker. Must not throw.
  std::function<void(int)> progress{};
};

class DevelopCancelled final : public std::exception {
public:
  const char* what() const noexcept override { return "RAW development cancelled"; }
};

struct DevelopParams {
  // Version 1 preserves the original neutral rendering and Auto noise policy.
  int processing_version{kProcessingVersion};
  RenderingProfile profile{RenderingProfile::Natural};
  WhiteBalanceMode white_balance{WhiteBalanceMode::AsShot};
  // Used when white_balance == Custom.
  WhiteBalance custom_white_balance{};
  // Linear exposure shift in EV; LibRaw supports -2 (darken) .. +3 (brighten).
  double exposure_ev{0.0};
  // Reconstruction of CLIPPED sensor data (distinct from the tonal `highlights` slider).
  HighlightMode highlight_recovery{HighlightMode::Clip};
  // Histogram-based auto brightening (dcraw behavior). Off by default: predictable
  // rendering beats a surprise histogram stretch; exposure/brightness are explicit.
  bool auto_brighten{false};
  // Manual brightness multiplier applied with (or instead of) auto brightening.
  double brightness{1.0};
  // Tone/color adjustments (-100..100, 0 = neutral), applied by Patchy to LibRaw's
  // 16-bit output before the 8-bit bake (see formats/raw_tone.hpp).
  double contrast{0.0};
  double highlights{0.0};
  double shadows{0.0};
  double saturation{0.0};
  double vibrance{0.0};
  DemosaicAlgorithm demosaic{DemosaicAlgorithm::Ahd};
  NoiseReductionMode noise_reduction{NoiseReductionMode::Auto};
  // Wavelet denoise threshold, 0 (off) .. 1000; 100-350 is a typical high-ISO range.
  int wavelet_denoise_threshold{0};
  FbddNoiseReduction fbdd{FbddNoiseReduction::Off};
  // Manual color-difference median passes (0..4); Auto resolves from ISO.
  int color_denoise_passes{0};
  // Reduce finished output by 2 in each dimension. Draft quality is independent.
  bool half_size{false};
  friend bool operator==(const DevelopParams&, const DevelopParams&) = default;
};

[[nodiscard]] DevelopParams normalize_develop_params(DevelopParams params);

struct EffectiveNoiseReduction {
  int wavelet_threshold{0};
  FbddNoiseReduction fbdd{FbddNoiseReduction::Off};
  bool auto_available{false};
  int color_passes{0};
};

struct RawFileInfo {
  std::string camera_make;
  std::string camera_model;
  std::string lens;
  double iso{0.0};
  double shutter_seconds{0.0};
  double aperture_f_number{0.0};
  double focal_length_mm{0.0};
  // Unix time of capture; 0 = unknown.
  long long timestamp{0};
  // Full-size develop output dimensions with the camera orientation applied.
  std::int32_t output_width{0};
  std::int32_t output_height{0};
  // dcraw flip code the develop applies: 0 none, 3 = 180, 5 = 90 CCW, 6 = 90 CW. Embedded
  // thumbnails are usually stored unrotated, so previewing one needs this rotation.
  int orientation_flip{0};
  bool is_xtrans{false};
  bool is_foveon{false};
  bool is_three_color_bayer{false};
  // As-shot white balance expressed as temperature/tint through the camera matrix;
  // nullopt when the file carries no usable as-shot multipliers or camera matrix.
  std::optional<WhiteBalance> as_shot_white_balance;
  // Largest embedded preview, verbatim (usually JPEG bytes; empty when absent). Decoding
  // is the caller's job: the formats library stays image-codec-free.
  std::vector<std::uint8_t> thumbnail;
};

[[nodiscard]] EffectiveNoiseReduction effective_noise_reduction(
    const DevelopParams& params, const RawFileInfo& info);

// Lowercase extensions (no dot) routed to the raw reader. Deliberately excludes the
// ambiguous ".raw". TIFF-based raws that some cameras write as .tif stay with Qt's TIFF.
[[nodiscard]] const std::vector<std::string>& camera_raw_extensions();
[[nodiscard]] bool is_camera_raw_extension(std::string_view extension);

// Holds the opened file and unpacked sensor data so the develop stage can re-run with new
// parameters without re-reading or re-unpacking (the preview's slider loop). Instances are
// not internally synchronized: use from one thread at a time.
class DevelopSession {
public:
  // Takes ownership of the file bytes (LibRaw reads from the buffer lazily). Throws
  // std::runtime_error with a user-facing message on unsupported/corrupt files.
  explicit DevelopSession(std::vector<std::uint8_t> file_bytes);
  ~DevelopSession();
  DevelopSession(const DevelopSession&) = delete;
  DevelopSession& operator=(const DevelopSession&) = delete;

  [[nodiscard]] const RawFileInfo& info() const noexcept;

  struct DevelopedImage {
    std::int32_t width{0};
    std::int32_t height{0};
    // Intended final-output dimensions, even when rgb contains a smaller draft.
    std::int32_t output_width{0};
    std::int32_t output_height{0};
    DevelopQuality quality{DevelopQuality::Final};
    int processing_version{kProcessingVersion};
    RenderingProfile profile{RenderingProfile::Natural};
    bool fast_half_size{false};
    bool reused_draft_decode{false};
    // Bayer demosaic actually used; the fast draft path bypasses demosaicing.
    std::optional<DemosaicAlgorithm> demosaic;
    EffectiveNoiseReduction noise;
    std::optional<std::array<double, 4>> white_balance_multipliers;
    std::optional<WhiteBalance> effective_white_balance;
    // Tightly packed interleaved RGB, 3 bytes per pixel, orientation applied.
    std::vector<std::uint8_t> rgb;
  };

  // Runs the raw develop pipeline with `params`. Callable repeatedly.
  [[nodiscard]] DevelopedImage develop(const DevelopParams& params, const DevelopOptions& options = {});

  // develop() wrapped into a single-"Background"-pixel-layer sRGB document (the flat-reader
  // convention shared with BMP/TGA/PCX).
  [[nodiscard]] FormatReadResult develop_document(const DevelopParams& params, const DevelopOptions& options = {});

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] FormatReadResult document_from_developed(const DevelopSession::DevelopedImage& image);

// One-shot convenience for the headless paths (format registry, tests): open + develop +
// wrap in a document.
[[nodiscard]] FormatReadResult read_camera_raw(std::span<const std::uint8_t> bytes, const DevelopParams& params);

}  // namespace patchy::raw
