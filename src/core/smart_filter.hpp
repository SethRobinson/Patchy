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
};

struct GaussianBlurSmartFilter {
  double radius_pixels{0.0};
};

struct HighPassSmartFilter {
  double radius_pixels{0.0};
};

struct MedianSmartFilter {
  double radius_pixels{1.0};
};

struct DustAndScratchesSmartFilter {
  std::int32_t radius_pixels{1};
  std::int32_t threshold{0};
};

struct SurfaceBlurSmartFilter {
  double radius_pixels{5.0};
  std::int32_t threshold{15};
};

using SmartFilterParameters =
    std::variant<std::monostate, GaussianBlurSmartFilter,
                 HighPassSmartFilter, MedianSmartFilter,
                 DustAndScratchesSmartFilter, SurfaceBlurSmartFilter>;

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
