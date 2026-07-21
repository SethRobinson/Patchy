#pragma once

// Shared helpers moved from tests/core/smart_filter_descriptors_tests.cpp
// (used by both split parts of the smart_filter_descriptors test group).

#include "core/smart_filter.hpp"

namespace patchy::test {

const patchy::GaussianBlurSmartFilter& require_gaussian_filter(const patchy::SmartFilterEntry& entry);

const patchy::HighPassSmartFilter& require_high_pass_filter(
    const patchy::SmartFilterEntry& entry);

const patchy::MedianSmartFilter& require_median_filter(
    const patchy::SmartFilterEntry& entry);

const patchy::DustAndScratchesSmartFilter&
require_dust_and_scratches_filter(const patchy::SmartFilterEntry& entry);

const patchy::SurfaceBlurSmartFilter& require_surface_blur_filter(
    const patchy::SmartFilterEntry& entry);

const patchy::PlasticWrapSmartFilter &
require_plastic_wrap_filter(const patchy::SmartFilterEntry &entry);

const patchy::MosaicSmartFilter &
require_mosaic_filter(const patchy::SmartFilterEntry &entry);

const patchy::EmbossSmartFilter &
require_emboss_filter(const patchy::SmartFilterEntry &entry);

const patchy::BoxBlurSmartFilter &
require_box_blur_filter(const patchy::SmartFilterEntry &entry);

}  // namespace patchy::test
