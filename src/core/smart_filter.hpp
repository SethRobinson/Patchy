#pragma once

#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/smart_filter_table.hpp"

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
  RadialBlur,
  AddNoise,
  // Table-described filters (core/smart_filter_schema.hpp); append only.
  Solarize,
  FindEdges,
  ZigZag,
  Wave,
  // Table-described filters (core/smart_filter_table.hpp); their settings
  // are a TableSmartFilter.
  Offset,
  Maximum,
  Minimum,
  Custom,
  Fragment,
  Crystallize,
  Pointillize,
  Mezzotint,
  Fibers,
  // Image > Adjustments applied to a Smart Object (Levels, Curves, ...); the
  // settings are an AdjustmentSmartFilter (docs/smart-filters-adjustments.md).
  Adjustment,
  Ripple,
  Shear,
  Twirl,
  Pinch,
  Spherize,
  PolarCoordinates,
  TraceContour,
  Diffuse,
  // Photoshop's Filter Gallery entry (GEfc): one or more gallery effects in
  // order; the settings are a FilterGallerySmartFilter.
  FilterGallery,
  // Photoshop's parameterless classics (see parameterless_smart_filter_native).
  Blur,
  BlurMore,
  Sharpen,
  SharpenMore,
  Invert,
  Average,
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

// Photoshop stores Amnt as a plain integer 1..100 plus BlrM (Spn/Zm) and
// BlrQ (Drft/Gd/Bst) enums; the blur CENTER is not stored in the descriptor
// (July 2026 captures). Spin replicates the destructive sweep; Zoom is
// Patchy's own ray average calibrated to Photoshop's extent (October 2026,
// docs/smart-filters-native.md).
enum class RadialBlurQuality {
  Draft,
  Good,
  Best,
};

struct RadialBlurSmartFilter {
  std::int32_t amount{10};
  RadialBlurQuality quality{RadialBlurQuality::Good};
  bool zoom{false};
  bool operator==(const RadialBlurSmartFilter&) const = default;
};

// Photoshop stores Dstr (Unfr/Gsn), Nose as a #Prc unit double, Mnch, and
// the FlRs random seed verbatim (July 2026 captures). Patchy renders its own
// deterministic position-hashed noise from the same seed.
struct AddNoiseSmartFilter {
  double amount_percent{12.5};
  bool gaussian{false};
  bool monochromatic{false};
  std::int32_t seed{1};
  bool operator==(const AddNoiseSmartFilter&) const = default;
};

// Settings of a table-described filter: one integer per schema field, in
// Photoshop's descriptor key order (core/smart_filter_schema.hpp).
struct SchemaSmartFilter {
  std::vector<std::int32_t> values;
  // Horizontal, vertical pairs of a point-list field (Shear's curve), in order.
  std::vector<double> points;
  bool operator==(const SchemaSmartFilter&) const = default;
};
// An Image > Adjustments command stored as a Smart Filter. Photoshop keeps the
// command's action descriptor (Lvls, Crvs, HStr, BrgC, ...) as the entry's Fltr
// object; Patchy reads it into the adjustment-layer model and renders it with
// the adjustment-layer math (docs/smart-filters-adjustments.md).
struct AdjustmentSmartFilter {
  AdjustmentSettings settings;
};
// Compares the settings of the adjustment kind only (AdjustmentSettings holds
// every kind's settings side by side).
[[nodiscard]] bool operator==(const AdjustmentSmartFilter& a,
                              const AdjustmentSmartFilter& b);
// Photoshop's parameterless classic filters store no Fltr object: the entry
// carries only its name, hasoptions (false from Photoshop 2026; older files
// wrote true for Invert, kept as read) and a charID filterID (October 2026
// captures). Their SmartFilterEntry::parameters stay std::monostate.
struct ParameterlessSmartFilterNative {
  SmartFilterKind kind;
  const char* native_name;
  std::uint32_t filter_id;
};

inline constexpr ParameterlessSmartFilterNative kParameterlessSmartFilters[] = {
    {SmartFilterKind::Blur, "Blur", 0x426c7220U},              // 'Blr '
    {SmartFilterKind::BlurMore, "Blur More", 0x426c724dU},     // 'BlrM'
    {SmartFilterKind::Sharpen, "Sharpen", 0x53687270U},        // 'Shrp'
    {SmartFilterKind::SharpenMore, "Sharpen More", 0x5368724dU},  // 'ShrM'
    {SmartFilterKind::Invert, "Invert", 0x496e7672U},          // 'Invr'
    {SmartFilterKind::Average, "Average", 0x41767267U},        // 'Avrg'
};

[[nodiscard]] inline const ParameterlessSmartFilterNative*
parameterless_smart_filter_native(SmartFilterKind kind) noexcept {
  for (const auto& native : kParameterlessSmartFilters) {
    if (native.kind == kind) {
      return &native;
    }
  }
  return nullptr;
}

[[nodiscard]] inline const ParameterlessSmartFilterNative*
parameterless_smart_filter_native_for_id(std::uint32_t filter_id) noexcept {
  for (const auto& native : kParameterlessSmartFilters) {
    if (native.filter_id == filter_id) {
      return &native;
    }
  }
  return nullptr;
}

// One effect of a Filter Gallery entry: Photoshop's GEft id ("PlsW") and its
// settings. Only effects with a Patchy renderer are modeled.
using FilterGalleryEffectSettings = std::variant<PlasticWrapSmartFilter>;
struct FilterGalleryEffect {
  std::string effect_id;
  FilterGalleryEffectSettings settings;
  bool operator==(const FilterGalleryEffect&) const = default;
};

// A Filter Gallery entry (GEfc): its effects in application order. Photoshop
// writes one effect inline in the GEfc object and two or more as a GEfs list
// of GEfc objects (October 2026 captures, docs/smart-filters-native.md).
struct FilterGallerySmartFilter {
  std::vector<FilterGalleryEffect> effects;
  bool listed{false};  // the GEfs list form
  bool operator==(const FilterGallerySmartFilter&) const = default;
};

using SmartFilterParameters =
    std::variant<std::monostate, GaussianBlurSmartFilter, HighPassSmartFilter,
                 MedianSmartFilter, DustAndScratchesSmartFilter,
                 SurfaceBlurSmartFilter, UnsharpMaskSmartFilter,
                 MotionBlurSmartFilter, PlasticWrapSmartFilter,
                 MosaicSmartFilter, EmbossSmartFilter, BoxBlurSmartFilter,
                 RadialBlurSmartFilter, AddNoiseSmartFilter,
                 SchemaSmartFilter, TableSmartFilter, AdjustmentSmartFilter,
                 FilterGallerySmartFilter>;

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

// The Plastic Wrap settings an entry's settings dialog edits: a Plastic Wrap entry's, or
// the one effect of a single-effect Filter Gallery entry. Null for anything else.
[[nodiscard]] const PlasticWrapSmartFilter* editable_plastic_wrap(const SmartFilterEntry& entry) noexcept;
[[nodiscard]] PlasticWrapSmartFilter* editable_plastic_wrap(SmartFilterEntry& entry) noexcept;
// The kind whose settings dialog edits `entry`: Plastic Wrap for a single-effect Plastic
// Wrap gallery entry, the entry's own kind otherwise.
[[nodiscard]] SmartFilterKind smart_filter_dialog_kind(const SmartFilterEntry& entry) noexcept;

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
  // The native stack around the entries: root flags, entry records with a readable
  // enable flag, the shared mask, and the FEid render cache. Import clears it when any
  // of them is outside the verified subset; entries may still be filters Patchy does not
  // model (kind Unsupported) while it holds (GitHub issue 243).
  bool native_shape_supported{true};
  // Fail closed: import code promotes this only after every entry and the
  // shared mask have been decoded into the verified semantic subset. Recompute it with
  // smart_filter_stack_support after changing entries or enable flags.
  SmartFilterStackSupport support{SmartFilterStackSupport::Unsupported};
};

// Whether Patchy can render the stack itself: its native shape is supported and every
// filter Photoshop would run is one Patchy models. A hidden entry, or every entry of a
// hidden stack, does not run and may be a filter Patchy only preserves (GitHub issue 243).
[[nodiscard]] inline bool smart_filter_entry_runs(const SmartFilterStack& stack,
                                                  const SmartFilterEntry& entry) noexcept {
  return stack.enabled && entry.enabled;
}

[[nodiscard]] inline SmartFilterStackSupport smart_filter_stack_support(
    const SmartFilterStack& stack) noexcept {
  if (!stack.native_shape_supported || stack.entries.empty() || stack.mask.linked) {
    return SmartFilterStackSupport::Unsupported;
  }
  for (const auto& entry : stack.entries) {
    if (entry.kind == SmartFilterKind::Unsupported && smart_filter_entry_runs(stack, entry)) {
      return SmartFilterStackSupport::Unsupported;
    }
  }
  return SmartFilterStackSupport::Supported;
}

}  // namespace patchy
