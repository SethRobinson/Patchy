#include "smart_filter_descriptors_test_support.hpp"

#include "test_harness.hpp"

#include <variant>

namespace patchy::test {

const patchy::GaussianBlurSmartFilter& require_gaussian_filter(const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::GaussianBlur);
  const auto* gaussian = std::get_if<patchy::GaussianBlurSmartFilter>(&entry.parameters);
  CHECK(gaussian != nullptr);
  return *gaussian;
}

const patchy::HighPassSmartFilter& require_high_pass_filter(
    const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::HighPass);
  const auto* high_pass =
      std::get_if<patchy::HighPassSmartFilter>(&entry.parameters);
  CHECK(high_pass != nullptr);
  return *high_pass;
}

const patchy::MedianSmartFilter& require_median_filter(
    const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::Median);
  const auto* median =
      std::get_if<patchy::MedianSmartFilter>(&entry.parameters);
  CHECK(median != nullptr);
  return *median;
}

const patchy::DustAndScratchesSmartFilter&
require_dust_and_scratches_filter(const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::DustAndScratches);
  const auto* dust =
      std::get_if<patchy::DustAndScratchesSmartFilter>(&entry.parameters);
  CHECK(dust != nullptr);
  return *dust;
}

const patchy::SurfaceBlurSmartFilter& require_surface_blur_filter(
    const patchy::SmartFilterEntry& entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::SurfaceBlur);
  const auto* surface =
      std::get_if<patchy::SurfaceBlurSmartFilter>(&entry.parameters);
  CHECK(surface != nullptr);
  return *surface;
}

const patchy::PlasticWrapSmartFilter &
require_plastic_wrap_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::PlasticWrap);
  const auto *plastic =
      std::get_if<patchy::PlasticWrapSmartFilter>(&entry.parameters);
  CHECK(plastic != nullptr);
  return *plastic;
}

const patchy::MosaicSmartFilter &
require_mosaic_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::Mosaic);
  const auto *mosaic =
      std::get_if<patchy::MosaicSmartFilter>(&entry.parameters);
  CHECK(mosaic != nullptr);
  return *mosaic;
}

const patchy::EmbossSmartFilter &
require_emboss_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::Emboss);
  const auto *emboss =
      std::get_if<patchy::EmbossSmartFilter>(&entry.parameters);
  CHECK(emboss != nullptr);
  return *emboss;
}

const patchy::BoxBlurSmartFilter &
require_box_blur_filter(const patchy::SmartFilterEntry &entry) {
  CHECK(entry.kind == patchy::SmartFilterKind::BoxBlur);
  const auto *box =
      std::get_if<patchy::BoxBlurSmartFilter>(&entry.parameters);
  CHECK(box != nullptr);
  return *box;
}

}  // namespace patchy::test
