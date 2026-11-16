#include "ui/ocr_text_layers.hpp"

#include "core/layer.hpp"
#include "core/layer_render_utils.hpp"
#include "psd/psd_text_runs.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/text_layout.hpp"

#include <QAbstractTextDocumentLayout>
#include <QStringList>
#include <QTextBlock>
#include <QTextLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace patchy::ui {

namespace {

// The size lines are measured at before the fit; any size works, a large one keeps the
// measure fine-grained.
constexpr int kProbeSize = 100;
constexpr int kMinimumSize = 4;
constexpr int kMaximumSize = 2000;
// A recognized line box is about this share of the font size tall (ascender to descender
// of mixed-case text) or, for a vertical column of full-width characters, wide. Only a
// sanity bound on the width fit, and the fallback for text too short to fit by width.
constexpr double kLineBoxShareOfSize = 0.9;
constexpr double kMinimumFitRatio = 0.5;
constexpr double kMaximumFitRatio = 2.2;
// An italic face leans 10 to 14 degrees; upright text measures within a step of zero.
constexpr double kItalicSlantDegrees = 7.0;
constexpr double kMaxUprightAngle = 2.0;

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

// A source pixel as red, green, blue over white.
std::array<int, 3> rgb_over_white(const QImage& source, int x, int y) {
  const auto* pixel = source.constScanLine(y) + static_cast<std::ptrdiff_t>(x) * 4;
  const int alpha = pixel[3];
  const auto over = [alpha](int value) { return (value * alpha + 255 * (255 - alpha) + 127) / 255; };
  return {over(pixel[0]), over(pixel[1]), over(pixel[2])};
}

QRect clamped_rect(const QImage& source, const QRectF& box) {
  return box.toAlignedRect().intersected(source.rect());
}

// The per-channel median of the one-pixel ring just outside `rect` (inside the image).
std::array<int, 3> ring_median(const QImage& source, const QRect& rect) {
  std::array<std::vector<int>, 3> channels;
  const QRect ring = rect.adjusted(-1, -1, 1, 1).intersected(source.rect());
  const auto take = [&](int x, int y) {
    const auto rgb = rgb_over_white(source, x, y);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      channels[channel].push_back(rgb[channel]);
    }
  };
  for (int x = ring.left(); x <= ring.right(); ++x) {
    take(x, ring.top());
    take(x, ring.bottom());
  }
  for (int y = ring.top() + 1; y < ring.bottom(); ++y) {
    take(ring.left(), y);
    take(ring.right(), y);
  }
  std::array<int, 3> result{255, 255, 255};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    auto& values = channels[channel];
    if (!values.empty()) {
      std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2), values.end());
      result[channel] = values[values.size() / 2];
    }
  }
  return result;
}

int squared_distance(const std::array<int, 3>& a, const std::array<int, 3>& b) {
  int sum = 0;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    sum += (a[channel] - b[channel]) * (a[channel] - b[channel]);
  }
  return sum;
}

// EngineData /Justification, which the paragraph runs name: 0 left, 1 right, 2 center.
int justification(ocr::Alignment alignment) {
  return alignment == ocr::Alignment::End ? 1 : alignment == ocr::Alignment::Center ? 2 : 0;
}

struct BlockText {
  QStringList lines;
  bool vertical{false};
  ocr::Alignment alignment{ocr::Alignment::Start};
  double leading{0.0};  // 0: one line, or natural spacing
  QString family;
  bool italic{false};
  QColor color;
  int anti_alias{3};
};

struct SerializedText {
  TextToolSettings settings;
  QString runs;
  QString paragraphs;
};

// The layer's text metadata for `block` at `size`: one style run over the whole story and
// one paragraph per line, through the same serializers the PSD reader uses.
SerializedText serialize(const BlockText& block, int size) {
  const QString text = block.lines.join(QLatin1Char('\n'));
  psd::PsdTextStyleRun run;
  run.start = 0;
  run.length = static_cast<int>(text.size());
  run.family = block.family.toStdString();
  run.size = size;
  run.color = RgbColor{static_cast<std::uint8_t>(block.color.red()), static_cast<std::uint8_t>(block.color.green()),
                       static_cast<std::uint8_t>(block.color.blue())};
  run.italic = block.italic;
  const bool fixed_leading = block.lines.size() > 1 && block.leading > 0.0;
  if (fixed_leading) {
    run.leading = block.leading;
  }
  const std::array<psd::PsdTextStyleRun, 1> runs{run};

  std::vector<psd::PsdTextParagraphRun> paragraphs;
  int start = 0;
  for (qsizetype index = 0; index < block.lines.size(); ++index) {
    psd::PsdTextParagraphRun paragraph;
    paragraph.start = start;
    // Every paragraph but the last owns the line break after it, as a QTextBlock does.
    paragraph.length = static_cast<int>(block.lines[index].size()) + (index + 1 < block.lines.size() ? 1 : 0);
    paragraph.justification = justification(block.alignment);
    start += paragraph.length;
    paragraphs.push_back(paragraph);
  }

  SerializedText serialized;
  serialized.runs = QString::fromStdString(psd::serialize_patchy_text_runs(runs));
  serialized.paragraphs = QString::fromStdString(psd::serialize_patchy_paragraph_runs(paragraphs));
  auto& settings = serialized.settings;
  settings.text = text;
  settings.family = block.family;
  settings.size = size;
  settings.bold = false;
  settings.italic = block.italic;
  settings.anti_alias = block.anti_alias;
  settings.boxed = false;
  settings.vertical = block.vertical;
  // A fixed leading only lays out under the Photoshop line model (the Character panel's
  // leading field opts a layer in the same way).
  settings.photoshop_layout = fixed_leading;
  settings.html = document_html_from_text_runs(text, serialized.runs, settings, block.color);
  return serialized;
}

// How long each line of the laid-out story is along its reading direction, at the size it
// was serialized with.
std::vector<double> line_lengths(const SerializedText& text, QColor color) {
  const auto plan = build_text_render_plan(text.settings, color, 320, text.paragraphs, text.runs, std::nullopt, 1.0,
                                           QTransform(), 1.0);
  std::vector<double> lengths;
  if (plan.vertical) {
    for (const auto& column : plan.vertical_plan.columns) {
      lengths.push_back(column.cells.empty() ? 0.0
                                             : column.cells.back().top + column.cells.back().advance - column.top);
    }
  } else if (plan.built.document != nullptr) {
    // One paragraph per recognized line, and point text never wraps: each block's lines
    // (one, in practice) give that line's natural width. Read from the laid-out document,
    // not from plan.line_render_items: the plan only lists line items for the layouts that
    // place lines itself (box text, the Photoshop line model), so plain point text has none
    // and every single-line block silently fell back to its thickness.
    (void)plan.built.document->documentLayout()->documentSize();  // make sure it is laid out
    for (auto block = plan.built.document->begin(); block.isValid(); block = block.next()) {
      double width = 0.0;
      if (const auto* layout = block.layout(); layout != nullptr) {
        for (int index = 0; index < layout->lineCount(); ++index) {
          width = std::max(width, layout->lineAt(index).naturalTextWidth());
        }
      }
      lengths.push_back(width);
    }
  }
  return lengths;
}

struct LineTarget {
  double length{0.0};     // along the reading direction, document pixels
  double thickness{0.0};  // across it
};

LineTarget target_of(const ocr::Line& line) {
  const auto distance = [](const ocr::Point& a, const ocr::Point& b) { return std::hypot(b.x - a.x, b.y - a.y); };
  const double top_edge = distance(line.quad[0], line.quad[1]);
  const double side_edge = distance(line.quad[0], line.quad[3]);
  return line.vertical ? LineTarget{side_edge, top_edge} : LineTarget{top_edge, side_edge};
}

struct FittedSize {
  int size{0};
  // False when the line thickness decided (text too short to fit by length, or a length
  // fit outside reason).
  bool by_length{false};
};

// One size for the block: each line's length fit against the family, the median of those,
// kept within reason of what the line thickness says. Text too short to fit by length
// (a page number, a single character) takes the thickness alone.
FittedSize fitted_size(const BlockText& block, const std::vector<LineTarget>& targets) {
  std::vector<double> thicknesses;
  for (const auto& target : targets) {
    thicknesses.push_back(target.thickness);
  }
  const double by_thickness = median(thicknesses) / kLineBoxShareOfSize;

  const auto probe = serialize(block, kProbeSize);
  const auto lengths = line_lengths(probe, block.color);
  std::vector<double> fits;
  for (std::size_t index = 0; index < std::min(lengths.size(), targets.size()); ++index) {
    QString visible = block.lines[static_cast<qsizetype>(index)];
    visible.remove(QLatin1Char(' '));
    if (lengths[index] > 1.0 && targets[index].length > 1.0 && visible.size() >= 3) {
      fits.push_back(kProbeSize * targets[index].length / lengths[index]);
    }
  }
  bool by_length = !fits.empty();
  double size = by_length ? median(std::move(fits)) : by_thickness;
  if (by_thickness > 0.0 && (size < kMinimumFitRatio * by_thickness || size > kMaximumFitRatio * by_thickness)) {
    size = by_thickness;
    by_length = false;
  }
  return FittedSize{std::clamp(static_cast<int>(std::lround(size)), kMinimumSize, kMaximumSize), by_length};
}

QRectF document_box(const ocr::Box& box, QPoint origin) {
  return QRectF(box.x + origin.x(), box.y + origin.y(), box.width, box.height);
}

}  // namespace

QString ocr_grouping_token(OcrGrouping grouping) {
  return grouping == OcrGrouping::Lines ? QStringLiteral("lines") : QStringLiteral("blocks");
}

std::optional<OcrGrouping> ocr_grouping_from_token(const QString& token) {
  if (token == QLatin1String("blocks")) {
    return OcrGrouping::Blocks;
  }
  if (token == QLatin1String("lines")) {
    return OcrGrouping::Lines;
  }
  return std::nullopt;
}

QString recognized_text_layer_name(const QString& text) {
  auto name = text.simplified();
  if (name.size() > 24) {
    name = name.left(24) + QStringLiteral("...");
  }
  return name;
}

QColor sampled_text_color(const QImage& source, const QRectF& box) {
  const QRect rect = clamped_rect(source, box);
  if (source.format() != QImage::Format_RGBA8888 || rect.width() < 2 || rect.height() < 2) {
    return QColor(Qt::black);  // color-literal-ok: text color of a document layer
  }
  const auto background = ring_median(source, rect);
  // At most about 20,000 samples on an even grid: enough for a colour, cheap on a headline.
  const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(rect.width()) * rect.height() / 20000.0)));
  struct Sample {
    int distance;
    std::array<int, 3> rgb;
  };
  std::vector<Sample> samples;
  for (int y = rect.top(); y <= rect.bottom(); y += step) {
    for (int x = rect.left(); x <= rect.right(); x += step) {
      const auto rgb = rgb_over_white(source, x, y);
      samples.push_back(Sample{squared_distance(rgb, background), rgb});
    }
  }
  if (samples.empty()) {
    return QColor(Qt::black);  // color-literal-ok: text color of a document layer
  }
  const auto fifth = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - std::max<std::size_t>(1, samples.size() / 5));
  std::nth_element(samples.begin(), fifth, samples.end(),
                   [](const Sample& a, const Sample& b) { return a.distance < b.distance; });
  const int farthest =
      std::max_element(fifth, samples.end(), [](const Sample& a, const Sample& b) { return a.distance < b.distance; })
          ->distance;
  // Nothing stands out by 24 levels a channel: the box holds no text Patchy can see.
  if (farthest < 3 * 24 * 24) {
    return QColor(Qt::black);  // color-literal-ok: text color of a document layer
  }
  std::array<long long, 3> sum{0, 0, 0};
  long long count = 0;
  for (auto sample = fifth; sample != samples.end(); ++sample) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      sum[channel] += sample->rgb[channel];
    }
    ++count;
  }
  return QColor(static_cast<int>((sum[0] + count / 2) / count), static_cast<int>((sum[1] + count / 2) / count),
                static_cast<int>((sum[2] + count / 2) / count));
}

double measured_slant_degrees(const QImage& source, const QRectF& box) {
  const QRect rect = clamped_rect(source, box);
  if (source.format() != QImage::Format_RGBA8888 || rect.height() < 12 || rect.width() < 3 * rect.height()) {
    return 0.0;
  }
  const auto background = ring_median(source, rect);
  // Ink: pixels at least half as far from the background as the farthest one. Rows and
  // columns are stepped so the measure costs about the same on a headline as on body text.
  const int step = std::max(1, rect.height() / 48);
  const int rows = (rect.height() + step - 1) / step;
  const int columns = (rect.width() + step - 1) / step;
  int farthest = 0;
  std::vector<int> distances(static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns));
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      const int distance =
          squared_distance(rgb_over_white(source, rect.left() + column * step, rect.top() + row * step), background);
      distances[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column)] =
          distance;
      farthest = std::max(farthest, distance);
    }
  }
  if (farthest < 3 * 24 * 24) {
    return 0.0;
  }
  const int threshold = farthest / 4;  // half the distance, squared
  double best_degrees = 0.0;
  double best_score = -1.0;
  const int margin = rows;  // room for the sheared rows on either side
  std::vector<int> profile(static_cast<std::size_t>(columns + 2 * margin));
  // From upright outwards, so upright wins unless a lean is measurably sharper.
  static constexpr std::array<int, 15> kTriedDegrees = {0, -2, 2, -4, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24};
  for (const int degrees : kTriedDegrees) {
    std::fill(profile.begin(), profile.end(), 0);
    const double slope = std::tan(degrees * std::numbers::pi / 180.0);
    for (int row = 0; row < rows; ++row) {
      // Undo a lean of `degrees`: rows above the middle move left, rows below move right.
      const int shift = static_cast<int>(std::lround((row - rows / 2.0) * slope));
      for (int column = 0; column < columns; ++column) {
        if (distances[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) +
                      static_cast<std::size_t>(column)] >= threshold) {
          const int target = column + shift + margin;
          if (target >= 0 && target < static_cast<int>(profile.size())) {
            ++profile[static_cast<std::size_t>(target)];
          }
        }
      }
    }
    double score = 0.0;
    for (const int value : profile) {
      score += static_cast<double>(value) * value;
    }
    if (best_score < 0.0 || score > best_score * 1.001) {
      best_score = score;
      best_degrees = degrees;
    }
  }
  return best_degrees;
}

std::vector<RecognizedTextLayer> build_recognized_text_layers(const ocr::Result& result, const QImage& source,
                                                              QPoint origin, OcrGrouping grouping,
                                                              const OcrLayerStyle& style) {
  std::vector<RecognizedTextLayer> layers;
  const auto blocks =
      grouping == OcrGrouping::Lines ? ocr::lines_as_blocks(result) : ocr::group_lines_into_blocks(result);
  for (const auto& block : blocks) {
    BlockText text;
    text.vertical = block.vertical;
    text.alignment = block.alignment;
    text.family = style.family;
    text.anti_alias = style.anti_alias;
    std::vector<LineTarget> targets;
    std::vector<std::size_t> members;
    for (const std::size_t index : block.lines) {
      const auto& line = result.lines[index];
      const auto line_text = QString::fromUtf8(line.text.data(), static_cast<qsizetype>(line.text.size())).simplified();
      if (line_text.isEmpty()) {
        continue;
      }
      text.lines.push_back(line_text);
      targets.push_back(target_of(line));
      members.push_back(index);
    }
    if (text.lines.isEmpty()) {
      continue;
    }
    const auto& first_line = result.lines[members.front()];
    const double angle = first_line.angle_degrees;
    const bool turned = std::abs(angle) > kMaxUprightAngle;
    text.leading = text.lines.size() > 1 ? block.line_pitch : 0.0;

    // The longest line speaks for the block's colour and slant.
    const auto longest = *std::max_element(members.begin(), members.end(), [&](std::size_t a, std::size_t b) {
      return result.lines[a].box.width * result.lines[a].box.height <
             result.lines[b].box.width * result.lines[b].box.height;
    });
    const QRectF sample_box(result.lines[longest].box.x, result.lines[longest].box.y, result.lines[longest].box.width,
                            result.lines[longest].box.height);
    text.color = style.fixed_color.value_or(style.match_style ? sampled_text_color(source, sample_box) : style.color);
    text.italic = style.match_style && !text.vertical && !turned &&
                  measured_slant_degrees(source, sample_box) >= kItalicSlantDegrees;

    const FittedSize fit = style.match_style
                               ? fitted_size(text, targets)
                               : FittedSize{std::clamp(style.size, kMinimumSize, kMaximumSize), false};
    auto serialized = serialize(text, fit.size);

    // Draw it, then put its ink where the recognized lines are: the centre of what was
    // drawn on the centre of the block's line boxes.
    QRectF target;
    for (const std::size_t index : members) {
      target = target.united(document_box(result.lines[index].box, origin));
    }
    QTransform turn;
    if (turned) {
      turn.rotate(angle);
    }
    auto rendered = render_text_pixels_with_local_rect(serialized.settings, text.color, 320, serialized.paragraphs,
                                                       serialized.runs, std::nullopt, 1.0, turn);
    auto ink = visible_alpha_local_bounds(rendered.pixels);
    if (rendered.pixels.empty() || !ink.has_value()) {
      continue;
    }
    // The fit above measured the layout at a probe size; what counts is the ink that size
    // really draws (a layout's natural width and its rendered glyphs do not scale alike on
    // every system: the Mac came out 6% wide). So the drawn block is measured against the
    // recognized lines along the reading direction, and when it is more than 2% off it is
    // drawn once more at the size that difference calls for. One correction, no loop.
    if (fit.by_length && !turned) {
      const double drawn = text.vertical ? ink->height : ink->width;
      const double wanted = text.vertical ? target.height() : target.width();
      if (drawn > 1.0 && wanted > 1.0 && std::abs(wanted / drawn - 1.0) > 0.02) {
        const int corrected =
            std::clamp(static_cast<int>(std::lround(fit.size * wanted / drawn)), kMinimumSize, kMaximumSize);
        if (corrected != fit.size) {
          auto again = serialize(text, corrected);
          auto redrawn = render_text_pixels_with_local_rect(again.settings, text.color, 320, again.paragraphs,
                                                            again.runs, std::nullopt, 1.0, turn);
          if (const auto redrawn_ink = visible_alpha_local_bounds(redrawn.pixels);
              !redrawn.pixels.empty() && redrawn_ink.has_value()) {
            serialized = std::move(again);
            rendered = std::move(redrawn);
            ink = redrawn_ink;
          }
        }
      }
    }
    const double buffer_left = std::floor(rendered.local_rect.left());
    const double buffer_top = std::floor(rendered.local_rect.top());
    const QPointF ink_center(buffer_left + ink->x + ink->width / 2.0, buffer_top + ink->y + ink->height / 2.0);
    const QPoint shift(static_cast<int>(std::lround(target.center().x() - ink_center.x())),
                       static_cast<int>(std::lround(target.center().y() - ink_center.y())));

    RecognizedTextLayer layer;
    layer.name = recognized_text_layer_name(serialized.settings.text);
    layer.color = text.color;
    layer.rich_text_runs = std::move(serialized.runs);
    layer.paragraph_runs = std::move(serialized.paragraphs);
    layer.metrics = rendered.metrics;
    layer.lines = std::move(members);
    if (turned) {
      // A turned line keeps only its ink, as every transformed text render does, and its
      // transform: the turn, then the shift that placed it.
      const auto cropped = qimage_from_pixel_buffer(rendered.pixels)
                               .convertToFormat(QImage::Format_RGBA8888)
                               .copy(ink->x, ink->y, ink->width, ink->height);
      layer.pixels = pixels_from_image_rgba(cropped);
      layer.bounds = Rect{static_cast<std::int32_t>(buffer_left) + ink->x + shift.x(),
                          static_cast<std::int32_t>(buffer_top) + ink->y + shift.y(), ink->width, ink->height};
      layer.transform = turn * QTransform::fromTranslate(shift.x(), shift.y());
    } else {
      layer.bounds = Rect{static_cast<std::int32_t>(buffer_left) + shift.x(),
                          static_cast<std::int32_t>(buffer_top) + shift.y(), rendered.pixels.width(),
                          rendered.pixels.height()};
      layer.pixels = std::move(rendered.pixels);
    }
    layer.settings = std::move(serialized.settings);
    layers.push_back(std::move(layer));
  }
  return layers;
}

}  // namespace patchy::ui
