#pragma once

// One registration function per test group, split from tests/test_main.cpp.
// main() concatenates them in this exact order; the concatenation reproduces
// the original registration vector entry for entry.

#include "test_harness.hpp"

#include <vector>

std::vector<patchy::test::TestCase> document_model_tests();
std::vector<patchy::test::TestCase> compositor_blend_if_tests();
std::vector<patchy::test::TestCase> compositor_layer_styles_tests();
std::vector<patchy::test::TestCase> gradients_interior_effects_tests();
std::vector<patchy::test::TestCase> psd_core_io_tests();
std::vector<patchy::test::TestCase> stroke_mask_effects_tests();
std::vector<patchy::test::TestCase> smart_objects_warp_tests();
std::vector<patchy::test::TestCase> smart_filter_pixels_tests();
std::vector<patchy::test::TestCase> smart_filter_descriptors_tests();
std::vector<patchy::test::TestCase> psd_writer_stability_tests();
std::vector<patchy::test::TestCase> pattern_styles_fixtures_tests();
std::vector<patchy::test::TestCase> adjustments_curves_tests();
std::vector<patchy::test::TestCase> psd_structure_tests();
std::vector<patchy::test::TestCase> psd_text_tests();
std::vector<patchy::test::TestCase> layer_metadata_tests();
std::vector<patchy::test::TestCase> brush_engine_tests();
std::vector<patchy::test::TestCase> pat_asl_abr_tests();
std::vector<patchy::test::TestCase> pixel_tools_tests();
std::vector<patchy::test::TestCase> palette_tests();
std::vector<patchy::test::TestCase> document_ops_filters_tests();
std::vector<patchy::test::TestCase> flat_formats_bmp_tests();
std::vector<patchy::test::TestCase> raw_heif_tests();
std::vector<patchy::test::TestCase> flat_formats_misc_tests();
std::vector<patchy::test::TestCase> infra_selection_tests();
