#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace patchy {

// Semantic subset of Photoshop Smart Filters understood by Patchy. Unsupported
// entries remain represented so one unknown entry can fail the whole imported
// stack closed while the PSD layer keeps using Photoshop's baked preview.
enum class SmartFilterKind {
  Unsupported,
  GaussianBlur,
  HighPass,
  Median,
  DustAndScratches,
  SurfaceBlur,
  UnsharpMask,
  MotionBlur,
  PlasticWrap,
  Mosaic,
  Emboss,
  BoxBlur,
};

struct GaussianBlurSmartFilter {
  double radius_pixels{0.0};
  bool operator==(const GaussianBlurSmartFilter&) const = default;
};

struct HighPassSmartFilter {
  double radius_pixels{0.0};
  bool operator==(const HighPassSmartFilter&) const = default;
};

struct MedianSmartFilter {
  double radius_pixels{1.0};
  bool operator==(const MedianSmartFilter&) const = default;
};

struct DustAndScratchesSmartFilter {
  std::int32_t radius_pixels{1};
  std::int32_t threshold{0};
  bool operator==(const DustAndScratchesSmartFilter&) const = default;
};

struct SurfaceBlurSmartFilter {
  double radius_pixels{5.0};
  std::int32_t threshold{15};
  bool operator==(const SurfaceBlurSmartFilter&) const = default;
};

struct UnsharpMaskSmartFilter {
  double amount_percent{150.0};
  double radius_pixels{2.0};
  std::int32_t threshold{8};
  bool operator==(const UnsharpMaskSmartFilter&) const = default;
};

struct MotionBlurSmartFilter {
  std::int32_t angle_degrees{0};
  std::int32_t distance_pixels{12};
  bool operator==(const MotionBlurSmartFilter&) const = default;
};

struct PlasticWrapSmartFilter {
  std::int32_t highlight_strength{9};
  std::int32_t detail{7};
  std::int32_t smoothness{5};
  bool operator==(const PlasticWrapSmartFilter&) const = default;
};

// Photoshop stores Cell Size as a #Pxl unit double but its dialog only
// produces whole pixels from 2 through 200 (July 2026 captures).
struct MosaicSmartFilter {
  std::int32_t cell_size_pixels{8};
  bool operator==(const MosaicSmartFilter&) const = default;
};

// Photoshop stores all three Emboss settings as plain integers ordered
// Angl, Hght, Amnt (July 2026 captures). Angle is -360..360 and Amount is
// 1..500 per Adobe's filter reference; Height is accepted through 100 px.
struct EmbossSmartFilter {
  std::int32_t angle_degrees{135};
  std::int32_t height_pixels{2};
  std::int32_t amount_percent{100};
  bool operator==(const EmbossSmartFilter&) const = default;
};

// Photoshop stores Radius as a #Pxl unit double (July 2026 captures);
// fractional values are preserved and floored for rendering like Median.
// Values through 2000 px are accepted on import.
struct BoxBlurSmartFilter {
  double radius_pixels{1.0};
  bool operator==(const BoxBlurSmartFilter&) const = default;
};

using SmartFilterParameters =
    std::variant<std::monostate, GaussianBlurSmartFilter, HighPassSmartFilter,
                 MedianSmartFilter, DustAndScratchesSmartFilter,
                 SurfaceBlurSmartFilter, UnsharpMaskSmartFilter,
                 MotionBlurSmartFilter, PlasticWrapSmartFilter,
                 MosaicSmartFilter, EmbossSmartFilter, BoxBlurSmartFilter>;

struct SmartFilterEntry {
  SmartFilterKind kind{SmartFilterKind::Unsupported};
  std::string native_name;
  std::string native_class_id;
  std::uint32_t native_filter_id{0};
  bool enabled{true};
  bool has_options{true};
  double opacity{1.0};
  BlendMode blend_mode{BlendMode::Normal};
  RgbColor foreground{};
  RgbColor background{255, 255, 255};
  SmartFilterParameters parameters;
};

// Photoshop has one shared mask for an entire Smart Filter stack. An empty
// pixel buffer means the native stack has no stored mask pixels; the descriptor
// flags still remain meaningful and are preserved here.
struct SmartFilterMask {
  Rect bounds{};
  PixelBuffer pixels{};
  std::uint8_t default_color{255};
  bool enabled{true};
  bool linked{true};
  bool extend_with_white{true};
};

enum class SmartFilterStackSupport {
  Supported,
  Unsupported,
};

struct SmartFilterStack {
  bool enabled{true};
  bool valid_at_position{true};
  // Native execution order is bottom-up. Photoshop's layer panel displays the
  // same entries in reverse, with the final/topmost effect visually first.
  std::vector<SmartFilterEntry> entries;
  SmartFilterMask mask;
  // Fail closed: import code promotes this only after every entry and the
  // shared mask have been decoded into the verified semantic subset.
  SmartFilterStackSupport support{SmartFilterStackSupport::Unsupported};
};

}  // namespace patchy
