#include "filters/builtin_filter_looks.hpp"

#include "support/translate_noop.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace patchy {

namespace {

using Params = FilterParameterMap;

FilterRecipeEntry step(std::string_view filter, Params parameters = {},
                       double opacity = 1.0,
                       BlendMode blend_mode = BlendMode::Normal) {
  FilterInvocation invocation;
  invocation.filter_id = std::string("patchy.filters.") + std::string(filter);
  invocation.parameters = std::move(parameters);
  return FilterRecipeEntry{std::move(invocation), true, opacity, blend_mode};
}

FilterParameterValue integer(std::int64_t value) { return value; }
FilterParameterValue real(double value) { return value; }

// The tuning reference for every Look: a photo about 4000 pixels on its longer
// side (12-16 MP). Pixel distances below are at that size. Ids are persisted;
// names and descriptions are translation sources.
std::vector<BuiltinFilterLook> make_builtin_filter_looks() {
  std::vector<BuiltinFilterLook> looks;
  const auto add = [&looks](std::string_view id, const char* name,
                            const char* description,
                            std::vector<FilterRecipeEntry> entries) {
    looks.push_back(
        BuiltinFilterLook{id, name, description, FilterRecipe{std::move(entries)}});
  };

  // Photo grades.
  add("patchy.looks.warm_film", PATCHY_TRANSLATE_NOOP("QObject", "Warm Film"),
      PATCHY_TRANSLATE_NOOP("QObject", "Golden tones, softened contrast and fine grain."),
      {step("sepia", {}, 0.4, BlendMode::Overlay),
       step("brightness_contrast", {{"brightness", integer(0)}, {"contrast", integer(-8)}}),
       step("film_grain", {{"amount", integer(25)}})});
  add("patchy.looks.cool_cinema", PATCHY_TRANSLATE_NOOP("QObject", "Cool Cinema"),
      PATCHY_TRANSLATE_NOOP("QObject", "Blue-teal grade with deeper contrast and a light vignette."),
      {step("invert"),
       step("sepia", {}, 0.4, BlendMode::Overlay),
       step("invert"),
       step("brightness_contrast", {{"brightness", integer(-4)}, {"contrast", integer(14)}}),
       step("vignette", {{"strength", integer(30)}})});
  add("patchy.looks.faded_matte", PATCHY_TRANSLATE_NOOP("QObject", "Faded Matte"),
      PATCHY_TRANSLATE_NOOP("QObject", "Lifted blacks and muted color for a soft, flat print."),
      {step("brightness_contrast", {{"brightness", integer(10)}, {"contrast", integer(-28)}}),
       step("desaturate", {{"amount", integer(25)}}),
       step("film_grain", {{"amount", integer(20)}})});
  add("patchy.looks.bold_black_and_white",
      PATCHY_TRANSLATE_NOOP("QObject", "Bold Black and White"),
      PATCHY_TRANSLATE_NOOP("QObject", "High-contrast monochrome with punchy local detail."),
      {step("grayscale"),
       step("brightness_contrast", {{"contrast", integer(35)}}),
       step("high_pass", {{"radius", real(40.0)}}, 0.5, BlendMode::SoftLight),
       step("vignette", {{"strength", integer(30)}})});
  add("patchy.looks.sepia_print", PATCHY_TRANSLATE_NOOP("QObject", "Sepia Print"),
      PATCHY_TRANSLATE_NOOP("QObject", "Warm brown monochrome with grain and darkened corners."),
      {step("grayscale"),
       step("sepia"),
       step("brightness_contrast", {{"brightness", integer(4)}, {"contrast", integer(-8)}}),
       step("film_grain", {{"amount", integer(25)}}),
       step("vignette", {{"strength", integer(35)}})});
  add("patchy.looks.cross_process", PATCHY_TRANSLATE_NOOP("QObject", "Cross Process"),
      PATCHY_TRANSLATE_NOOP("QObject", "Shifted, saturated colors with yellow highlights and cool shadows."),
      {step("invert"),
       step("sepia", {}, 0.35, BlendMode::Color),
       step("invert"),
       step("sepia", {}, 0.6, BlendMode::SoftLight),
       step("punchy_color", {{"amount", integer(70)}})});
  add("patchy.looks.silver_contrast", PATCHY_TRANSLATE_NOOP("QObject", "Silver Contrast"),
      PATCHY_TRANSLATE_NOOP("QObject", "Gritty, silvery contrast with low saturation."),
      {step("desaturate", {}, 0.85, BlendMode::Overlay),
       step("desaturate", {{"amount", integer(35)}})});
  add("patchy.looks.vintage_print", PATCHY_TRANSLATE_NOOP("QObject", "Vintage Print"),
      PATCHY_TRANSLATE_NOOP("QObject", "An aged, faded photo with a warm cast and vignette."),
      {step("vintage_fade", {{"amount", integer(80)}}),
       step("vignette", {{"strength", integer(40)}})});
  add("patchy.looks.cyanotype", PATCHY_TRANSLATE_NOOP("QObject", "Cyanotype"),
      PATCHY_TRANSLATE_NOOP("QObject", "Deep blue monochrome in the style of an early photographic print."),
      {step("invert"),
       step("sepia"),
       step("invert"),
       step("brightness_contrast", {{"contrast", integer(10)}}),
       step("film_grain", {{"amount", integer(20)}})});

  // Clarity and portrait helpers.
  add("patchy.looks.dreamy_glow", PATCHY_TRANSLATE_NOOP("QObject", "Dreamy Glow"),
      PATCHY_TRANSLATE_NOOP("QObject", "A soft halo around highlights for portraits and backlit scenes."),
      {step("gaussian_blur", {{"radius", real(30.0)}}, 0.35, BlendMode::Screen),
       step("brightness_contrast", {{"brightness", integer(-6)}, {"contrast", integer(8)}})});
  add("patchy.looks.crisp_detail", PATCHY_TRANSLATE_NOOP("QObject", "Crisp Detail"),
      PATCHY_TRANSLATE_NOOP("QObject", "Adds local contrast and fine sharpening without changing color."),
      {step("high_pass", {{"radius", real(30.0)}}, 0.6, BlendMode::SoftLight),
       step("unsharp_mask", {{"amount", integer(80)}, {"radius", real(1.5)}, {"threshold", integer(3)}})});

  // Creative looks.
  add("patchy.looks.watercolor", PATCHY_TRANSLATE_NOOP("QObject", "Watercolor"),
      PATCHY_TRANSLATE_NOOP("QObject", "Soft washes of color with darker pigment edges on light paper."),
      {step("median", {{"radius", real(3.0)}}),
       step("lens_blur", {{"radius", real(10.0)}, {"blades", integer(8)}, {"blade_curvature", integer(100)}}),
       step("posterize", {{"levels", integer(7)}}, 0.7),
       step("glowing_edges", {{"edge_width", integer(5)}, {"brightness", integer(160)}, {"smoothness", integer(6)}},
            0.45, BlendMode::Subtract),
       step("brightness_contrast", {{"brightness", integer(22)}, {"contrast", integer(-20)}}),
       step("add_noise", {{"amount", real(4.0)}, {"distribution", std::string("gaussian")},
                          {"monochromatic", true}})});
  add("patchy.looks.ink_sketch", PATCHY_TRANSLATE_NOOP("QObject", "Ink Sketch"),
      PATCHY_TRANSLATE_NOOP("QObject", "Clean black ink lines on white."),
      {step("grayscale"),
       step("gaussian_blur", {{"radius", real(8.0)}}, 1.0, BlendMode::Divide),
       step("threshold", {{"threshold", integer(232)}})});
  add("patchy.looks.pencil_drawing", PATCHY_TRANSLATE_NOOP("QObject", "Pencil Drawing"),
      PATCHY_TRANSLATE_NOOP("QObject", "Graphite strokes and shading on white paper."),
      {step("grayscale"),
       step("gaussian_blur", {{"radius", real(20.0)}}, 1.0, BlendMode::Divide),
       step("brightness_contrast", {{"brightness", integer(-25)}, {"contrast", integer(35)}})});
  add("patchy.looks.comic", PATCHY_TRANSLATE_NOOP("QObject", "Comic"),
      PATCHY_TRANSLATE_NOOP("QObject", "Bold outlines and flat, vivid color areas."),
      {step("median", {{"radius", real(3.0)}}),
       step("punchy_color", {{"amount", integer(60)}}),
       step("posterize", {{"levels", integer(6)}}, 0.6),
       step("glowing_edges", {{"edge_width", integer(4)}, {"brightness", integer(200)}, {"smoothness", integer(3)}},
            0.75, BlendMode::Subtract)});
  add("patchy.looks.oil_paint", PATCHY_TRANSLATE_NOOP("QObject", "Oil Paint"),
      PATCHY_TRANSLATE_NOOP("QObject", "Smoothed color with visible diagonal brush texture."),
      {step("median", {{"radius", real(4.0)}}),
       step("add_noise", {{"amount", real(30.0)}, {"monochromatic", true}}),
       step("motion_blur", {{"angle", integer(45)}, {"distance", integer(14)}}),
       step("emboss", {{"angle", integer(135)}, {"height", integer(3)}, {"amount", integer(200)}}, 0.45,
            BlendMode::Overlay),
       step("punchy_color", {{"amount", integer(50)}})});

  // Texture looks.
  add("patchy.looks.halftone_print", PATCHY_TRANSLATE_NOOP("QObject", "Halftone Print"),
      PATCHY_TRANSLATE_NOOP("QObject", "Bright color printed as large halftone dots."),
      {step("punchy_color", {{"amount", integer(35)}}),
       step("color_halftone", {{"cell_size", integer(28)}, {"intensity", integer(75)}, {"contrast", integer(60)}})});
  add("patchy.looks.film_grain", PATCHY_TRANSLATE_NOOP("QObject", "Film Grain"),
      PATCHY_TRANSLATE_NOOP("QObject", "Even monochrome grain with a touch of contrast."),
      {step("brightness_contrast", {{"contrast", integer(8)}}),
       step("desaturate", {{"amount", integer(12)}}),
       step("add_noise", {{"amount", real(8.0)}, {"distribution", std::string("gaussian")},
                          {"monochromatic", true}})});
  add("patchy.looks.toy_camera", PATCHY_TRANSLATE_NOOP("QObject", "Toy Camera"),
      PATCHY_TRANSLATE_NOOP("QObject", "Saturated color and heavy dark corners, like a plastic-lens camera."),
      {step("punchy_color"),
       step("sepia", {}, 0.25, BlendMode::SoftLight),
       step("vignette", {{"strength", integer(85)}})});
  add("patchy.looks.miniature", PATCHY_TRANSLATE_NOOP("QObject", "Miniature"),
      PATCHY_TRANSLATE_NOOP("QObject", "A narrow band of focus and bright color that makes scenes look like models."),
      {step("tilt_shift_blur",
            {{"blur", real(30.0)}, {"focus_half_width", real(8.0)}, {"transition_width", real(18.0)}}),
       step("punchy_color", {{"amount", integer(70)}})});

  // Store complete invocations (every catalog parameter present) so the
  // gallery shows the same values the Look renders with.
  FilterRegistry registry;
  register_builtin_filters(registry);
  for (auto& look : looks) {
    for (auto& entry : look.recipe.entries) {
      if (auto normalized = registry.normalize(entry.invocation); normalized.has_value()) {
        entry.invocation = std::move(*normalized);
      }
    }
  }
  return looks;
}

}  // namespace

const std::vector<BuiltinFilterLook>& builtin_filter_looks() {
  static const std::vector<BuiltinFilterLook> looks = make_builtin_filter_looks();
  return looks;
}

const BuiltinFilterLook* find_builtin_filter_look(std::string_view id) {
  const auto& looks = builtin_filter_looks();
  const auto found = std::find_if(looks.begin(), looks.end(),
                                  [id](const BuiltinFilterLook& look) { return look.id == id; });
  return found == looks.end() ? nullptr : &*found;
}

std::optional<FilterRecipe> builtin_look_recipe_for_size(const FilterRegistry& registry,
                                                         const BuiltinFilterLook& look,
                                                         std::int32_t width,
                                                         std::int32_t height) {
  const auto long_edge = std::max(width, height);
  if (long_edge <= 0) {
    return std::nullopt;
  }
  return registry.scale(look.recipe, static_cast<double>(long_edge) /
                                         static_cast<double>(kBuiltinLookReferenceLongEdge));
}

}  // namespace patchy
