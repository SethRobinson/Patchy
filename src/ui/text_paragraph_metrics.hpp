#pragma once

#include <optional>

namespace patchy::ui {

// Photoshop's Paragraph panel metrics in DOCUMENT pixels (the unit the paragraph runs and the
// TySh EngineData store). An unset field leaves the paragraph's current value alone.
struct TextParagraphMetrics {
  std::optional<double> first_line_indent;
  std::optional<double> start_indent;  // Photoshop's left indent
  std::optional<double> end_indent;    // Photoshop's right indent
  std::optional<double> space_before;
  std::optional<double> space_after;

  [[nodiscard]] bool empty() const noexcept {
    return !first_line_indent && !start_indent && !end_indent && !space_before && !space_after;
  }
};

}  // namespace patchy::ui
