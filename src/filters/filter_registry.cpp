#include "filters/filter_registry.hpp"

#include "core/blend_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace patchy {

namespace {

bool parameter_value_matches(const FilterParameterDefinition &definition,
                             const FilterParameterValue &value) {
  switch (definition.kind) {
  case FilterParameterKind::Integer:
    return std::holds_alternative<std::int64_t>(value);
  case FilterParameterKind::Double:
    return std::holds_alternative<double>(value) &&
           std::isfinite(std::get<double>(value));
  case FilterParameterKind::Boolean:
    return std::holds_alternative<bool>(value);
  case FilterParameterKind::Option: {
    const auto *option = std::get_if<std::string>(&value);
    if (option == nullptr) {
      return false;
    }
    return std::any_of(definition.options.begin(), definition.options.end(),
                       [option](const FilterParameterOption &candidate) {
                         return candidate.value == *option;
                       });
  }
  }
  return false;
}

FilterParameterValue
clamp_parameter_value(const FilterParameterDefinition &definition,
                      FilterParameterValue value) {
  if (auto *integer = std::get_if<std::int64_t>(&value); integer != nullptr) {
    if (definition.minimum.has_value()) {
      *integer = std::max(
          *integer, static_cast<std::int64_t>(std::ceil(*definition.minimum)));
    }
    if (definition.maximum.has_value()) {
      *integer = std::min(
          *integer, static_cast<std::int64_t>(std::floor(*definition.maximum)));
    }
  } else if (auto *real = std::get_if<double>(&value); real != nullptr) {
    if (definition.minimum.has_value()) {
      *real = std::max(*real, *definition.minimum);
    }
    if (definition.maximum.has_value()) {
      *real = std::min(*real, *definition.maximum);
    }
  }
  return value;
}

void remap_center_parameter_for_padding(
    FilterInvocation &invocation, const FilterParameterDefinition &definition,
    std::int32_t original_extent, int margin) {
  if (margin <= 0 ||
      (definition.presentation !=
           FilterParameterPresentation::CenterXPercent &&
       definition.presentation !=
           FilterParameterPresentation::CenterYPercent)) {
    return;
  }
  const auto found = invocation.parameters.find(definition.key);
  if (found == invocation.parameters.end()) {
    return;
  }

  double percent = 50.0;
  if (const auto *real = std::get_if<double>(&found->second); real != nullptr) {
    percent = *real;
  } else if (const auto *integer =
                 std::get_if<std::int64_t>(&found->second);
             integer != nullptr) {
    percent = static_cast<double>(*integer);
  } else {
    return;
  }

  const auto original_span =
      static_cast<double>(std::max<std::int32_t>(0, original_extent - 1));
  const auto padded_extent = static_cast<std::int64_t>(original_extent) +
                             static_cast<std::int64_t>(margin) * 2;
  if (padded_extent <= 1) {
    return;
  }
  const auto padded_span = static_cast<double>(padded_extent - 1);
  const auto padded_percent =
      100.0 * (static_cast<double>(margin) +
               original_span * std::clamp(percent, 0.0, 100.0) / 100.0) /
      padded_span;
  if (std::holds_alternative<double>(found->second)) {
    found->second = padded_percent;
  } else {
    found->second = static_cast<std::int64_t>(std::llround(padded_percent));
  }
}

void remap_spatial_parameters_for_padding(
    FilterInvocation &invocation, const FilterDefinition &definition,
    std::int32_t original_width, std::int32_t original_height, int margin) {
  const auto original_shorter = static_cast<double>(
      std::max<std::int32_t>(1, std::min(original_width, original_height)));
  const auto padded_shorter = static_cast<double>(std::max<std::int64_t>(
      1, std::min<std::int64_t>(
             static_cast<std::int64_t>(original_width) + margin * 2LL,
             static_cast<std::int64_t>(original_height) + margin * 2LL)));
  for (const auto &parameter : definition.catalog.parameters) {
    if (parameter.presentation ==
        FilterParameterPresentation::CenterXPercent) {
      remap_center_parameter_for_padding(invocation, parameter, original_width,
                                         margin);
    } else if (parameter.presentation ==
               FilterParameterPresentation::CenterYPercent) {
      remap_center_parameter_for_padding(invocation, parameter, original_height,
                                         margin);
    } else if (parameter.presentation ==
                   FilterParameterPresentation::TiltFocusHalfWidthPercent ||
               parameter.presentation ==
                   FilterParameterPresentation::TiltTransitionWidthPercent) {
      const auto found = invocation.parameters.find(parameter.key);
      if (found != invocation.parameters.end()) {
        if (auto *value = std::get_if<double>(&found->second);
            value != nullptr) {
          *value *= original_shorter / padded_shorter;
        }
      }
    }
  }
}

bool recipe_blend_mode_supported(BlendMode mode) noexcept {
  const auto value = static_cast<int>(mode);
  return value >= static_cast<int>(BlendMode::Normal) &&
         value <= static_cast<int>(BlendMode::Divide);
}

FilterProgress filter_progress_phase(const FilterProgress *progress,
                                     int phase_index, int phase_count) {
  if (progress == nullptr || !progress->update) {
    return {};
  }
  return FilterProgress{[progress, phase_index,
                         phase_count](int completed, int total,
                                      FilterProgressStage stage) {
    constexpr int kPhaseScale = 1000;
    const auto safe_phase_count = std::max(1, phase_count);
    const auto safe_total = std::max(1, total);
    const auto clamped_completed = std::clamp(completed, 0, safe_total);
    const auto phase_completed = (clamped_completed * kPhaseScale) / safe_total;
    return progress->update(std::clamp(phase_index, 0, safe_phase_count - 1) *
                                    kPhaseScale +
                                phase_completed,
                            safe_phase_count * kPhaseScale, stage);
  }};
}

void blit_buffer(PixelBuffer &destination, const PixelBuffer &source,
                 std::int32_t x, std::int32_t y) {
  const auto row_bytes = static_cast<std::size_t>(source.width()) *
                         bytes_per_pixel(source.format());
  for (std::int32_t row = 0; row < source.height(); ++row) {
    const auto *source_row = source.pixel(0, row);
    auto *destination_row = destination.pixel(x, y + row);
    std::copy(source_row, source_row + row_bytes, destination_row);
  }
}

Rect union_bounds(Rect left, Rect right) {
  if (left.empty()) {
    return right;
  }
  if (right.empty()) {
    return left;
  }
  const auto x1 = std::min<std::int64_t>(left.x, right.x);
  const auto y1 = std::min<std::int64_t>(left.y, right.y);
  const auto x2 =
      std::max<std::int64_t>(static_cast<std::int64_t>(left.x) + left.width,
                             static_cast<std::int64_t>(right.x) + right.width);
  const auto y2 =
      std::max<std::int64_t>(static_cast<std::int64_t>(left.y) + left.height,
                             static_cast<std::int64_t>(right.y) + right.height);
  if (x1 < std::numeric_limits<std::int32_t>::min() ||
      y1 < std::numeric_limits<std::int32_t>::min() ||
      x2 > std::numeric_limits<std::int32_t>::max() ||
      y2 > std::numeric_limits<std::int32_t>::max() ||
      x2 - x1 > std::numeric_limits<std::int32_t>::max() ||
      y2 - y1 > std::numeric_limits<std::int32_t>::max()) {
    throw std::overflow_error("Filter result bounds overflow");
  }
  return Rect{static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
              static_cast<std::int32_t>(x2 - x1),
              static_cast<std::int32_t>(y2 - y1)};
}

FilterRenderResult blend_recipe_result(FilterRenderResult before,
                                       FilterRenderResult filtered,
                                       double opacity, BlendMode blend_mode) {
  if (opacity <= 0.0) {
    return before;
  }
  if (opacity >= 1.0 && blend_mode == BlendMode::Normal) {
    return filtered;
  }
  if (before.pixels.format() != filtered.pixels.format()) {
    throw std::invalid_argument("Filter recipe changed the pixel format");
  }

  constexpr std::uint64_t kOpacityScale = 65535U;
  const auto effect_weight = static_cast<std::uint64_t>(
      std::llround(std::clamp(opacity, 0.0, 1.0) *
                   static_cast<double>(kOpacityScale)));
  if (effect_weight == 0U) {
    return before;
  }

  const auto bounds = union_bounds(before.bounds, filtered.bounds);
  const auto align_to_bounds = [bounds](FilterRenderResult result) {
    if (result.bounds.x == bounds.x && result.bounds.y == bounds.y &&
        result.bounds.width == bounds.width &&
        result.bounds.height == bounds.height) {
      return std::move(result.pixels);
    }
    PixelBuffer aligned(bounds.width, bounds.height, result.pixels.format());
    aligned.clear(0);
    blit_buffer(aligned, result.pixels, result.bounds.x - bounds.x,
                result.bounds.y - bounds.y);
    return aligned;
  };
  auto destination = align_to_bounds(std::move(before));
  auto source = align_to_bounds(std::move(filtered));

  // A recipe filter is an alternate result for the entry's input, not a new
  // layer composited over it. Interpolate the two complete premultiplied
  // results so equal alphas stay equal and an alpha-changing filter moves
  // linearly toward its result. The fixed-point weight makes the rounding path
  // independent of the platform's float evaluation details.
  const auto before_weight = kOpacityScale - effect_weight;
  const auto color_channels =
      std::min<std::uint16_t>(destination.format().channels, 3);
  const auto has_alpha = destination.format().channels >= 4;
  for (std::int32_t y = 0; y < destination.height(); ++y) {
    for (std::int32_t x = 0; x < destination.width(); ++x) {
      auto *dst = destination.pixel(x, y);
      const auto *src = source.pixel(x, y);
      const auto source_alpha =
          static_cast<std::uint64_t>(has_alpha ? src[3] : 255U);
      const auto destination_alpha =
          static_cast<std::uint64_t>(has_alpha ? dst[3] : 255U);
      std::array<std::uint8_t, 3> source_rgb{};
      std::array<std::uint8_t, 3> destination_rgb{};
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        source_rgb[channel] = src[channel];
        destination_rgb[channel] = dst[channel];
      }
      // Blend modes only combine colors where both results have coverage. On a
      // source-only expanded halo, keeping the filtered color avoids Multiply
      // and similar modes turning a clean colored fringe black.
      const auto effect_rgb =
          blend_mode == BlendMode::Normal || source_alpha == 0U ||
                  destination_alpha == 0U
              ? source_rgb
              : blend_rgb(source_rgb, destination_rgb, blend_mode);
      const auto output_alpha_numerator =
          destination_alpha * before_weight + source_alpha * effect_weight;
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        if (output_alpha_numerator == 0U) {
          dst[channel] = 0;
          continue;
        }
        const auto premultiplied_numerator =
            static_cast<std::uint64_t>(destination_rgb[channel]) *
                destination_alpha * before_weight +
            static_cast<std::uint64_t>(effect_rgb[channel]) * source_alpha *
                effect_weight;
        dst[channel] = static_cast<std::uint8_t>(std::min<std::uint64_t>(
            255U, (premultiplied_numerator + output_alpha_numerator / 2U) /
                      output_alpha_numerator));
      }
      if (has_alpha) {
        dst[3] = static_cast<std::uint8_t>(std::min<std::uint64_t>(
            255U, (output_alpha_numerator + kOpacityScale / 2U) /
                      kOpacityScale));
      }
    }
  }
  return FilterRenderResult{std::move(destination), bounds};
}

PixelBuffer pad_buffer_transparent(const PixelBuffer &source, int margin) {
  const auto padded_width = static_cast<std::int64_t>(source.width()) +
                            static_cast<std::int64_t>(margin) * 2;
  const auto padded_height = static_cast<std::int64_t>(source.height()) +
                             static_cast<std::int64_t>(margin) * 2;
  if (padded_width > std::numeric_limits<std::int32_t>::max() ||
      padded_height > std::numeric_limits<std::int32_t>::max()) {
    throw std::overflow_error("Filter result dimensions overflow");
  }
  PixelBuffer padded(static_cast<std::int32_t>(padded_width),
                     static_cast<std::int32_t>(padded_height), source.format());
  padded.clear(0);
  blit_buffer(padded, source, margin, margin);
  return padded;
}

Rect trim_transparent_border(PixelBuffer &buffer, Rect bounds,
                             Rect empty_result_bounds) {
  if (buffer.format().channels < 4 || buffer.empty()) {
    return bounds;
  }
  std::int32_t min_x = buffer.width();
  std::int32_t min_y = buffer.height();
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < buffer.height(); ++y) {
    for (std::int32_t x = 0; x < buffer.width(); ++x) {
      if (buffer.pixel(x, y)[3] != 0) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < min_x || max_y < min_y) {
    const auto local_x = static_cast<std::int64_t>(empty_result_bounds.x) -
                         static_cast<std::int64_t>(bounds.x);
    const auto local_y = static_cast<std::int64_t>(empty_result_bounds.y) -
                         static_cast<std::int64_t>(bounds.y);
    const auto right = local_x + empty_result_bounds.width;
    const auto bottom = local_y + empty_result_bounds.height;
    if (!empty_result_bounds.empty() && local_x >= 0 && local_y >= 0 &&
        right <= buffer.width() && bottom <= buffer.height()) {
      PixelBuffer cropped(empty_result_bounds.width, empty_result_bounds.height,
                          buffer.format());
      const auto row_bytes =
          static_cast<std::size_t>(empty_result_bounds.width) *
          bytes_per_pixel(buffer.format());
      for (std::int32_t y = 0; y < empty_result_bounds.height; ++y) {
        const auto *source_row = buffer.pixel(
            static_cast<std::int32_t>(local_x),
            static_cast<std::int32_t>(local_y) + y);
        auto *destination_row = cropped.pixel(0, y);
        std::copy(source_row, source_row + row_bytes, destination_row);
      }
      buffer = std::move(cropped);
      return empty_result_bounds;
    }
    return bounds;
  }
  if (min_x == 0 && min_y == 0 && max_x == buffer.width() - 1 &&
      max_y == buffer.height() - 1) {
    return bounds;
  }

  const auto width = max_x - min_x + 1;
  const auto height = max_y - min_y + 1;
  PixelBuffer cropped(width, height, buffer.format());
  const auto row_bytes =
      static_cast<std::size_t>(width) * bytes_per_pixel(buffer.format());
  for (std::int32_t y = 0; y < height; ++y) {
    const auto *source_row = buffer.pixel(min_x, min_y + y);
    auto *destination_row = cropped.pixel(0, y);
    std::copy(source_row, source_row + row_bytes, destination_row);
  }
  buffer = std::move(cropped);
  return Rect{bounds.x + min_x, bounds.y + min_y, width, height};
}

} // namespace

FilterCancelled::FilterCancelled() : std::runtime_error("Filter cancelled") {}

void FilterRegistry::register_filter(FilterDefinition filter) {
  if (filter.identifier.empty()) {
    throw std::invalid_argument("Filter identifier cannot be empty");
  }
  if (!filter.apply) {
    throw std::invalid_argument("Filter implementation cannot be empty");
  }
  if (find(filter.identifier) != nullptr) {
    throw std::invalid_argument("Filter identifier is already registered");
  }
  filters_.push_back(std::move(filter));
}

const FilterDefinition *
FilterRegistry::find(std::string_view identifier) const noexcept {
  const auto found = std::find_if(filters_.begin(), filters_.end(),
                                  [identifier](const FilterDefinition &filter) {
                                    return filter.identifier == identifier;
                                  });
  return found == filters_.end() ? nullptr : &*found;
}

const std::vector<FilterDefinition> &FilterRegistry::filters() const noexcept {
  return filters_;
}

void FilterRegistry::apply(std::string_view identifier,
                           PixelBuffer &pixels) const {
  const auto *filter = find(identifier);
  if (filter == nullptr) {
    throw std::invalid_argument("Unknown filter identifier");
  }
  filter->apply(pixels);
}

FilterInvocation FilterRegistry::default_invocation(std::string_view identifier,
                                                    RgbColor foreground,
                                                    RgbColor background) const {
  const auto *filter = find(identifier);
  if (filter == nullptr || !filter->catalog.execute) {
    throw std::invalid_argument("Unknown or uncatalogued filter identifier");
  }
  FilterInvocation invocation;
  invocation.filter_id = filter->identifier;
  invocation.schema_version = filter->catalog.schema_version;
  invocation.foreground = foreground;
  invocation.background = background;
  for (const auto &parameter : filter->catalog.parameters) {
    invocation.parameters.emplace(parameter.key, parameter.default_value);
  }
  return invocation;
}

std::optional<FilterInvocation>
FilterRegistry::normalize(const FilterInvocation &invocation) const {
  const auto *filter = find(invocation.filter_id);
  if (filter == nullptr || !filter->catalog.execute ||
      invocation.schema_version != filter->catalog.schema_version) {
    return std::nullopt;
  }

  auto normalized = default_invocation(
      filter->identifier, invocation.foreground, invocation.background);
  for (const auto &parameter : filter->catalog.parameters) {
    const auto found = invocation.parameters.find(parameter.key);
    if (found == invocation.parameters.end()) {
      continue;
    }
    if (!parameter_value_matches(parameter, found->second)) {
      return std::nullopt;
    }
    normalized.parameters[parameter.key] =
        clamp_parameter_value(parameter, found->second);
  }
  return normalized;
}

bool FilterRegistry::supports(const FilterInvocation &invocation) const {
  return normalize(invocation).has_value();
}

std::optional<FilterInvocation>
FilterRegistry::scale(const FilterInvocation &invocation,
                      double spatial_scale) const {
  if (!std::isfinite(spatial_scale) || spatial_scale <= 0.0) {
    return std::nullopt;
  }
  auto scaled = normalize(invocation);
  if (!scaled.has_value()) {
    return std::nullopt;
  }
  const auto *filter = find(scaled->filter_id);
  if (filter == nullptr) {
    return std::nullopt;
  }
  for (const auto &parameter : filter->catalog.parameters) {
    if (parameter.spatial_scale != FilterSpatialScale::Pixels) {
      continue;
    }
    auto found = scaled->parameters.find(parameter.key);
    if (found == scaled->parameters.end()) {
      continue;
    }
    if (auto *integer = std::get_if<std::int64_t>(&found->second);
        integer != nullptr) {
      const auto value = static_cast<double>(*integer) * spatial_scale;
      if (value <
              static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
          value >
              static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
      }
      *integer = static_cast<std::int64_t>(std::llround(value));
    } else if (auto *real = std::get_if<double>(&found->second);
               real != nullptr) {
      *real *= spatial_scale;
    }
    found->second = clamp_parameter_value(parameter, std::move(found->second));
  }
  return scaled;
}

std::optional<FilterRecipe>
FilterRegistry::scale(const FilterRecipe &recipe, double spatial_scale) const {
  if (!std::isfinite(spatial_scale) || spatial_scale <= 0.0 ||
      !supports(recipe)) {
    return std::nullopt;
  }
  FilterRecipe scaled;
  scaled.entries.reserve(recipe.entries.size());
  for (const auto &entry : recipe.entries) {
    auto invocation = scale(entry.invocation, spatial_scale);
    if (!invocation.has_value()) {
      return std::nullopt;
    }
    auto scaled_entry = entry;
    scaled_entry.invocation = std::move(*invocation);
    scaled.entries.push_back(std::move(scaled_entry));
  }
  return scaled;
}

void FilterRegistry::apply(const FilterInvocation &invocation,
                           PixelBuffer &pixels,
                           const FilterProgress *progress) const {
  const auto normalized = normalize(invocation);
  if (!normalized.has_value()) {
    throw std::invalid_argument("Unsupported filter invocation");
  }
  const auto *filter = find(normalized->filter_id);
  if (filter == nullptr || !filter->catalog.execute) {
    throw std::invalid_argument("Unsupported filter invocation");
  }
  filter->catalog.execute(*this, *normalized, pixels, progress);
}

bool FilterRegistry::supports(const FilterRecipe &recipe) const {
  return std::all_of(recipe.entries.begin(), recipe.entries.end(),
                     [this](const FilterRecipeEntry &entry) {
                       return std::isfinite(entry.opacity) &&
                              entry.opacity >= 0.0 && entry.opacity <= 1.0 &&
                              recipe_blend_mode_supported(entry.blend_mode) &&
                              supports(entry.invocation);
                     });
}

void FilterRegistry::apply(const FilterRecipe &recipe, PixelBuffer &pixels,
                           const FilterProgress *progress) const {
  if (!supports(recipe)) {
    throw std::invalid_argument("Unsupported filter recipe");
  }
  const auto effective_count = static_cast<int>(std::count_if(
      recipe.entries.begin(), recipe.entries.end(),
      [](const FilterRecipeEntry &entry) {
        return entry.enabled && entry.opacity > 0.0;
      }));
  int phase = 0;
  for (const auto &entry : recipe.entries) {
    if (!entry.enabled || entry.opacity <= 0.0) {
      continue;
    }
    auto entry_progress =
        filter_progress_phase(progress, phase++, std::max(1, effective_count));
    if (entry.opacity >= 1.0 && entry.blend_mode == BlendMode::Normal) {
      apply(entry.invocation, pixels, &entry_progress);
      continue;
    }

    auto before = pixels;
    apply(entry.invocation, pixels, &entry_progress);
    const Rect bounds{0, 0, pixels.width(), pixels.height()};
    auto blended = blend_recipe_result(
        FilterRenderResult{std::move(before), bounds},
        FilterRenderResult{std::move(pixels), bounds}, entry.opacity,
        entry.blend_mode);
    pixels = std::move(blended.pixels);
  }
}

int FilterRegistry::output_margin(const FilterInvocation &invocation,
                                  std::int32_t width,
                                  std::int32_t height) const {
  const auto normalized = normalize(invocation);
  if (!normalized.has_value()) {
    throw std::invalid_argument("Unsupported filter invocation");
  }
  const auto *filter = find(normalized->filter_id);
  return filter != nullptr && filter->catalog.output_margin
             ? std::max(
                   0, filter->catalog.output_margin(*normalized, width, height))
             : 0;
}

std::optional<int> FilterRegistry::translation_invariant_support(
    const FilterInvocation &invocation) const {
  const auto normalized = normalize(invocation);
  if (!normalized.has_value()) {
    return std::nullopt;
  }
  const auto *filter = find(normalized->filter_id);
  return filter != nullptr && filter->catalog.translation_support
             ? filter->catalog.translation_support(*normalized)
             : std::nullopt;
}

std::optional<int> FilterRegistry::translation_invariant_support(
    const FilterRecipe &recipe) const {
  if (!supports(recipe)) {
    return std::nullopt;
  }
  std::int64_t total = 0;
  for (const auto &entry : recipe.entries) {
    if (!entry.enabled || entry.opacity <= 0.0) {
      continue;
    }
    const auto support = translation_invariant_support(entry.invocation);
    if (!support.has_value() || *support < 0) {
      return std::nullopt;
    }
    total += *support;
    if (total > std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
  }
  return static_cast<int>(total);
}

FilterRenderResult
FilterRegistry::render(const FilterInvocation &invocation,
                       const PixelBuffer &original, Rect bounds,
                       bool allow_output_expansion,
                       const FilterProgress *progress) const {
  const auto normalized = normalize(invocation);
  if (!normalized.has_value()) {
    throw std::invalid_argument("Unsupported filter invocation");
  }
  const auto margin =
      allow_output_expansion && original.format().channels >= 4
          ? output_margin(*normalized, original.width(), original.height())
          : 0;
  if (margin <= 0) {
    auto pixels = original;
    apply(*normalized, pixels, progress);
    return FilterRenderResult{std::move(pixels), bounds};
  }

  auto pixels = pad_buffer_transparent(original, margin);
  auto padded_invocation = *normalized;
  if (const auto *filter = find(padded_invocation.filter_id);
      filter != nullptr) {
    remap_spatial_parameters_for_padding(padded_invocation, *filter,
                                         original.width(), original.height(),
                                         margin);
  }
  apply(padded_invocation, pixels, progress);
  const auto grown_x = static_cast<std::int64_t>(bounds.x) - margin;
  const auto grown_y = static_cast<std::int64_t>(bounds.y) - margin;
  if (grown_x < std::numeric_limits<std::int32_t>::min() ||
      grown_y < std::numeric_limits<std::int32_t>::min()) {
    throw std::overflow_error("Filter result bounds overflow");
  }
  const Rect grown{static_cast<std::int32_t>(grown_x),
                   static_cast<std::int32_t>(grown_y), pixels.width(),
                   pixels.height()};
  const auto trimmed = trim_transparent_border(pixels, grown, bounds);
  return FilterRenderResult{std::move(pixels), trimmed};
}

FilterRenderResult
FilterRegistry::render(const FilterRecipe &recipe, const PixelBuffer &original,
                       Rect bounds, bool allow_output_expansion,
                       const FilterProgress *progress,
                       FilterRecipeRenderTrace *trace) const {
  if (!supports(recipe)) {
    throw std::invalid_argument("Unsupported filter recipe");
  }
  if (trace != nullptr) {
    trace->entry_input_bounds.clear();
    trace->entry_input_bounds.reserve(recipe.entries.size());
  }
  FilterRenderResult current{original, bounds};
  const auto effective_count = static_cast<int>(std::count_if(
      recipe.entries.begin(), recipe.entries.end(),
      [](const FilterRecipeEntry &entry) {
        return entry.enabled && entry.opacity > 0.0;
      }));
  int phase = 0;
  for (const auto &entry : recipe.entries) {
    if (trace != nullptr) {
      trace->entry_input_bounds.push_back(current.bounds);
    }
    if (!entry.enabled || entry.opacity <= 0.0) {
      continue;
    }
    auto entry_progress =
        filter_progress_phase(progress, phase++, std::max(1, effective_count));
    auto filtered =
        render(entry.invocation, current.pixels, current.bounds,
               allow_output_expansion, &entry_progress);
    if (entry.opacity >= 1.0 && entry.blend_mode == BlendMode::Normal) {
      current = std::move(filtered);
    } else {
      current = blend_recipe_result(std::move(current), std::move(filtered),
                                    entry.opacity, entry.blend_mode);
    }
  }
  return current;
}

} // namespace patchy
