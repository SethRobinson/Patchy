#include "core/guide_layout.hpp"

#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

std::int32_t to_32nds(double pixels) {
  return static_cast<std::int32_t>(std::lround(pixels * 32.0));
}

GuideLayoutAxis cells(int count, double gutter = 0.0) {
  return GuideLayoutAxis{count, gutter, 0.0, 0.0};
}

}  // namespace

std::vector<std::int32_t> guide_layout_positions_32(const GuideLayoutAxis& axis, int extent) {
  std::vector<std::int32_t> positions;
  if (extent <= 0) {
    return positions;
  }
  const double start = std::max(0.0, axis.margin_start);
  const double end = static_cast<double>(extent) - std::max(0.0, axis.margin_end);
  if (axis.margin_start > 0.0) {
    positions.push_back(to_32nds(start));
  }
  if (axis.margin_end > 0.0) {
    positions.push_back(to_32nds(end));
  }
  const double gutter = std::max(0.0, axis.gutter);
  const double content = end - start;
  if (axis.count >= 2 && content > 0.0) {
    const double cell = (content - gutter * static_cast<double>(axis.count - 1)) / static_cast<double>(axis.count);
    if (cell > 0.0) {
      for (int index = 1; index < axis.count; ++index) {
        const double cell_end = start + cell * static_cast<double>(index) + gutter * static_cast<double>(index - 1);
        positions.push_back(to_32nds(cell_end));
        if (gutter > 0.0) {
          positions.push_back(to_32nds(cell_end + gutter));
        }
      }
    }
  }
  std::sort(positions.begin(), positions.end());
  positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
  return positions;
}

const std::vector<GuideLayoutPreset>& guide_layout_presets() {
  static const std::vector<GuideLayoutPreset> presets{
      {"guideLayout.2columns", PATCHY_TRANSLATE_NOOP("QObject", "2 Columns"), {cells(2), cells(0)}},
      {"guideLayout.3columns", PATCHY_TRANSLATE_NOOP("QObject", "3 Columns"), {cells(3), cells(0)}},
      {"guideLayout.4columns", PATCHY_TRANSLATE_NOOP("QObject", "4 Columns"), {cells(4), cells(0)}},
      {"guideLayout.2x2", PATCHY_TRANSLATE_NOOP("QObject", "2 x 2 Grid"), {cells(2), cells(2)}},
      {"guideLayout.3x3", PATCHY_TRANSLATE_NOOP("QObject", "3 x 3 Grid"), {cells(3), cells(3)}},
      {"guideLayout.4x4", PATCHY_TRANSLATE_NOOP("QObject", "4 x 4 Grid"), {cells(4), cells(4)}},
      {"guideLayout.12columnsGutter",
       PATCHY_TRANSLATE_NOOP("QObject", "12 Columns, 20 px Gutter"),
       {cells(12, 20.0), cells(0)}},
  };
  return presets;
}

}  // namespace patchy
