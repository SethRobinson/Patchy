#pragma once

// The interpreter enforces this when registering commands, including commands
// defined by a formula's run(). Restricted bodies become error stubs, so $$name
// cannot recover their implementation under an alias. Keep the catalog exclusions
// and the underlying algorithm entry points covered (docs/legal-constraints-filters.md).
// C++17: this header is also included by the vendored interpreter.
#include <string_view>

namespace patchy::gmic {

[[nodiscard]] inline bool restricted_command(std::string_view name) {
  // G'MIC uses leading underscores for private helpers and + for specializations.
  while (!name.empty() && (name.front() == '_' || name.front() == '+')) {
    name.remove_prefix(1);
  }
  if (name == "denoise") return true;  // Built-in patch-based denoising; dispatch also refuses it.
  constexpr std::string_view roots[] = {
      "gui_download_all_data", "fx_gmicky", "fx_frame_seamless", "fx_shuffle_patches",
      "fx_rotate_tileable", "fx_linify", "fx_stylize", "fx_colorize_interactive",
      "fx_autofill_lineart", "fx_colorize_lineart", "fx_clut_from_ab", "fx_mask_color",
      "fx_curves_interactive", "fx_extract_foreground", "fx_morph_interactive",
      "fx_seamcarve", "fx_warp_interactive", "fx_simulate_grain", "fx_blur_dof",
      "fx_blur_motion", "fx_watermark_visible", "fx_blend_seamless", "fx_light_leaks",
      "syntexturize", "fx_syntexturize", "fx_equation_parametric", "fx_equation_plot", "fx_generate_random_portrait",
      "fx_quick_copyright", "fx_denoise", "fx_inpaint", "fx_smooth_patch", "fx_smooth_patchpca",
      "fx_scale2x_cnn", "fx_custom_code", "fx_output_565", "fx_gmic_demos", "fx_import_image",
      "fx_intarsia",
      // Lower-level paths must not bypass their excluded Filter Library wrappers.
      // Shared non-patch diffusion/morphology helpers remain available: offered
      // filters use them for color interpolation, gradients and hole filling.
      // Patch inpaint and matchpatch are separately compiled out.
      "blend_seamless", "seamcarve", "stylize", "frame_seamless", "x_segment", "x_colorize",
      "inpaint_matchpatch", "inpaint_warping2d",
      "denoise_patchpca", "denoise_cnn", "scale2x_cnn", "nn",
  };
  for (const auto root : roots) {
    if (name == root || (name.size() > root.size() && name.substr(0, root.size()) == root &&
                         name[root.size()] == '_')) {
      return true;
    }
  }
  return false;
}

}  // namespace patchy::gmic
