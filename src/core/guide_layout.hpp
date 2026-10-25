#pragma once

// New Guide Layout geometry (View > Guide Operations > New Guide Layout,
// docs/guides.md): the guide positions a column or row layout produces, and
// the dialog's built-in presets. Qt-free and deterministic.

#include <cstdint>
#include <string_view>
#include <vector>

namespace patchy {

// One axis of a layout: columns across the width or rows down the height.
// Lengths are document pixels.
struct GuideLayoutAxis {
  int count{0};               // 0 or 1 = no dividing guides
  double gutter{0.0};         // space between neighboring columns or rows
  double margin_start{0.0};   // left or top
  double margin_end{0.0};     // right or bottom

  friend bool operator==(const GuideLayoutAxis&, const GuideLayoutAxis&) = default;
};

struct GuideLayout {
  GuideLayoutAxis columns;
  GuideLayoutAxis rows;

  friend bool operator==(const GuideLayout&, const GuideLayout&) = default;
};

// Guide positions in 32nds of a pixel along an axis `extent` pixels long,
// sorted and without duplicates. A margin above zero puts a guide at its edge;
// the count divides the space between the margins into equal cells, with one
// guide per division when the gutter is zero and one on each side of the
// gutter otherwise. Cells that would not fit (the margins and gutters fill the
// extent) produce no dividing guides.
[[nodiscard]] std::vector<std::int32_t> guide_layout_positions_32(const GuideLayoutAxis& axis, int extent);

struct GuideLayoutPreset {
  std::string_view id;  // stable
  const char* name;     // English, PATCHY_TRANSLATE_NOOP("QObject", ...)
  GuideLayout layout;
};

[[nodiscard]] const std::vector<GuideLayoutPreset>& guide_layout_presets();

}  // namespace patchy
