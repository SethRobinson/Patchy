#include "brush_pattern_palette_test_support.hpp"

#include <cstring>

namespace patchy::test::ui {

QImage make_bar_tip_image() {
  // 16x16 coverage mask with an opaque horizontal bar through the middle (rows 6-9).
  QImage mask(16, 16, QImage::Format_Grayscale8);
  mask.fill(0);
  for (int y = 6; y <= 9; ++y) {
    auto* row = mask.scanLine(y);
    std::memset(row, 255, 16);
  }
  return mask;
}

}  // namespace patchy::test::ui
