#include "filters/smart_filter_recipe_mapping.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

namespace patchy {

std::optional<std::vector<SmartFilterEntry>>
smart_filter_entries_from_recipe(const FilterRecipe& recipe,
                                 const FilterRegistry& registry) {
  if (recipe.entries.empty() || !registry.supports(recipe)) {
    return std::nullopt;
  }
  std::vector<SmartFilterEntry> mapped;
  mapped.reserve(recipe.entries.size());
  for (const auto& recipe_entry : recipe.entries) {
    const auto normalized = registry.normalize(recipe_entry.invocation);
    if (!normalized.has_value() || normalized->schema_version != 1U ||
        (normalized->filter_id != "patchy.filters.gaussian_blur" &&
         normalized->filter_id != "patchy.filters.high_pass" &&
         normalized->filter_id != "patchy.filters.median" &&
         normalized->filter_id != "patchy.filters.dust_and_scratches" &&
         normalized->filter_id != "patchy.filters.surface_blur" &&
         normalized->filter_id != "patchy.filters.unsharp_mask" &&
         normalized->filter_id != "patchy.filters.motion_blur")) {
      return std::nullopt;
    }
    const auto dust =
        normalized->filter_id == "patchy.filters.dust_and_scratches";
    const auto surface =
        normalized->filter_id == "patchy.filters.surface_blur";
    const auto median = normalized->filter_id == "patchy.filters.median";
    const auto high_pass = normalized->filter_id == "patchy.filters.high_pass";
    const auto unsharp = normalized->filter_id == "patchy.filters.unsharp_mask";
    const auto motion = normalized->filter_id == "patchy.filters.motion_blur";
    SmartFilterEntry entry;
    if (unsharp) {
      const auto amount_value = normalized->parameters.find("amount");
      const auto radius_value = normalized->parameters.find("radius");
      const auto threshold_value = normalized->parameters.find("threshold");
      if (amount_value == normalized->parameters.end() ||
          radius_value == normalized->parameters.end() ||
          threshold_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *amount = std::get_if<std::int64_t>(&amount_value->second);
      const auto *radius = std::get_if<double>(&radius_value->second);
      const auto *threshold =
          std::get_if<std::int64_t>(&threshold_value->second);
      if (amount == nullptr || *amount < 1 || *amount > 500 ||
          radius == nullptr || !std::isfinite(*radius) || *radius < 0.1 ||
          *radius > 1000.0 || threshold == nullptr || *threshold < 0 ||
          *threshold > 255) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::UnsharpMask;
      entry.native_name = "Unsharp Mask...";
      entry.native_class_id = "UnsM";
      entry.native_filter_id = 0x556e734dU;
      entry.parameters =
          UnsharpMaskSmartFilter{static_cast<double>(*amount), *radius,
                                 static_cast<std::int32_t>(*threshold)};
    } else if (motion) {
      const auto angle_value = normalized->parameters.find("angle");
      const auto distance_value = normalized->parameters.find("distance");
      if (angle_value == normalized->parameters.end() ||
          distance_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *angle = std::get_if<std::int64_t>(&angle_value->second);
      const auto *distance = std::get_if<std::int64_t>(&distance_value->second);
      if (angle == nullptr || *angle < -360 || *angle > 360 ||
          distance == nullptr || *distance < 1 || *distance > 999) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::MotionBlur;
      entry.native_name = "Motion Blur...";
      entry.native_class_id = "MtnB";
      entry.native_filter_id = 0x4d746e42U;
      entry.parameters =
          MotionBlurSmartFilter{static_cast<std::int32_t>(*angle),
                                static_cast<std::int32_t>(*distance)};
    } else if (dust) {
      const auto radius_value = normalized->parameters.find("radius");
      const auto threshold_value = normalized->parameters.find("threshold");
      if (radius_value == normalized->parameters.end() ||
          threshold_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto* radius =
          std::get_if<std::int64_t>(&radius_value->second);
      const auto* threshold =
          std::get_if<std::int64_t>(&threshold_value->second);
      if (radius == nullptr || threshold == nullptr || *radius < 1 ||
          *radius > 100 || *threshold < 0 || *threshold > 255) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::DustAndScratches;
      entry.native_name = "Dust && Scratches...";
      entry.native_class_id = "DstS";
      entry.native_filter_id = 0x44737453U;
      entry.parameters = DustAndScratchesSmartFilter{
          static_cast<std::int32_t>(*radius),
          static_cast<std::int32_t>(*threshold)};
    } else if (surface) {
      const auto radius_value = normalized->parameters.find("radius");
      const auto threshold_value = normalized->parameters.find("threshold");
      if (radius_value == normalized->parameters.end() ||
          threshold_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto* radius = std::get_if<double>(&radius_value->second);
      const auto* threshold =
          std::get_if<std::int64_t>(&threshold_value->second);
      if (radius == nullptr || !std::isfinite(*radius) || *radius < 1.0 ||
          *radius > 100.0 || threshold == nullptr || *threshold < 2 ||
          *threshold > 255) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::SurfaceBlur;
      entry.native_name = "Surface Blur...";
      entry.native_class_id = "surfaceBlur";
      entry.native_filter_id = 854U;
      entry.parameters = SurfaceBlurSmartFilter{
          *radius, static_cast<std::int32_t>(*threshold)};
    } else {
      const auto radius_value = normalized->parameters.find("radius");
      if (radius_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      double radius = 0.0;
      if (const auto* integer =
              std::get_if<std::int64_t>(&radius_value->second);
          integer != nullptr) {
        radius = static_cast<double>(*integer);
      } else if (const auto* real =
                     std::get_if<double>(&radius_value->second);
                 real != nullptr) {
        radius = *real;
      } else {
        return std::nullopt;
      }
      if (!std::isfinite(radius) || radius < (median ? 1.0 : 0.1) ||
          radius > (median ? 500.0 : 1000.0)) {
        return std::nullopt;
      }
      entry.kind = median ? SmartFilterKind::Median
                          : (high_pass ? SmartFilterKind::HighPass
                                       : SmartFilterKind::GaussianBlur);
      entry.native_name = median ? "Median..."
                                 : (high_pass ? "High Pass..."
                                              : "Gaussian Blur...");
      entry.native_class_id =
          median ? "Mdn " : (high_pass ? "HghP" : "GsnB");
      entry.native_filter_id = median ? 0x4d646e20U
                                      : (high_pass ? 0x48676850U
                                                   : 0x47736e42U);
      if (median) {
        entry.parameters = MedianSmartFilter{radius};
      } else if (high_pass) {
        entry.parameters = HighPassSmartFilter{radius};
      } else {
        entry.parameters = GaussianBlurSmartFilter{radius};
      }
    }
    entry.enabled = recipe_entry.enabled;
    entry.has_options = true;
    entry.opacity = recipe_entry.opacity;
    entry.blend_mode = recipe_entry.blend_mode;
    entry.foreground = normalized->foreground;
    entry.background = normalized->background;
    mapped.push_back(std::move(entry));
  }
  return mapped;
}

}  // namespace patchy
