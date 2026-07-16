#include "filters/smart_filter_recipe_mapping.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace patchy {

std::optional<SmartFilterKind> native_smart_filter_kind_for(
    std::string_view filter_id) {
  if (filter_id == "patchy.filters.gaussian_blur") {
    return SmartFilterKind::GaussianBlur;
  }
  if (filter_id == "patchy.filters.high_pass") {
    return SmartFilterKind::HighPass;
  }
  if (filter_id == "patchy.filters.median") {
    return SmartFilterKind::Median;
  }
  if (filter_id == "patchy.filters.dust_and_scratches") {
    return SmartFilterKind::DustAndScratches;
  }
  if (filter_id == "patchy.filters.surface_blur") {
    return SmartFilterKind::SurfaceBlur;
  }
  if (filter_id == "patchy.filters.unsharp_mask") {
    return SmartFilterKind::UnsharpMask;
  }
  if (filter_id == "patchy.filters.motion_blur") {
    return SmartFilterKind::MotionBlur;
  }
  if (filter_id == "patchy.filters.plastic_wrap") {
    return SmartFilterKind::PlasticWrap;
  }
  if (filter_id == "patchy.filters.pixelate") {
    return SmartFilterKind::Mosaic;
  }
  if (filter_id == "patchy.filters.emboss") {
    return SmartFilterKind::Emboss;
  }
  if (filter_id == "patchy.filters.box_blur") {
    return SmartFilterKind::BoxBlur;
  }
  if (filter_id == "patchy.filters.radial_blur") {
    return SmartFilterKind::RadialBlur;
  }
  if (filter_id == "patchy.filters.add_noise") {
    return SmartFilterKind::AddNoise;
  }
  return std::nullopt;
}

bool native_smart_filter_kind_supported(SmartFilterKind kind) {
  switch (kind) {
    case SmartFilterKind::GaussianBlur:
    case SmartFilterKind::HighPass:
    case SmartFilterKind::Median:
    case SmartFilterKind::DustAndScratches:
    case SmartFilterKind::SurfaceBlur:
    case SmartFilterKind::UnsharpMask:
    case SmartFilterKind::MotionBlur:
    case SmartFilterKind::PlasticWrap:
    case SmartFilterKind::Mosaic:
    case SmartFilterKind::Emboss:
    case SmartFilterKind::BoxBlur:
    case SmartFilterKind::RadialBlur:
    case SmartFilterKind::AddNoise:
      return true;
    case SmartFilterKind::Unsupported:
      return false;
  }
  return false;
}

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
        !native_smart_filter_kind_for(normalized->filter_id).has_value()) {
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
    const auto plastic =
        normalized->filter_id == "patchy.filters.plastic_wrap";
    const auto mosaic = normalized->filter_id == "patchy.filters.pixelate";
    const auto emboss = normalized->filter_id == "patchy.filters.emboss";
    const auto box_blur = normalized->filter_id == "patchy.filters.box_blur";
    const auto radial_blur = normalized->filter_id == "patchy.filters.radial_blur";
    const auto add_noise = normalized->filter_id == "patchy.filters.add_noise";
    SmartFilterEntry entry;
    if (radial_blur) {
      const auto amount_value = normalized->parameters.find("amount");
      const auto samples_value = normalized->parameters.find("samples");
      const auto center_x_value = normalized->parameters.find("center_x");
      const auto center_y_value = normalized->parameters.find("center_y");
      if (amount_value == normalized->parameters.end() ||
          samples_value == normalized->parameters.end() ||
          center_x_value == normalized->parameters.end() ||
          center_y_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *amount = std::get_if<std::int64_t>(&amount_value->second);
      const auto *samples = std::get_if<std::int64_t>(&samples_value->second);
      const auto *center_x = std::get_if<double>(&center_x_value->second);
      const auto *center_y = std::get_if<double>(&center_y_value->second);
      // Photoshop's descriptor has no blur center and its Amount minimum is
      // 1, so an edited center or amount 0 stays destructive-only. Samples
      // must land exactly on a quality tier (Draft 8, Good 16, Best 32).
      if (amount == nullptr || *amount < 1 || *amount > 100 ||
          samples == nullptr ||
          (*samples != 8 && *samples != 16 && *samples != 32) ||
          center_x == nullptr || *center_x != 50.0 || center_y == nullptr ||
          *center_y != 50.0) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::RadialBlur;
      entry.native_name = "Radial Blur...";
      entry.native_class_id = "RdlB";
      entry.native_filter_id = 0x52646c42U;
      entry.parameters = RadialBlurSmartFilter{
          static_cast<std::int32_t>(*amount),
          *samples == 8 ? RadialBlurQuality::Draft
                        : (*samples == 16 ? RadialBlurQuality::Good
                                          : RadialBlurQuality::Best)};
    } else if (add_noise) {
      const auto amount_value = normalized->parameters.find("amount");
      const auto distribution_value =
          normalized->parameters.find("distribution");
      const auto monochromatic_value =
          normalized->parameters.find("monochromatic");
      const auto seed_value = normalized->parameters.find("seed");
      if (amount_value == normalized->parameters.end() ||
          distribution_value == normalized->parameters.end() ||
          monochromatic_value == normalized->parameters.end() ||
          seed_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *amount = std::get_if<double>(&amount_value->second);
      const auto *distribution =
          std::get_if<std::string>(&distribution_value->second);
      const auto *monochromatic =
          std::get_if<bool>(&monochromatic_value->second);
      const auto *seed = std::get_if<std::int64_t>(&seed_value->second);
      if (amount == nullptr || !std::isfinite(*amount) || *amount < 0.1 ||
          *amount > 400.0 || distribution == nullptr ||
          (*distribution != "uniform" && *distribution != "gaussian") ||
          monochromatic == nullptr || seed == nullptr || *seed < 0 ||
          *seed > 999999999) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::AddNoise;
      entry.native_name = "Add Noise...";
      entry.native_class_id = "AdNs";
      entry.native_filter_id = 0x41644e73U;
      entry.parameters = AddNoiseSmartFilter{
          *amount, *distribution == "gaussian", *monochromatic,
          static_cast<std::int32_t>(*seed)};
    } else if (box_blur) {
      const auto radius_value = normalized->parameters.find("radius");
      if (radius_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *radius = std::get_if<std::int64_t>(&radius_value->second);
      if (radius == nullptr || *radius < 1 || *radius > 2000) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::BoxBlur;
      entry.native_name = "Box Blur...";
      entry.native_class_id = "boxblur";
      entry.native_filter_id = 843U;
      entry.parameters = BoxBlurSmartFilter{static_cast<double>(*radius)};
    } else if (emboss) {
      const auto angle_value = normalized->parameters.find("angle");
      const auto height_value = normalized->parameters.find("height");
      const auto amount_value = normalized->parameters.find("amount");
      if (angle_value == normalized->parameters.end() ||
          height_value == normalized->parameters.end() ||
          amount_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *angle = std::get_if<std::int64_t>(&angle_value->second);
      const auto *height = std::get_if<std::int64_t>(&height_value->second);
      const auto *amount = std::get_if<std::int64_t>(&amount_value->second);
      // Photoshop's Amount minimum is 1; a Patchy amount of 0 stays
      // destructive-only, which keeps the all-or-nothing rule intact.
      if (angle == nullptr || *angle < -360 || *angle > 360 ||
          height == nullptr || *height < 1 || *height > 100 ||
          amount == nullptr || *amount < 1 || *amount > 500) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::Emboss;
      entry.native_name = "Emboss...";
      entry.native_class_id = "Embs";
      entry.native_filter_id = 0x456d6273U;
      entry.parameters = EmbossSmartFilter{
          static_cast<std::int32_t>(*angle),
          static_cast<std::int32_t>(*height),
          static_cast<std::int32_t>(*amount)};
    } else if (mosaic) {
      const auto block_value = normalized->parameters.find("block_size");
      if (block_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *block = std::get_if<std::int64_t>(&block_value->second);
      if (block == nullptr || *block < 2 || *block > 200) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::Mosaic;
      entry.native_name = "Mosaic...";
      entry.native_class_id = "Msc ";
      entry.native_filter_id = 0x4d736320U;
      entry.parameters =
          MosaicSmartFilter{static_cast<std::int32_t>(*block)};
    } else if (plastic) {
      const auto highlight_value =
          normalized->parameters.find("highlight_strength");
      const auto detail_value = normalized->parameters.find("detail");
      const auto smoothness_value =
          normalized->parameters.find("smoothness");
      if (highlight_value == normalized->parameters.end() ||
          detail_value == normalized->parameters.end() ||
          smoothness_value == normalized->parameters.end()) {
        return std::nullopt;
      }
      const auto *highlight =
          std::get_if<std::int64_t>(&highlight_value->second);
      const auto *detail = std::get_if<std::int64_t>(&detail_value->second);
      const auto *smoothness =
          std::get_if<std::int64_t>(&smoothness_value->second);
      if (highlight == nullptr || *highlight < 0 || *highlight > 20 ||
          detail == nullptr || *detail < 1 || *detail > 15 ||
          smoothness == nullptr || *smoothness < 1 || *smoothness > 15) {
        return std::nullopt;
      }
      entry.kind = SmartFilterKind::PlasticWrap;
      entry.native_name = "Plastic Wrap...";
      entry.native_class_id = "PlsW";
      entry.native_filter_id = 0x506c7357U;
      entry.parameters = PlasticWrapSmartFilter{
          static_cast<std::int32_t>(*highlight),
          static_cast<std::int32_t>(*detail),
          static_cast<std::int32_t>(*smoothness)};
    } else if (unsharp) {
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
