// Text recognition against a real engine (docs/ocr.md): text rendered in-test, read by the
// operating system's engine when this machine has one. These pin what the engine's own
// documentation leaves open: that word boxes land on the pixels, how rotated text is
// reported, and that the banding rule reads a tall page once.

#include "ocr/ocr_engine.hpp"
#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QTransform>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using patchy::test::ui::ScopedRealOcrEngines;

struct OpenedEngine {
  std::shared_ptr<patchy::ocr::Engine> engine;
  std::string language;
};

// The machine's Windows engine with an English recognizer, or a skip: these tests render
// English text, and a machine without that language pack has nothing to measure.
OpenedEngine windows_engine_with_english() {
  const auto engine = patchy::ocr::find_engine("windows");
  if (engine == nullptr) {
    patchy::test::skip(patchy::test::SkipKind::platform, "no built-in text recognition engine on this platform");
  }
  if (engine->unavailable() != patchy::ocr::Unavailable::None) {
    patchy::test::skip(patchy::test::SkipKind::feature,
                       "the Windows text recognition engine is unavailable: " + engine->unavailable_reason());
  }
  for (const auto& language : engine->languages()) {
    if (language.tag.rfind("en", 0) == 0) {
      return OpenedEngine{engine, language.tag};
    }
  }
  patchy::test::skip(patchy::test::SkipKind::feature, "Windows has no English text recognition language installed");
}

QFont test_font(int pixel_size) {
  QFont font(QStringLiteral("Arial"));
  font.setPixelSize(pixel_size);
  font.setStyleStrategy(QFont::PreferAntialias);
  return font;
}

patchy::ocr::ImageView view_of(const QImage& rgba) {
  return patchy::ocr::ImageView{rgba.constBits(), rgba.width(), rgba.height(), rgba.bytesPerLine(),
                                patchy::ocr::PixelLayout::Rgba8};
}

std::string lowered(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(c < 0x80 ? std::tolower(c) : c);
  });
  return text;
}

patchy::ocr::Point center_of(const patchy::ocr::Quad& quad) {
  return {(quad[0].x + quad[1].x + quad[2].x + quad[3].x) / 4.0, (quad[0].y + quad[1].y + quad[2].y + quad[3].y) / 4.0};
}

void ui_ocr_windows_engine_reads_rendered_text_if_available() {
  const ScopedRealOcrEngines real;
  const auto opened = windows_engine_with_english();
  // The language list names what it opened.
  CHECK(!opened.engine->default_language().empty());

  QImage image(1400, 420, QImage::Format_RGBA8888);
  image.fill(Qt::white);
  const QRect first_rect(100, 80, 1200, 80);
  const QRect second_rect(100, 240, 1200, 80);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::black);
    painter.setFont(test_font(56));
    painter.drawText(first_rect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("The quick brown fox"));
    painter.drawText(second_rect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("jumps over 12345 lazy dogs"));
  }
  CHECK(image.save(QStringLiteral("test-artifacts/ui_ocr_windows_engine_rendered_text.png")));

  const auto recognizer = opened.engine->open(opened.language);
  const auto result = recognizer->recognize(view_of(image));
  std::cout << "[INFO] Windows OCR read: " << result.text() << '\n';
  CHECK(result.lines.size() == 2);
  if (result.lines.size() != 2) {
    return;
  }
  CHECK(lowered(result.lines[0].text).find("quick brown fox") != std::string::npos);
  CHECK(lowered(result.lines[1].text).find("12345") != std::string::npos);
  CHECK(result.lines[0].words.size() == 4);
  CHECK(result.lines[1].words.size() == 5);
  // Boxes land on the pixels the text was drawn in: inside its row, starting at its left.
  const auto inside_row = [](const patchy::ocr::Box& box, const QRect& row) {
    return box.y >= row.top() - 4 && box.y + box.height <= row.bottom() + 4 && box.x >= row.left() - 6 &&
           box.x <= row.left() + 20 && box.height >= 30.0;
  };
  CHECK(inside_row(result.lines[0].box, first_rect));
  CHECK(inside_row(result.lines[1].box, second_rect));
  // Upright text: no rotation worth the name, and the quad is the box.
  CHECK(std::abs(result.lines[0].angle_degrees) < 1.0);
  CHECK(!result.lines[0].vertical);
  // Words run left to right inside their line.
  for (const auto& line : result.lines) {
    for (std::size_t index = 1; index < line.words.size(); ++index) {
      CHECK(line.words[index].box.x > line.words[index - 1].box.x);
      CHECK(line.words[index].box.x + line.words[index].box.width <= line.box.x + line.box.width + 1.0);
    }
  }

  // A blank image is an empty result, not an error.
  QImage blank(400, 300, QImage::Format_RGBA8888);
  blank.fill(Qt::white);
  CHECK(recognizer->recognize(view_of(blank)).lines.empty());
  // So is one smaller than the engine's smallest side (it is padded, not refused).
  QImage tiny(20, 12, QImage::Format_RGBA8888);
  tiny.fill(Qt::white);
  CHECK(recognizer->recognize(view_of(tiny)).lines.empty());

  // A language the engine does not have is refused with a reason.
  bool refused = false;
  try {
    (void)opened.engine->open("zz-ZZ");
  } catch (const std::runtime_error& error) {
    refused = std::string(error.what()).size() > 0;
  }
  CHECK(refused);
}

void ui_ocr_windows_engine_reports_rotated_text_if_available() {
  const ScopedRealOcrEngines real;
  const auto opened = windows_engine_with_english();

  // Two lines turned 8 degrees clockwise about the image centre.
  constexpr double kAngle = 8.0;
  QImage image(1400, 900, QImage::Format_RGBA8888);
  image.fill(Qt::white);
  const QPointF center(image.width() / 2.0, image.height() / 2.0);
  QTransform turn;
  turn.translate(center.x(), center.y());
  turn.rotate(kAngle);  // clockwise on screen
  turn.translate(-center.x(), -center.y());
  const QRectF first_rect(200, 340, 1000, 80);
  const QRectF second_rect(200, 480, 1000, 80);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setTransform(turn);
    painter.setPen(Qt::black);
    painter.setFont(test_font(56));
    painter.drawText(first_rect, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Rotated text reads fine"));
    painter.drawText(second_rect, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("when the page is turned"));
  }
  CHECK(image.save(QStringLiteral("test-artifacts/ui_ocr_windows_engine_rotated_text.png")));

  const auto result = opened.engine->open(opened.language)->recognize(view_of(image));
  std::cout << "[INFO] Windows OCR read (rotated): " << result.text() << " angle "
            << result.text_angle_degrees.value_or(0.0) << '\n';
  CHECK(result.lines.size() == 2);
  if (result.lines.size() != 2) {
    return;
  }
  CHECK(lowered(result.lines[0].text).find("rotated text") != std::string::npos);
  // The engine reports the turn, clockwise positive, and each line carries it.
  CHECK(result.text_angle_degrees.has_value());
  CHECK(std::abs(result.text_angle_degrees.value_or(0.0) - kAngle) < 1.5);
  CHECK(std::abs(result.lines[0].angle_degrees - kAngle) < 1.5);
  // Each line's quad sits on the turned text: its centre is the drawn row's centre turned
  // the same way, and its top edge runs along the text.
  const auto check_line = [&](const patchy::ocr::Line& line, const QRectF& row) {
    const QPointF expected = turn.map(row.center());
    const auto actual = center_of(line.quad);
    CHECK(std::abs(actual.x - expected.x()) < 40.0);
    CHECK(std::abs(actual.y - expected.y()) < 16.0);
    const double edge_degrees =
        std::atan2(line.quad[1].y - line.quad[0].y, line.quad[1].x - line.quad[0].x) * 180.0 / 3.14159265358979323846;
    CHECK(std::abs(edge_degrees - kAngle) < 1.5);
    // The axis-aligned box bounds the turned quad: a 56 px line some 500 px long, turned 8
    // degrees, stands about 120 px tall.
    CHECK(line.box.width >= 400.0 && line.box.height > 90.0);
  };
  check_line(result.lines[0], first_rect);
  check_line(result.lines[1], second_rect);
}

void ui_ocr_windows_engine_reads_a_tall_page_in_bands_if_available() {
  const ScopedRealOcrEngines real;
  const auto opened = windows_engine_with_english();

  // Taller than the engine's limit (2600 on current Windows), so it is read in bands;
  // some lines sit inside the overlaps. Each numbered line must come back once, in order.
  constexpr int kLines = 20;
  constexpr int kPitch = 300;
  QImage image(1800, kLines * kPitch + 100, QImage::Format_RGBA8888);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::black);
    painter.setFont(test_font(60));
    for (int index = 0; index < kLines; ++index) {
      painter.drawText(QRect(120, 60 + index * kPitch, 1500, 90), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("Row %1 of the tall page").arg(index + 101));
    }
  }
  const auto result = opened.engine->open(opened.language)->recognize(view_of(image));
  CHECK(static_cast<int>(result.lines.size()) == kLines);
  for (int index = 0; index < std::min<int>(kLines, static_cast<int>(result.lines.size())); ++index) {
    const auto& line = result.lines[static_cast<std::size_t>(index)];
    CHECK(line.text.find(std::to_string(index + 101)) != std::string::npos);
    CHECK(std::abs(line.box.y + line.box.height / 2.0 - (105.0 + index * kPitch)) < 30.0);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> ocr_ui_tests() {
  return {
      {"ui_ocr_windows_engine_reads_rendered_text_if_available", ui_ocr_windows_engine_reads_rendered_text_if_available},
      {"ui_ocr_windows_engine_reports_rotated_text_if_available",
       ui_ocr_windows_engine_reports_rotated_text_if_available},
      {"ui_ocr_windows_engine_reads_a_tall_page_in_bands_if_available",
       ui_ocr_windows_engine_reads_a_tall_page_in_bands_if_available},
  };
}
