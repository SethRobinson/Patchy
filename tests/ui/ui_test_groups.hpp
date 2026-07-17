#pragma once

// One registration function per UI test group, split from the retired
// tests/ui_visual_tests.cpp monolith. main() concatenates them in this exact
// order; the concatenation reproduces the original registration vector entry
// for entry.

#include "test_harness.hpp"

#include <vector>

std::vector<patchy::test::TestCase> app_shell_tests();
std::vector<patchy::test::TestCase> filter_catalog_dialog_tests();
std::vector<patchy::test::TestCase> layer_style_gradient_tests();
std::vector<patchy::test::TestCase> destructive_filters_gallery_tests();
std::vector<patchy::test::TestCase> pickers_notices_hotkeys_tests();
std::vector<patchy::test::TestCase> canvas_view_tools_tests();
std::vector<patchy::test::TestCase> layer_context_lifecycle_tests();
std::vector<patchy::test::TestCase> brush_pattern_palette_tests();
std::vector<patchy::test::TestCase> layer_panel_organization_tests();
std::vector<patchy::test::TestCase> move_tool_processing_overlay_tests();
std::vector<patchy::test::TestCase> selection_marquee_lasso_tests();
std::vector<patchy::test::TestCase> clipboard_free_transform_tests();
std::vector<patchy::test::TestCase> channels_panel_tests();
std::vector<patchy::test::TestCase> camera_raw_heif_tests();
std::vector<patchy::test::TestCase> layer_mask_tests();
std::vector<patchy::test::TestCase> pen_tablet_input_tests();
std::vector<patchy::test::TestCase> brush_engine_stroke_tests();
std::vector<patchy::test::TestCase> text_editor_font_picker_tests();
std::vector<patchy::test::TestCase> psd_text_import_tests();
std::vector<patchy::test::TestCase> text_transform_commit_tests();
std::vector<patchy::test::TestCase> flat_image_format_tests();
std::vector<patchy::test::TestCase> smart_filter_tests();
std::vector<patchy::test::TestCase> smart_object_tests();
std::vector<patchy::test::TestCase> warp_tests();
std::vector<patchy::test::TestCase> import_print_resolution_tests();
std::vector<patchy::test::TestCase> image_adjustments_curves_tests();
std::vector<patchy::test::TestCase> selection_engines_tests();
std::vector<patchy::test::TestCase> misc_visuals_outline_stress_tests();
std::vector<patchy::test::TestCase> float_window_tests();
std::vector<patchy::test::TestCase> vector_shape_tool_tests();
std::vector<patchy::test::TestCase> svg_ui_tests();
std::vector<patchy::test::TestCase> readme_screenshot_tests();
