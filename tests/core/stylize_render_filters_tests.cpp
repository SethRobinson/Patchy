// The Stylize and Render filters added for GitHub issue 496
// (docs/stylize-render-filters.md): the direct commands that reuse a native Smart
// Filter kernel render what that Smart Filter renders and map onto it, and
// Patchy's own Wind, Tiles, Extrude, Difference Clouds, Lens Flare and Flame
// are deterministic with pinned output.

#include "core/smart_filter.hpp"
#include "core/smart_filter_schema.hpp"
#include "core/smart_filter_table.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_distort_stylize.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_table_render.hpp"
#include "core_test_support.hpp"
#include "test_harness.hpp"
#include "test_groups.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

// 31x23 RGBA: gradients, a noise block and soft alpha (a transparent corner,
// a half-transparent band), so edge, alpha and color paths all run.
patchy::PixelBuffer make_probe() {
  patchy::PixelBuffer pixels(31, 23, patchy::PixelFormat::rgba8());
  std::uint32_t state = 496U;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      state = state * 1103515245U + 12345U;
      const auto noise = static_cast<std::uint8_t>(state >> 24U);
      const auto in_noise = x >= 18 && y >= 10;
      px[0] = in_noise ? noise : static_cast<std::uint8_t>(x * 8);
      px[1] = in_noise ? static_cast<std::uint8_t>(noise ^ 0x5aU) : static_cast<std::uint8_t>(y * 11);
      px[2] = static_cast<std::uint8_t>(255 - x * 4 - y * 3);
      px[3] = (x < 4 && y < 4) ? 0 : (y >= 6 && y < 9 ? 128 : 255);
    }
  }
  return pixels;
}

patchy::FilterInvocation invocation_for(const patchy::FilterRegistry& registry, std::string_view id,
                                        const patchy::FilterParameterMap& overrides = {}) {
  auto invocation = registry.default_invocation(id, patchy::RgbColor{200, 40, 20},
                                                patchy::RgbColor{20, 30, 90});
  for (const auto& [key, value] : overrides) {
    invocation.parameters[key] = value;
  }
  return invocation;
}

patchy::PixelBuffer applied(const patchy::FilterRegistry& registry,
                            const patchy::FilterInvocation& invocation,
                            const patchy::PixelBuffer& source) {
  auto pixels = source;
  registry.apply(invocation, pixels);
  return pixels;
}

bool same_pixels(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  return a.width() == b.width() && a.height() == b.height() &&
         std::equal(a.data().begin(), a.data().end(), b.data().begin(), b.data().end());
}

// The pixels a single-entry recipe's mapped native Smart Filter renders over
// `source`, with the source as its own filter canvas.
patchy::PixelBuffer smart_filter_pixels(const patchy::FilterRegistry& registry,
                                        const patchy::FilterInvocation& invocation,
                                        const patchy::PixelBuffer& source,
                                        patchy::SmartFilterEntry* mapped_entry) {
  patchy::FilterRecipe recipe;
  recipe.entries.push_back({invocation});
  const auto mapped = patchy::smart_filter_entries_from_recipe(recipe, registry);
  CHECK(mapped.has_value() && mapped->size() == 1U);
  if (!mapped.has_value() || mapped->size() != 1U) {
    return {};
  }
  const auto& entry = mapped->front();
  *mapped_entry = entry;
  const auto canvas = patchy::Rect::from_size(source.width(), source.height());
  patchy::FilterRenderResult rendered;
  if (const auto* settings = std::get_if<patchy::SchemaSmartFilter>(&entry.parameters);
      settings != nullptr) {
    rendered = patchy::render_schema_smart_filter(patchy::FilterRenderResult{source, canvas}, canvas,
                                                  entry.kind, *settings);
  } else {
    rendered = patchy::render_table_smart_filter(patchy::FilterRenderResult{source, canvas}, entry);
  }
  // A trimmed result (Diffuse) goes back into the full canvas, transparent elsewhere.
  patchy::PixelBuffer full(canvas.width, canvas.height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < rendered.pixels.height(); ++y) {
    for (std::int32_t x = 0; x < rendered.pixels.width(); ++x) {
      const auto* px = rendered.pixels.pixel(x, y);
      std::copy(px, px + 4, full.pixel(rendered.bounds.x + x, rendered.bounds.y + y));
    }
  }
  return full;
}

void filter_issue_496_direct_filters_match_their_smart_filters() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_probe();
  struct Case {
    const char* id;
    patchy::FilterParameterMap overrides;
    patchy::SmartFilterKind kind;
    const char* native_name;
    bool has_options;
  };
  const std::vector<Case> cases{
      {"patchy.filters.solarize", {}, patchy::SmartFilterKind::Solarize, "Solarize", false},
      {"patchy.filters.find_edges", {}, patchy::SmartFilterKind::FindEdges, "Find Edges", false},
      {"patchy.filters.trace_contour", {}, patchy::SmartFilterKind::TraceContour, "Trace Contour...",
       true},
      {"patchy.filters.trace_contour",
       {{"level", std::int64_t{77}}, {"edge", std::string("upper")}},
       patchy::SmartFilterKind::TraceContour, "Trace Contour...", true},
      {"patchy.filters.diffuse", {}, patchy::SmartFilterKind::Diffuse, "Diffuse...", true},
      {"patchy.filters.diffuse", {{"mode", std::string("darken_only")}},
       patchy::SmartFilterKind::Diffuse, "Diffuse...", true},
      {"patchy.filters.diffuse", {{"mode", std::string("lighten_only")}},
       patchy::SmartFilterKind::Diffuse, "Diffuse...", true},
      {"patchy.filters.fibers", {}, patchy::SmartFilterKind::Fibers, "Fibers...", true},
      {"patchy.filters.fibers", {{"variance", std::int64_t{40}}, {"strength", std::int64_t{9}}},
       patchy::SmartFilterKind::Fibers, "Fibers...", true},
  };
  for (const auto& test_case : cases) {
    const auto invocation = invocation_for(registry, test_case.id, test_case.overrides);
    CHECK(patchy::native_smart_filter_kind_for(test_case.id) == test_case.kind);
    CHECK(patchy::native_smart_filter_kind_supported(test_case.kind));
    const auto direct = applied(registry, invocation, source);
    patchy::SmartFilterEntry entry;
    const auto native = smart_filter_pixels(registry, invocation, source, &entry);
    CHECK(entry.kind == test_case.kind);
    CHECK(entry.native_name == test_case.native_name);
    CHECK(entry.has_options == test_case.has_options);
    CHECK(entry.foreground == (patchy::RgbColor{200, 40, 20}));
    CHECK(entry.background == (patchy::RgbColor{20, 30, 90}));
    CHECK(same_pixels(direct, native));
    CHECK(!same_pixels(direct, source));
  }

  // The settings carry the invocation's values in the native field order.
  patchy::FilterRecipe recipe;
  recipe.entries.push_back({invocation_for(registry, "patchy.filters.trace_contour",
                                           {{"level", std::int64_t{77}}, {"edge", std::string("upper")}})});
  recipe.entries.push_back({invocation_for(registry, "patchy.filters.fibers",
                                           {{"variance", std::int64_t{40}}, {"strength", std::int64_t{9}}})});
  const auto mapped = patchy::smart_filter_entries_from_recipe(recipe, registry);
  CHECK(mapped.has_value() && mapped->size() == 2U);
  if (mapped.has_value() && mapped->size() == 2U) {
    const auto* trace = std::get_if<patchy::SchemaSmartFilter>(&(*mapped)[0].parameters);
    CHECK(trace != nullptr && trace->values == (std::vector<std::int32_t>{77, 1}));
    CHECK((*mapped)[0].native_class_id == "TrcC");
    const auto* fibers = std::get_if<patchy::TableSmartFilter>(&(*mapped)[1].parameters);
    CHECK(fibers != nullptr && fibers->values.size() == 2U);
    if (fibers != nullptr && fibers->values.size() == 2U) {
      CHECK(std::get<std::int32_t>(fibers->values[0]) == 40);
      CHECK(std::get<std::int32_t>(fibers->values[1]) == 9);
    }
    CHECK((*mapped)[1].native_class_id == "Fbrs");
  }
  // Patchy's own filters have no Photoshop Smart Filter counterpart.
  for (const auto* id : {"patchy.filters.wind", "patchy.filters.tiles", "patchy.filters.extrude",
                         "patchy.filters.difference_clouds", "patchy.filters.lens_flare",
                         "patchy.filters.flame"}) {
    CHECK(!patchy::native_smart_filter_kind_for(id).has_value());
  }
}

void filter_issue_496_difference_clouds_share_the_clouds_pixels() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const patchy::FilterParameterMap settings{{"scale", std::int64_t{24}}, {"seed", std::int64_t{7}}};
  const auto clouds_invocation = invocation_for(registry, "patchy.filters.clouds", settings);
  const auto difference_invocation = invocation_for(registry, "patchy.filters.difference_clouds", settings);
  // Over opaque black and over transparency Difference Clouds is Clouds.
  const auto black = patchy::test::solid_rgba(40, 30, 0, 0, 0, 255);
  const auto clear = patchy::test::solid_rgba(40, 30, 90, 90, 90, 0);
  const auto clouds = applied(registry, clouds_invocation, black);
  CHECK(same_pixels(applied(registry, difference_invocation, black), clouds));
  CHECK(same_pixels(applied(registry, difference_invocation, clear), clouds));
  // Over opaque white it is the inverted clouds.
  const auto white = applied(registry, difference_invocation, patchy::test::solid_rgba(40, 30, 255, 255, 255, 255));
  for (std::int32_t y = 0; y < white.height(); ++y) {
    for (std::int32_t x = 0; x < white.width(); ++x) {
      for (int channel = 0; channel < 3; ++channel) {
        CHECK(white.pixel(x, y)[channel] == 255 - clouds.pixel(x, y)[channel]);
      }
      CHECK(white.pixel(x, y)[3] == 255);
    }
  }
}

void filter_issue_496_own_filters_shape_their_output() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  // Wind: a white dot on black streaks downwind only; Blast streaks every row.
  auto dot = patchy::test::solid_rgba(40, 12, 0, 0, 0, 255);
  for (std::int32_t y = 0; y < 12; ++y) {
    auto* px = dot.pixel(20, y);
    px[0] = px[1] = px[2] = 255;
  }
  const auto from_right = applied(registry, invocation_for(registry, "patchy.filters.wind",
                                                           {{"method", std::string("blast")}}),
                                  dot);
  const auto from_left = applied(registry,
                                 invocation_for(registry, "patchy.filters.wind",
                                                {{"method", std::string("blast")},
                                                 {"direction", std::string("from_left")}}),
                                 dot);
  for (std::int32_t y = 0; y < 12; ++y) {
    CHECK(from_right.pixel(19, y)[0] > 0 && from_right.pixel(21, y)[0] == 0);
    CHECK(from_left.pixel(21, y)[0] > 0 && from_left.pixel(19, y)[0] == 0);
    CHECK(from_right.pixel(19, y)[0] > from_right.pixel(16, y)[0]);
  }

  // Tiles: with transparent gaps, alpha appears only where tiles moved apart.
  const auto opaque = patchy::test::solid_rgba(60, 40, 120, 160, 200, 255);
  const auto tiles = applied(registry,
                             invocation_for(registry, "patchy.filters.tiles",
                                            {{"fill", std::string("transparent")},
                                             {"max_offset", std::int64_t{40}}}),
                             opaque);
  std::size_t gaps = 0;
  for (std::int32_t y = 0; y < tiles.height(); ++y) {
    for (std::int32_t x = 0; x < tiles.width(); ++x) {
      const auto* px = tiles.pixel(x, y);
      CHECK(px[3] == 0 || (px[0] == 120 && px[1] == 160 && px[2] == 200 && px[3] == 255));
      gaps += px[3] == 0 ? 1U : 0U;
    }
  }
  CHECK(gaps > 0U && gaps < 60U * 40U / 2U);

  // Lens Flare on an empty layer: the light itself, with alpha, fading away
  // from the flare.
  const auto empty = patchy::test::solid_rgba(80, 60, 0, 0, 0, 0);
  const auto flare = applied(registry, invocation_for(registry, "patchy.filters.lens_flare"), empty);
  const auto* at_flare = flare.pixel(24, 18);  // 30 % of 79 and 59
  CHECK(at_flare[3] >= 250 && at_flare[0] >= 240);
  CHECK(flare.pixel(79, 0)[3] < at_flare[3]);
  // Over an opaque layer the flare only brightens (Screen).
  const auto gray = patchy::test::solid_rgba(80, 60, 90, 90, 90, 255);
  const auto lit = applied(registry, invocation_for(registry, "patchy.filters.lens_flare"), gray);
  for (std::int32_t y = 0; y < lit.height(); ++y) {
    for (std::int32_t x = 0; x < lit.width(); ++x) {
      const auto* px = lit.pixel(x, y);
      CHECK(px[0] >= 90 && px[1] >= 90 && px[2] >= 90 && px[3] == 255);
    }
  }

  // Flame on an empty layer: flames at the bottom, nothing at the top.
  const auto flame = applied(registry, invocation_for(registry, "patchy.filters.flame"), empty);
  std::size_t bottom = 0;
  std::size_t top = 0;
  for (std::int32_t x = 0; x < flame.width(); ++x) {
    bottom += flame.pixel(x, 57)[3] > 0 ? 1U : 0U;
    top += flame.pixel(x, 2)[3] > 0 ? 1U : 0U;
  }
  CHECK(bottom > 20U);
  CHECK(top == 0U);

  // Extrude keeps an opaque layer opaque and changes it.
  const auto probe = make_probe();
  auto opaque_probe = probe;
  for (std::int32_t y = 0; y < opaque_probe.height(); ++y) {
    for (std::int32_t x = 0; x < opaque_probe.width(); ++x) {
      opaque_probe.pixel(x, y)[3] = 255;
    }
  }
  const auto extruded = applied(registry,
                                invocation_for(registry, "patchy.filters.extrude",
                                               {{"size", std::int64_t{6}}, {"depth", std::int64_t{8}}}),
                                opaque_probe);
  CHECK(!same_pixels(extruded, opaque_probe));
  for (std::int32_t y = 0; y < extruded.height(); ++y) {
    for (std::int32_t x = 0; x < extruded.width(); ++x) {
      CHECK(extruded.pixel(x, y)[3] == 255);
    }
  }
}

void filter_issue_496_outputs_are_deterministic_and_pinned() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_probe();
  struct Pin {
    const char* id;
    patchy::FilterParameterMap overrides;
    std::uint64_t digest;
  };
  // FNV-1a of the RGBA bytes over make_probe(). The kernels use integer math
  // or + - * / and sqrt only, so these hold on every toolchain.
  const std::vector<Pin> pins{
      {"patchy.filters.wind", {}, 0xb97e9276aa82e46dULL},
      {"patchy.filters.wind", {{"method", std::string("blast")}, {"direction", std::string("from_left")}},
       0x69e8be4e845d83dfULL},
      {"patchy.filters.wind", {{"method", std::string("stagger")}, {"length", std::int64_t{5}}}, 0xfd63e7df92879683ULL},
      {"patchy.filters.tiles", {{"count", std::int64_t{4}}, {"max_offset", std::int64_t{30}}}, 0x7751121264dbf45bULL},
      {"patchy.filters.tiles",
       {{"count", std::int64_t{3}},
        {"max_offset", std::int64_t{45}},
        {"fill", std::string("inverse")},
        {"seed", std::int64_t{9}}},
       0xca5acbf8f1980feaULL},
      {"patchy.filters.extrude", {{"size", std::int64_t{5}}, {"depth", std::int64_t{9}}}, 0xad3254adb944d335ULL},
      {"patchy.filters.extrude",
       {{"type", std::string("pyramids")},
        {"size", std::int64_t{6}},
        {"heights", std::string("level_based")}},
       0xd6322aa2945ced34ULL},
      {"patchy.filters.extrude", {{"size", std::int64_t{4}}, {"solid_front", true}}, 0xa4b058ef5bfd1fb3ULL},
      {"patchy.filters.difference_clouds", {{"scale", std::int64_t{16}}}, 0x0a9df03d93897175ULL},
      {"patchy.filters.lens_flare", {}, 0x443538c6aec4f873ULL},
      {"patchy.filters.lens_flare",
       {{"lens", std::string("star")}, {"center_x", 70.0}, {"brightness", std::int64_t{150}}}, 0x86ed0e4a49c387c3ULL},
      {"patchy.filters.lens_flare", {{"lens", std::string("ring")}}, 0x37e60b34269eccd3ULL},
      {"patchy.filters.lens_flare", {{"lens", std::string("anamorphic")}, {"center_y", 50.0}}, 0xa2e0558fcfaf26d1ULL},
      {"patchy.filters.flame", {}, 0x7b92624c69bb9547ULL},
      {"patchy.filters.flame", {{"colors", std::string("blue")}, {"turbulence", std::int64_t{90}}}, 0x7fb52edda7bc800aULL},
      {"patchy.filters.flame",
       {{"colors", std::string("colors")}, {"length", std::int64_t{80}}, {"center_x", 30.0}}, 0xad1f34f02637b612ULL},
  };
  std::size_t mismatches = 0;
  for (std::size_t index = 0; index < pins.size(); ++index) {
    const auto& pin = pins[index];
    const auto invocation = invocation_for(registry, pin.id, pin.overrides);
    const auto first = applied(registry, invocation, source);
    const auto second = applied(registry, invocation, source);
    CHECK(same_pixels(first, second));
    CHECK(!same_pixels(first, source));
    const auto digest = patchy::test::fnv1a_hash_bytes(first.data());
    if (digest != pin.digest) {
      // Every mismatch is listed before the check fails.
      std::printf("[PIN] %zu %s 0x%016llxULL\n", index, pin.id,
                  static_cast<unsigned long long>(digest));
      ++mismatches;
    }
  }
  CHECK(mismatches == 0U);
  // The compatibility path runs the catalog defaults.
  for (const auto* id : {"patchy.filters.wind", "patchy.filters.tiles", "patchy.filters.extrude",
                         "patchy.filters.lens_flare", "patchy.filters.flame", "patchy.filters.solarize",
                         "patchy.filters.find_edges", "patchy.filters.diffuse", "patchy.filters.trace_contour"}) {
    auto legacy = source;
    registry.apply(id, legacy);
    CHECK(same_pixels(legacy, applied(registry, registry.default_invocation(id), source)));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> stylize_render_filters_tests() {
  return {
      {"filter_issue_496_direct_filters_match_their_smart_filters",
       filter_issue_496_direct_filters_match_their_smart_filters},
      {"filter_issue_496_difference_clouds_share_the_clouds_pixels",
       filter_issue_496_difference_clouds_share_the_clouds_pixels},
      {"filter_issue_496_own_filters_shape_their_output", filter_issue_496_own_filters_shape_their_output},
      {"filter_issue_496_outputs_are_deterministic_and_pinned",
       filter_issue_496_outputs_are_deterministic_and_pinned},
  };
}
