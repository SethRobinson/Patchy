#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace patchy {

using FilterParameterValue =
    std::variant<std::int64_t, double, bool, std::string>;
using FilterParameterMap =
    std::map<std::string, FilterParameterValue, std::less<>>;

struct FilterInvocation {
  std::string filter_id;
  std::uint32_t schema_version{1};
  FilterParameterMap parameters;
  RgbColor foreground{};
  RgbColor background{255, 255, 255};
};

struct FilterRecipeEntry {
  FilterInvocation invocation;
  bool enabled{true};
  double opacity{1.0};
  BlendMode blend_mode{BlendMode::Normal};
};

struct FilterRecipe {
  std::vector<FilterRecipeEntry> entries;
};

enum class FilterCategory {
  Uncategorized,
  Adjustment,
  PhotoLooks,
  Blur,
  Sharpen,
  Distort,
  Noise,
  Pixelate,
  Stylize,
  Render
};

enum class FilterParameterKind { Integer, Double, Boolean, Option };

enum class FilterParameterUnit { None, Percent, Pixels, Degrees };

enum class FilterSpatialScale { None, Pixels };

// Optional semantic/presentation hints for catalog-generated controls. These
// do not affect persistence: parameter keys and values remain authoritative.
enum class FilterParameterPresentation {
  Standard,
  Angle,
  CenterXPercent,
  CenterYPercent,
  EffectRadiusPercent,
  WaveAmplitude,
  WaveWavelength,
  WavePhase
};

struct FilterParameterOption {
  std::string value;
  std::string display_name;
};

struct FilterParameterDefinition {
  std::string key;
  std::string display_name;
  std::string control_object_name;
  FilterParameterKind kind{FilterParameterKind::Integer};
  FilterParameterValue default_value{std::int64_t{0}};
  std::optional<double> minimum;
  std::optional<double> maximum;
  std::optional<double> step;
  FilterParameterUnit unit{FilterParameterUnit::None};
  FilterSpatialScale spatial_scale{FilterSpatialScale::None};
  std::vector<FilterParameterOption> options;
  FilterParameterPresentation presentation{
      FilterParameterPresentation::Standard};
};

enum class FilterProgressStage {
  Filtering,
  Blurring,
  Sharpening,
  DetectingEdges,
  Distorting,
  Twisting,
  Embossing,
  GeneratingClouds,
  Pixelating,
  RenderingHalftone,
  AddingGrain,
  ApplyingVignette
};

struct FilterProgress {
  std::function<bool(int completed, int total, FilterProgressStage stage)>
      update;
};

class FilterCancelled final : public std::runtime_error {
public:
  FilterCancelled();
};

struct FilterRenderResult {
  PixelBuffer pixels;
  Rect bounds{};
};

class FilterRegistry;

using PixelFilterFn = std::function<void(PixelBuffer &)>;
using ParameterizedPixelFilterFn =
    std::function<void(const FilterRegistry &, const FilterInvocation &,
                       PixelBuffer &, const FilterProgress *)>;
using FilterOutputMarginFn = std::function<int(
    const FilterInvocation &, std::int32_t width, std::int32_t height)>;
using FilterTranslationSupportFn =
    std::function<std::optional<int>(const FilterInvocation &)>;

struct FilterCatalogMetadata {
  FilterCategory category{FilterCategory::Uncategorized};
  bool adjustment_only{false};
  std::uint32_t schema_version{1};
  std::vector<FilterParameterDefinition> parameters;
  ParameterizedPixelFilterFn execute;
  FilterOutputMarginFn output_margin;
  FilterTranslationSupportFn translation_support;
};

struct FilterDefinition {
  std::string identifier;
  std::string display_name;
  PixelFilterFn apply;
  FilterCatalogMetadata catalog;
};

class FilterRegistry {
public:
  void register_filter(FilterDefinition filter);
  [[nodiscard]] const FilterDefinition *
  find(std::string_view identifier) const noexcept;
  [[nodiscard]] const std::vector<FilterDefinition> &filters() const noexcept;

  // Compatibility path. This deliberately keeps each original built-in
  // implementation and its historical output; it does not redirect through the
  // catalog defaults.
  void apply(std::string_view identifier, PixelBuffer &pixels) const;

  [[nodiscard]] FilterInvocation
  default_invocation(std::string_view identifier, RgbColor foreground = {},
                     RgbColor background = {255, 255, 255}) const;
  [[nodiscard]] bool supports(const FilterInvocation &invocation) const;
  [[nodiscard]] std::optional<FilterInvocation>
  normalize(const FilterInvocation &invocation) const;
  [[nodiscard]] std::optional<FilterInvocation>
  scale(const FilterInvocation &invocation, double spatial_scale) const;
  void apply(const FilterInvocation &invocation, PixelBuffer &pixels,
             const FilterProgress *progress = nullptr) const;

  [[nodiscard]] bool supports(const FilterRecipe &recipe) const;
  void apply(const FilterRecipe &recipe, PixelBuffer &pixels,
             const FilterProgress *progress = nullptr) const;

  [[nodiscard]] int output_margin(const FilterInvocation &invocation,
                                  std::int32_t width,
                                  std::int32_t height) const;
  [[nodiscard]] std::optional<int>
  translation_invariant_support(const FilterInvocation &invocation) const;
  [[nodiscard]] FilterRenderResult
  render(const FilterInvocation &invocation, const PixelBuffer &original,
         Rect bounds, bool allow_output_expansion = true,
         const FilterProgress *progress = nullptr) const;
  [[nodiscard]] FilterRenderResult
  render(const FilterRecipe &recipe, const PixelBuffer &original, Rect bounds,
         bool allow_output_expansion = true,
         const FilterProgress *progress = nullptr) const;

private:
  std::vector<FilterDefinition> filters_;
};

void register_builtin_filters(FilterRegistry &registry);

} // namespace patchy
