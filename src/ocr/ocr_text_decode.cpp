#include "ocr/ocr_text_decode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace patchy::ocr {

namespace {

// One source pixel as blue, green, red over white.
std::array<float, 3> bgr_over_white(const ImageView& image, std::int32_t x, std::int32_t y) noexcept {
  const std::uint8_t* pixel = image.data + static_cast<std::ptrdiff_t>(y) * image.stride +
                              static_cast<std::ptrdiff_t>(x) * channel_count(image.layout);
  const auto over_white = [](std::uint8_t value, std::uint8_t alpha) {
    return (static_cast<float>(value) * static_cast<float>(alpha) + 255.0F * static_cast<float>(255 - alpha)) / 255.0F;
  };
  switch (image.layout) {
    case PixelLayout::Gray8:
      return {static_cast<float>(pixel[0]), static_cast<float>(pixel[0]), static_cast<float>(pixel[0])};
    case PixelLayout::Rgb8:
      return {static_cast<float>(pixel[2]), static_cast<float>(pixel[1]), static_cast<float>(pixel[0])};
    case PixelLayout::Rgba8:
      return {over_white(pixel[2], pixel[3]), over_white(pixel[1], pixel[3]), over_white(pixel[0], pixel[3])};
    case PixelLayout::Bgra8:
      return {over_white(pixel[0], pixel[3]), over_white(pixel[1], pixel[3]), over_white(pixel[2], pixel[3])};
  }
  return {255.0F, 255.0F, 255.0F};
}

// Bilinear sample at a pixel-centre position, the image's edge repeated outside it.
std::array<float, 3> sample(const ImageView& image, double x, double y) noexcept {
  const double fx = std::clamp(x - 0.5, 0.0, static_cast<double>(image.width - 1));
  const double fy = std::clamp(y - 0.5, 0.0, static_cast<double>(image.height - 1));
  const auto x0 = static_cast<std::int32_t>(fx);
  const auto y0 = static_cast<std::int32_t>(fy);
  const std::int32_t x1 = std::min(image.width - 1, x0 + 1);
  const std::int32_t y1 = std::min(image.height - 1, y0 + 1);
  const auto tx = static_cast<float>(fx - x0);
  const auto ty = static_cast<float>(fy - y0);
  const auto a = bgr_over_white(image, x0, y0);
  const auto b = bgr_over_white(image, x1, y0);
  const auto c = bgr_over_white(image, x0, y1);
  const auto d = bgr_over_white(image, x1, y1);
  std::array<float, 3> out{};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const float top = a[channel] + (b[channel] - a[channel]) * tx;
    const float bottom = c[channel] + (d[channel] - c[channel]) * tx;
    out[channel] = top + (bottom - top) * ty;
  }
  return out;
}

double distance(const Point& a, const Point& b) noexcept {
  return std::hypot(b.x - a.x, b.y - a.y);
}

// A YAML list item's scalar: plain, single-quoted ('' is a quote) or double-quoted with the
// escapes a dictionary can hold.
std::string unquoted(std::string_view value) {
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    std::string out;
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
      out.push_back(value[index]);
      if (value[index] == '\'' && index + 2 < value.size() && value[index + 1] == '\'') {
        ++index;
      }
    }
    return out;
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    std::string out;
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
      if (value[index] != '\\' || index + 2 >= value.size()) {
        out.push_back(value[index]);
        continue;
      }
      const char escaped = value[++index];
      switch (escaped) {
        case 'n':
          out.push_back('\n');
          break;
        case 't':
          out.push_back('\t');
          break;
        case '0':
          out.push_back('\0');
          break;
        default:
          out.push_back(escaped);  // \\ and \" and anything else: the character itself
          break;
      }
    }
    return out;
  }
  return std::string(value);
}

}  // namespace

LineInput line_input(const ImageView& image, const Quad& quad) {
  if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
    throw std::invalid_argument("text recognition was given an empty or badly sized image");
  }
  const double width = std::max(1.0, (distance(quad[0], quad[1]) + distance(quad[3], quad[2])) / 2.0);
  const double height = std::max(1.0, (distance(quad[0], quad[3]) + distance(quad[1], quad[2])) / 2.0);
  LineInput input;
  input.vertical = height >= kVerticalLineRatio * width;
  // The line's own frame: `along` runs in the reading direction of the input, `down` across
  // it. A column is read with its top at the left, so along is down the column and down
  // points to the column's left.
  const Point origin = input.vertical ? quad[1] : quad[0];
  const Point along_end = input.vertical ? quad[2] : quad[1];
  const Point down_end = input.vertical ? quad[0] : quad[3];
  const double line_length = input.vertical ? height : width;
  const double line_thickness = input.vertical ? width : height;

  const double scale = static_cast<double>(kRecognizerHeight) / line_thickness;
  input.text_width = static_cast<std::int32_t>(
      std::clamp(std::ceil(line_length * scale), 1.0, static_cast<double>(kRecognizerMaxWidth)));
  input.tensor_width = std::max(input.text_width, kRecognizerMinWidth);
  const auto plane = static_cast<std::size_t>(kRecognizerHeight) * static_cast<std::size_t>(input.tensor_width);
  input.values.assign(plane * 3U, 0.0F);

  // Source pixels per input pixel, each way: the sample grid follows the larger.
  const double step_along = line_length / input.text_width;
  const double step_down = line_thickness / kRecognizerHeight;
  const int grid = static_cast<int>(std::clamp(std::ceil(std::max(step_along, step_down)), 1.0, 4.0));
  const double ax = (along_end.x - origin.x) / input.text_width;
  const double ay = (along_end.y - origin.y) / input.text_width;
  const double dx = (down_end.x - origin.x) / kRecognizerHeight;
  const double dy = (down_end.y - origin.y) / kRecognizerHeight;
  const float weight = 1.0F / static_cast<float>(grid * grid);
  for (std::int32_t row = 0; row < kRecognizerHeight; ++row) {
    for (std::int32_t column = 0; column < input.text_width; ++column) {
      std::array<float, 3> sum{0.0F, 0.0F, 0.0F};
      for (int sub_y = 0; sub_y < grid; ++sub_y) {
        const double v = row + (sub_y + 0.5) / grid;
        for (int sub_x = 0; sub_x < grid; ++sub_x) {
          const double u = column + (sub_x + 0.5) / grid;
          const auto value = sample(image, origin.x + u * ax + v * dx, origin.y + u * ay + v * dy);
          sum[0] += value[0];
          sum[1] += value[1];
          sum[2] += value[2];
        }
      }
      const auto at = static_cast<std::size_t>(row) * static_cast<std::size_t>(input.tensor_width) + static_cast<std::size_t>(column);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        input.values[channel * plane + at] = sum[channel] * weight / 127.5F - 1.0F;
      }
    }
  }
  return input;
}

std::vector<std::string> character_list_from_yaml(std::string_view yaml) {
  std::vector<std::string> characters;
  bool inside = false;
  std::size_t start = 0;
  while (start <= yaml.size()) {
    std::size_t end = yaml.find('\n', start);
    if (end == std::string_view::npos) {
      end = yaml.size();
    }
    std::string_view line = yaml.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    start = end + 1;
    if (!inside) {
      inside = line.starts_with("  character_dict:");
      continue;
    }
    if (line == "  -") {
      characters.emplace_back();
      continue;
    }
    if (!line.starts_with("  - ")) {
      break;  // the list has ended
    }
    characters.push_back(unquoted(line.substr(4)));
  }
  if (characters.empty()) {
    throw std::runtime_error("text recognition: the model's character list could not be read");
  }
  return characters;
}

DecodedLine decode_line(std::span<const float> values, std::int32_t steps, std::int32_t classes,
                        const std::vector<std::string>& characters) {
  if (steps < 0 || classes < 2 ||
      values.size() != static_cast<std::size_t>(steps) * static_cast<std::size_t>(classes) ||
      static_cast<std::size_t>(classes) != characters.size() + 2U) {
    throw std::runtime_error("text recognition: the model's output does not fit its character list");
  }
  DecodedLine line;
  double confidence = 0.0;
  std::int32_t previous = 0;
  for (std::int32_t step = 0; step < steps; ++step) {
    const float* row = values.data() + static_cast<std::size_t>(step) * static_cast<std::size_t>(classes);
    // The first of the largest: a fixed rule for ties.
    const auto best = static_cast<std::int32_t>(std::max_element(row, row + classes) - row);
    if (best != 0 && best != previous) {
      const bool space = static_cast<std::size_t>(best) == characters.size() + 1U;
      line.characters.push_back({space ? std::string(" ") : characters[static_cast<std::size_t>(best) - 1U], step});
      line.text += line.characters.back().text;
      confidence += row[best];
    }
    previous = best;
  }
  line.confidence = line.characters.empty() ? 0.0 : confidence / static_cast<double>(line.characters.size());
  return line;
}

std::vector<Word> words_from_decoded_line(const DecodedLine& line, std::int32_t steps, double text_fraction,
                                          const Quad& quad) {
  std::vector<Word> words;
  if (line.characters.empty() || steps <= 0 || text_fraction <= 0.0) {
    return words;
  }
  // Where along the line (0 to 1) the gap before character `index` has its middle.
  const auto boundary = [&](std::size_t index) {
    double step = 0.0;
    if (index == 0) {
      step = 0.0;
    } else if (index >= line.characters.size()) {
      step = static_cast<double>(steps) * text_fraction;
    } else {
      step = (line.characters[index - 1].step + line.characters[index].step + 1) / 2.0;
    }
    return std::clamp(step / (static_cast<double>(steps) * text_fraction), 0.0, 1.0);
  };
  const auto along = [&](double t) {
    const Point top{quad[0].x + (quad[1].x - quad[0].x) * t, quad[0].y + (quad[1].y - quad[0].y) * t};
    const Point bottom{quad[3].x + (quad[2].x - quad[3].x) * t, quad[3].y + (quad[2].y - quad[3].y) * t};
    return std::pair{top, bottom};
  };
  std::size_t first = 0;
  while (first < line.characters.size()) {
    if (line.characters[first].text == " ") {
      ++first;
      continue;
    }
    std::size_t last = first;
    while (last + 1 < line.characters.size() && line.characters[last + 1].text != " ") {
      ++last;
    }
    Word word;
    for (std::size_t index = first; index <= last; ++index) {
      word.text += line.characters[index].text;
    }
    const auto [top_left, bottom_left] = along(boundary(first));
    const auto [top_right, bottom_right] = along(boundary(last + 1));
    word.quad = {top_left, top_right, bottom_right, bottom_left};
    word.box = box_from_quad(word.quad);
    words.push_back(std::move(word));
    first = last + 1;
  }
  return words;
}

std::vector<Word> words_on_line(const std::vector<Word>& words, const Quad& line) {
  std::vector<Word> placed;
  const double dx = line[1].x - line[0].x;
  const double dy = line[1].y - line[0].y;
  const double length = std::hypot(dx, dy);
  if (!(length > 0.0)) {
    return placed;
  }
  // Where a point lies along the line's top edge, 0 at its start and 1 at its end.
  const auto along = [&](const Point& point) { return ((point.x - line[0].x) * dx + (point.y - line[0].y) * dy) / (length * length); };
  const auto at = [&](double t) {
    return std::pair{Point{line[0].x + dx * t, line[0].y + dy * t},
                     Point{line[3].x + (line[2].x - line[3].x) * t, line[3].y + (line[2].y - line[3].y) * t}};
  };
  for (const auto& word : words) {
    const double start = std::clamp(std::min(along(word.quad[0]), along(word.quad[3])), 0.0, 1.0);
    const double end = std::clamp(std::max(along(word.quad[1]), along(word.quad[2])), 0.0, 1.0);
    if (!(end > start)) {
      continue;
    }
    const auto [top_left, bottom_left] = at(start);
    const auto [top_right, bottom_right] = at(end);
    Word moved;
    moved.text = word.text;
    moved.quad = {top_left, top_right, bottom_right, bottom_left};
    moved.box = box_from_quad(moved.quad);
    placed.push_back(std::move(moved));
  }
  return placed;
}

}  // namespace patchy::ocr
