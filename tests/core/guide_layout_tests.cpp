// New Guide Layout geometry and presets (src/core/guide_layout.hpp; docs/guides.md).

#include "core/guide_layout.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <set>
#include <string>
#include <vector>

namespace {

using patchy::GuideLayoutAxis;

std::vector<std::int32_t> px(std::initializer_list<double> pixels) {
  std::vector<std::int32_t> out;
  for (const auto value : pixels) {
    out.push_back(static_cast<std::int32_t>(value * 32.0));
  }
  return out;
}

void guide_layout_even_cells_without_gutters_or_margins() {
  // The original behavior: interior divisions only, no edge guides.
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{3, 0, 0, 0}, 300) == px({100, 200}));
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{2, 0, 0, 0}, 301) == px({150.5}));
  // 1000 / 3 = 333.33 px, rounded to the nearest 32nd.
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{3, 0, 0, 0}, 1000) ==
        (std::vector<std::int32_t>{10667, 21333}));
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{0, 0, 0, 0}, 300).empty());
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{1, 0, 0, 0}, 300).empty());
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{3, 0, 0, 0}, 0).empty());
}

void guide_layout_gutters_and_margins_place_both_edges() {
  // 400 px, margins 20 / 40, 3 columns, 10 px gutters: content 20..360 (340 px),
  // cells (340 - 20) / 3 = 106.67 px.
  const auto positions = patchy::guide_layout_positions_32(GuideLayoutAxis{3, 10, 20, 40}, 400);
  CHECK(positions == (std::vector<std::int32_t>{20 * 32, 4053, 4373, 7787, 8107, 360 * 32}));
  // Gutter only: guides on both sides of each gutter, none at the edges.
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{2, 20, 0, 0}, 220) == px({100, 120}));
  // Margins only: the margin guides plus the even divisions between them.
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{2, 0, 50, 50}, 300) == px({50, 150, 250}));
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{0, 0, 10, 0}, 300) == px({10}));
  // Gutters that fill the space leave only the margin guides.
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{3, 200, 10, 10}, 300) == px({10, 290}));
  // Margins that meet: one guide, no divisions.
  CHECK(patchy::guide_layout_positions_32(GuideLayoutAxis{4, 0, 150, 150}, 300) == px({150}));
}

void guide_layout_presets_have_stable_ids_and_values() {
  const auto& presets = patchy::guide_layout_presets();
  CHECK(presets.size() == 7);
  std::set<std::string> ids;
  for (const auto& preset : presets) {
    CHECK(ids.insert(std::string(preset.id)).second);
    CHECK(preset.name != nullptr && std::string(preset.name).size() > 0);
  }
  const auto find = [&](std::string_view id) -> const patchy::GuideLayoutPreset* {
    for (const auto& preset : presets) {
      if (preset.id == id) {
        return &preset;
      }
    }
    return nullptr;
  };
  const auto* three = find("guideLayout.3x3");
  CHECK(three != nullptr);
  CHECK(three->layout.columns == (GuideLayoutAxis{3, 0, 0, 0}));
  CHECK(three->layout.rows == (GuideLayoutAxis{3, 0, 0, 0}));
  CHECK(patchy::guide_layout_positions_32(three->layout.columns, 900) == px({300, 600}));
  const auto* twelve = find("guideLayout.12columnsGutter");
  CHECK(twelve != nullptr);
  CHECK(twelve->layout.columns == (GuideLayoutAxis{12, 20, 0, 0}));
  CHECK(twelve->layout.rows.count == 0);
  // 1200 px: cells (1200 - 220) / 12 = 81.67 px, 22 guides.
  CHECK(patchy::guide_layout_positions_32(twelve->layout.columns, 1200).size() == 22);
  CHECK(find("guideLayout.2columns") != nullptr && find("guideLayout.2x2") != nullptr);
}

}  // namespace

std::vector<patchy::test::TestCase> guide_layout_tests() {
  return {
      {"guide_layout_even_cells_without_gutters_or_margins", guide_layout_even_cells_without_gutters_or_margins},
      {"guide_layout_gutters_and_margins_place_both_edges", guide_layout_gutters_and_margins_place_both_edges},
      {"guide_layout_presets_have_stable_ids_and_values", guide_layout_presets_have_stable_ids_and_values},
  };
}
