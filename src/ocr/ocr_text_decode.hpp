#pragma once

// Reading one detected line with a recognizer network (docs/ocr-engines.md, "PP-OCR"): the
// picture of the line the network is given, and its output turned into text. No network
// runs here; src/ai runs it.
//
// Binding rules (the "Text recognition" entry of docs/legal-constraints-scanning.md): a line
// is cut out as a rectangle (a turn and a scale, never a perspective or free-form warp), and
// the recognizer is the one the user's language pick names, never chosen from the picture.

#include "ocr/ocr_engine.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ocr {

inline constexpr std::int32_t kRecognizerHeight = 48;
inline constexpr std::int32_t kRecognizerMinWidth = 160;
inline constexpr std::int32_t kRecognizerMaxWidth = 3200;
// A crop at least this many times taller than wide is a column of vertical text.
inline constexpr double kVerticalLineRatio = 1.5;

// The recognizer's input for one line.
struct LineInput {
  // The line as cut out, in pixels of the network's input: `text_width` columns hold the
  // line, the rest up to `tensor_width` is padding.
  std::int32_t text_width{0};
  std::int32_t tensor_width{0};
  // A column: the cut-out was turned a quarter turn counterclockwise (its top is at the
  // left), which is how the PP-OCR recognizers read vertical text.
  bool vertical{false};
  // 3 x kRecognizerHeight x tensor_width floats, channels first (blue, green, red), each
  // value / 127.5 - 1; the padding is 0.
  std::vector<float> values;
};

// Cuts `quad` (corners in `image` pixels: top left, top right, bottom right, bottom left)
// out of the image and scales it to the recognizer's height. Every output pixel is the mean
// of a fixed grid of bilinear samples of the image over white, the grid as fine as the
// reduction needs (1 to 4 a side), so large text is not aliased. Pixels outside the image
// repeat its edge.
[[nodiscard]] LineInput line_input(const ImageView& image, const Quad& quad);

// The character list of a PP-OCR recognizer's inference.yml (its `character_dict`), in
// order. The file is read as text for that one list; nothing else in it is interpreted.
// Throws std::runtime_error when the list is missing or empty.
[[nodiscard]] std::vector<std::string> character_list_from_yaml(std::string_view yaml);

struct DecodedCharacter {
  std::string text;      // UTF-8, one dictionary entry
  std::int32_t step{0};  // the output step it was read at
};

struct DecodedLine {
  std::string text;
  std::vector<DecodedCharacter> characters;
  double confidence{0.0};  // mean of the kept steps' highest values
};

// Greedy CTC decoding of `steps` x `classes` values: class 0 is the blank, classes 1 to N
// are `characters`, class N + 1 is the space. A class repeated on neighbouring steps counts
// once. Throws std::runtime_error when `classes` does not fit the list.
[[nodiscard]] DecodedLine decode_line(std::span<const float> values, std::int32_t steps, std::int32_t classes,
                                      const std::vector<std::string>& characters);

// The words of a decoded horizontal line with their place along `quad`, from the steps the
// characters were read at: a word runs from the middle of the gap before its first character
// to the middle of the gap after its last. Approximate by design (the network reports steps,
// not boxes). `text_fraction` is text_width / tensor_width of the input the line was read
// from. A line with no space is one word.
[[nodiscard]] std::vector<Word> words_from_decoded_line(const DecodedLine& line, std::int32_t steps,
                                                        double text_fraction, const Quad& quad);

// `words` (placed along a line's cut) moved onto `line`, a rectangle in the same frame:
// each word keeps where it starts and ends along the line, clamped to `line`'s ends, and
// takes `line`'s top and bottom edges. A word left with no length is dropped.
[[nodiscard]] std::vector<Word> words_on_line(const std::vector<Word>& words, const Quad& line);

}  // namespace patchy::ocr
