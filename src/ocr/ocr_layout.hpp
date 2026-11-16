#pragma once

// Blocks of text from recognized lines (docs/ocr.md, "Blocks"). A block is a run of
// neighbouring lines that sit close together: what becomes one multi-line text layer.
//
// Binding rule (the "Text recognition" entry of docs/legal-constraints-scanning.md): the
// only inputs are the boxes and character counts of two lines at a time. No line is
// classified as starting or continuing a paragraph, and nothing about the page (its type,
// margins, indentation or layout) enters. Two paragraphs with no gap between them are one
// block; that is the stated limit of this grouping.

#include "ocr/ocr_engine.hpp"

#include <cstddef>
#include <vector>

namespace patchy::ocr {

// Where a block's lines line up along their reading direction: Start is left for
// horizontal text and top for vertical text.
enum class Alignment { Start, Center, End };

struct Block {
  // Indices into Result::lines, in reading order: top to bottom for horizontal text,
  // right to left for vertical text.
  std::vector<std::size_t> lines;
  Box box;
  bool vertical{false};
  Alignment alignment{Alignment::Start};
  // Median line thickness: height of a horizontal line, width of a vertical one.
  double line_size{0.0};
  // Median distance between the centres of consecutive lines; 0 for a single line.
  double line_pitch{0.0};
};

// Two lines of the same orientation join when they are the same size, their centres are at
// most 2.2 thicknesses of the thicker one apart across the lines (and at least half the
// thinner one: not the same row twice), and along the lines they overlap by at least half
// the shorter one. Rotated lines (more than 2 degrees) stay alone.
// "The same size" is their lengths per character within 30% of each other (with their
// thicknesses within a factor of 2.2), or, for a line of fewer than four characters, their
// thicknesses within 30%. An engine's line box hugs the ink, so thickness alone misjudges a
// line with no descenders, and the empty gap between two lines swings with the letters they
// hold: length per character and centre distance do not. Blocks come back in
// reading order: horizontal blocks top to bottom then left to right, vertical blocks right
// to left then top to bottom, horizontal before vertical.
[[nodiscard]] std::vector<Block> group_lines_into_blocks(const Result& result);

// Every line as its own block, in the same reading order: the "one layer per line" choice.
[[nodiscard]] std::vector<Block> lines_as_blocks(const Result& result);

}  // namespace patchy::ocr
