#include "ui/pdf_recognized_text.hpp"

#include "core/layer_metadata.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace patchy::ui {

namespace {

constexpr std::string_view kPlacesVersion = "1";

// Qt's number formatting and parsing use the C locale whatever the process locale is.
void append_number(std::string& out, double value) {
  out += QByteArray::number(value, 'f', 2).toStdString();
}

// Fields separated by `separator`; an empty input is one empty field.
std::vector<std::string_view> split(std::string_view text, char separator) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (true) {
    const auto end = text.find(separator, start);
    if (end == std::string_view::npos) {
      fields.push_back(text.substr(start));
      break;
    }
    fields.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return fields;
}

std::optional<double> number(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  bool ok = false;
  const double value = QByteArrayView(text.data(), static_cast<qsizetype>(text.size())).toDouble(&ok);
  if (!ok || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

// A layer's size still counts as the recognized one within a tenth (a correction typed
// into a line changes its length a little), and never less than two pixels.
bool close_size(std::int32_t recognized, std::int32_t now) {
  return std::abs(recognized - now) <= std::max(2, recognized / 10);
}

struct GroupText {
  const Layer* layer{nullptr};
  bool shown{true};  // the layer and every group above it are visible
};

void collect_group_text(const Layer& layer, bool inside_group, bool shown, std::vector<GroupText>& out) {
  const bool here = inside_group || layer_is_recognized_text_group(layer);
  const bool visible = shown && layer.visible();
  if (here && layer_is_text(layer)) {
    out.push_back({&layer, visible});
  }
  const auto& children = layer.children();
  for (auto child = children.rbegin(); child != children.rend(); ++child) {
    collect_group_text(*child, here, visible, out);
  }
}

// From the top of the layer stack down: a Recognized Text group holds its blocks in
// reading order from the top, and a reader takes a page's text in the order it is written.
std::vector<GroupText> recognized_text_layers(const Document& document) {
  std::vector<GroupText> layers;
  const auto& roots = document.layers();
  for (auto layer = roots.rbegin(); layer != roots.rend(); ++layer) {
    collect_group_text(*layer, false, true, layers);
  }
  return layers;
}

RecognizedLinePlace box_place(double x, double y, double width, double height, bool vertical) {
  return {{QPointF(x, y), QPointF(x + width, y), QPointF(x + width, y + height), QPointF(x, y + height)}, vertical};
}

double distance(const QPointF& a, const QPointF& b) {
  return std::hypot(b.x() - a.x(), b.y() - a.y());
}

}  // namespace

std::string serialize_recognized_line_places(const Rect& layer_bounds, const std::vector<RecognizedLinePlace>& places) {
  std::string out(kPlacesVersion);
  out += '|';
  out += std::to_string(layer_bounds.x) + ',' + std::to_string(layer_bounds.y) + ',' +
         std::to_string(layer_bounds.width) + ',' + std::to_string(layer_bounds.height);
  for (const auto& place : places) {
    out += '|';
    for (const auto& corner : place.quad) {
      append_number(out, corner.x());
      out += ',';
      append_number(out, corner.y());
      out += ',';
    }
    out += place.vertical ? '1' : '0';
  }
  return out;
}

std::optional<RecognizedLinePlaces> parse_recognized_line_places(std::string_view value) {
  const auto fields = split(value, '|');
  if (fields.size() < 3 || fields[0] != kPlacesVersion) {
    return std::nullopt;
  }
  RecognizedLinePlaces parsed;
  const auto bounds = split(fields[1], ',');
  if (bounds.size() != 4) {
    return std::nullopt;
  }
  std::array<double, 4> rect{};
  for (std::size_t index = 0; index < 4; ++index) {
    const auto parsed_number = number(bounds[index]);
    if (!parsed_number.has_value()) {
      return std::nullopt;
    }
    rect[index] = *parsed_number;
  }
  parsed.layer_bounds = Rect{static_cast<std::int32_t>(rect[0]), static_cast<std::int32_t>(rect[1]),
                             static_cast<std::int32_t>(rect[2]), static_cast<std::int32_t>(rect[3])};
  for (std::size_t field = 2; field < fields.size(); ++field) {
    const auto numbers = split(fields[field], ',');
    if (numbers.size() != 9 || (numbers[8] != "0" && numbers[8] != "1")) {
      return std::nullopt;
    }
    RecognizedLinePlace place;
    for (std::size_t corner = 0; corner < 4; ++corner) {
      const auto x = number(numbers[corner * 2]);
      const auto y = number(numbers[corner * 2 + 1]);
      if (!x.has_value() || !y.has_value()) {
        return std::nullopt;
      }
      place.quad[corner] = QPointF(*x, *y);
    }
    place.vertical = numbers[8] == "1";
    parsed.places.push_back(place);
  }
  return parsed;
}

bool document_has_recognized_text(const Document& document) {
  return !recognized_text_layers(document).empty();
}

bool document_has_hidden_recognized_text(const Document& document) {
  const auto layers = recognized_text_layers(document);
  return std::any_of(layers.begin(), layers.end(), [](const GroupText& text) { return !text.shown; });
}

std::vector<RecognizedTextLine> recognized_text_lines(const Document& document) {
  std::vector<RecognizedTextLine> lines;
  for (const auto& group_text : recognized_text_layers(document)) {
    const Layer* layer = group_text.layer;
    const auto& metadata = layer->metadata();
    const auto text = metadata.find(kLayerMetadataText);
    if (text == metadata.end()) {
      continue;
    }
    auto story = QString::fromStdString(text->second);
    story.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const auto texts = story.split(QLatin1Char('\n'));
    const auto bounds = layer->bounds();
    if (bounds.width <= 0 || bounds.height <= 0) {
      continue;
    }
    std::optional<RecognizedLinePlaces> stored;
    if (const auto found = metadata.find(kLayerMetadataOcrLine); found != metadata.end()) {
      stored = parse_recognized_line_places(found->second);
    }
    // The recognized places hold while the layer is the same number of lines at about the
    // same size; a move is carried along (by the layer's centre, which a small change of
    // length moves least).
    const bool places_hold = stored.has_value() && stored->places.size() == static_cast<std::size_t>(texts.size()) &&
                             close_size(stored->layer_bounds.width, bounds.width) &&
                             close_size(stored->layer_bounds.height, bounds.height);
    const auto orientation = metadata.find(kLayerMetadataTextOrientation);
    const bool vertical = orientation != metadata.end() && orientation->second == kTextOrientationVertical;
    const auto count = static_cast<double>(texts.size());
    for (qsizetype index = 0; index < texts.size(); ++index) {
      if (texts[index].trimmed().isEmpty()) {
        continue;
      }
      RecognizedTextLine line;
      line.text = texts[index];
      if (places_hold) {
        line.place = stored->places[static_cast<std::size_t>(index)];
        const QPointF moved(bounds.x + bounds.width / 2.0 - (stored->layer_bounds.x + stored->layer_bounds.width / 2.0),
                            bounds.y + bounds.height / 2.0 -
                                (stored->layer_bounds.y + stored->layer_bounds.height / 2.0));
        for (auto& corner : line.place.quad) {
          corner += moved;
        }
      } else if (vertical) {
        // Columns from the right, each the layer's height.
        const double width = bounds.width / count;
        line.place = box_place(bounds.x + bounds.width - (static_cast<double>(index) + 1.0) * width, bounds.y, width,
                               bounds.height, true);
      } else {
        const double height = bounds.height / count;
        line.place = box_place(bounds.x, bounds.y + static_cast<double>(index) * height, bounds.width, height, false);
      }
      const QPointF middle = (line.place.quad[0] + line.place.quad[2]) / 2.0;
      if (middle.x() < 0.0 || middle.y() < 0.0 || middle.x() > document.width() || middle.y() > document.height()) {
        continue;
      }
      lines.push_back(std::move(line));
    }
  }
  return lines;
}

QTransform document_to_pdf_page(QSize document_pixels, QSizeF page_points, int rotate) {
  if (document_pixels.isEmpty() || page_points.isEmpty()) {
    return {};
  }
  const bool turned = rotate == 90 || rotate == 270;
  // The page as displayed, in points with y down: what the document's pixels cover.
  const double shown_width = turned ? page_points.height() : page_points.width();
  const double shown_height = turned ? page_points.width() : page_points.height();
  const QTransform to_shown = QTransform::fromScale(shown_width / document_pixels.width(),
                                                    shown_height / document_pixels.height());
  // Displayed (xd, yd) back to the page's own space (x right, y up) for each /Rotate.
  QTransform to_page;
  switch (rotate) {
    case 90:
      to_page = QTransform(0.0, 1.0, 1.0, 0.0, 0.0, 0.0);  // x = yd, y = xd
      break;
    case 180:
      to_page = QTransform(-1.0, 0.0, 0.0, 1.0, page_points.width(), 0.0);  // x = W - xd, y = yd
      break;
    case 270:
      to_page = QTransform(0.0, -1.0, -1.0, 0.0, page_points.width(), page_points.height());  // x = W - yd, y = H - xd
      break;
    default:
      to_page = QTransform(1.0, 0.0, 0.0, -1.0, 0.0, page_points.height());  // x = xd, y = H - yd
      break;
  }
  return to_shown * to_page;
}

std::vector<pdf::TextLine> pdf_text_lines(const std::vector<RecognizedTextLine>& lines, const QTransform& to_page) {
  std::vector<pdf::TextLine> out;
  out.reserve(lines.size());
  for (const auto& line : lines) {
    const auto& quad = line.place.quad;
    // The run starts at the box's bottom left and runs along its bottom edge; a column
    // starts at its top left and runs down its left edge. Either way the box lies to the
    // left of the reading direction, where the font's glyph is.
    const QPointF start = to_page.map(line.place.vertical ? quad[0] : quad[3]);
    const QPointF end = to_page.map(line.place.vertical ? quad[3] : quad[2]);
    const QPointF across = to_page.map(line.place.vertical ? quad[1] : quad[0]);
    pdf::TextLine text;
    text.utf8 = line.text.toStdString();
    text.x = start.x();
    text.y = start.y();
    text.width = distance(start, end);
    text.size = distance(start, across);
    text.angle_degrees = std::atan2(end.y() - start.y(), end.x() - start.x()) * 180.0 / 3.14159265358979323846;
    if (text.width > 0.0 && text.size > 0.0) {
      out.push_back(std::move(text));
    }
  }
  return out;
}

}  // namespace patchy::ui
