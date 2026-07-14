#include "test_groups.hpp"

#include "test_harness.hpp"

#include <exception>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

using patchy::test::TestCase;

int main(int argc, char** argv) {
  patchy::test::suppress_crash_dialogs();
  std::vector<TestCase> tests;
  for (const auto& registration : {
           document_model_tests,
           compositor_blend_if_tests,
           compositor_layer_styles_tests,
           gradients_interior_effects_tests,
           psd_core_io_tests,
           stroke_mask_effects_tests,
           smart_objects_warp_tests,
           smart_filter_pixels_tests,
           smart_filter_descriptors_tests,
           psd_writer_stability_tests,
           pattern_styles_fixtures_tests,
           adjustments_curves_tests,
           psd_structure_tests,
           psd_text_tests,
           layer_metadata_tests,
           brush_engine_tests,
           pat_asl_abr_tests,
           pixel_tools_tests,
           palette_tests,
           document_ops_filters_tests,
           flat_formats_bmp_tests,
           raw_heif_tests,
           flat_formats_misc_tests,
           infra_selection_tests,
       }) {
    auto group = registration();
    tests.insert(tests.end(), std::make_move_iterator(group.begin()),
                 std::make_move_iterator(group.end()));
  }

  int failures = 0;
  const std::string_view name_filter =
      argc > 1 && argv[1] != nullptr ? std::string_view(argv[1])
                                     : std::string_view{};
  for (const auto& test : tests) {
    if (!name_filter.empty() &&
        std::string_view(test.name).find(name_filter) ==
            std::string_view::npos) {
      continue;
    }
    try {
      test.run();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
    }
  }

  return failures == 0 ? 0 : 1;
}
