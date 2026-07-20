#pragma once

// Public face of the PSD text-run model: the per-run style/paragraph structs
// and the serializers that produce the patchy.text.runs / patchy.text.paragraph.runs
// metadata (persistence contracts, v1-v3) plus the Qt rich-text body for
// patchy.text.html. Split out of psd_io_internal.hpp so non-PSD importers
// (the Affinity .af reader) can emit the same editable-text metadata;
// definitions live in psd_text_read.cpp.

#include "core/layer.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace patchy::psd {

struct PsdTextStyleRun {
  int start{0};
  int length{0};
  std::string family{"Arial"};
  double size{36.0};
  RgbColor color{0, 0, 0};
  bool bold{false};
  bool italic{false};
  // Fixed leading in engine units (document pixels through the TySh transform). Unset when the
  // run uses Photoshop auto leading (auto_leading), which is paragraph AutoLeading fraction x size.
  std::optional<double> leading;
  bool auto_leading{false};
  // Photoshop tracking: 1/1000 em added after every inter-glyph gap.
  double tracking{0.0};
  // Character-panel glyph scales: width x horizontal_scale, height x vertical_scale. Leading
  // stays FontSize-based (COM-calibrated: VerticalScale does not change auto leading).
  double horizontal_scale{1.0};
  double vertical_scale{1.0};
};

struct PsdTextParagraphRun {
  int start{0};
  int length{0};
  int justification{0};
  double first_line_indent{0.0};
  double start_indent{0.0};
  double end_indent{0.0};
  double space_before{0.0};
  double space_after{0.0};
  // Auto-leading fraction (Photoshop default 1.2): auto leading = fraction x font size.
  double auto_leading_fraction{1.2};
};

std::string serialize_patchy_text_runs(std::span<const PsdTextStyleRun> runs);
std::string serialize_patchy_paragraph_runs(std::span<const PsdTextParagraphRun> runs);
std::string html_from_text_runs(std::string_view text, std::span<const PsdTextStyleRun> runs,
                                std::span<const PsdTextParagraphRun> paragraph_runs = {});

}  // namespace patchy::psd
