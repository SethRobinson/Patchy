// The downloadable text recognition engine against its real models (docs/ocr-engines.md,
// "PP-OCR"): text drawn in-test and, when the benchmark pages are on this machine, the
// same pages the Python reference pipeline was measured on (docs/ocr-benchmark.md). Both
// skip where the models are not staged (build/ai-models-download, by scripts/setup-ai-deps).

#include "ai/ocr_model_engine.hpp"
#include "ai/ocr_models.hpp"
#include "ai/segmentation_engine.hpp"
#include "local_psd_fixtures.hpp"
#include "ocr/ocr_engine.hpp"
#include "ocr_test_support.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui/qt_paths.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

// Points the engine at the build's staged downloads for a test, then back at nothing.
struct StagedModels {
  StagedModels() { patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-download"); }
  ~StagedModels() {
    patchy::ai::release_ocr_models();
    patchy::ai::set_downloaded_model_directory({});
  }
  StagedModels(const StagedModels&) = delete;
  StagedModels& operator=(const StagedModels&) = delete;
};

std::shared_ptr<patchy::ocr::Engine> staged_engine() {
  const auto engine = patchy::ai::ocr_model_engine();
  if (engine == nullptr) {
    patchy::test::skip(patchy::test::SkipKind::platform, "this build cannot run the text recognition models");
  }
  if (engine->unavailable() != patchy::ocr::Unavailable::None) {
    patchy::test::skip(patchy::test::SkipKind::feature,
                       "the text recognition models are not staged: " + engine->unavailable_reason());
  }
  return engine;
}

patchy::ocr::ImageView view_of(const QImage& image) {
  return patchy::ocr::ImageView{image.constBits(), image.width(), image.height(), image.bytesPerLine(),
                                patchy::ocr::PixelLayout::Rgba8};
}

QString squeezed(const QString& text) {
  QString out;
  for (const auto ch : text) {
    if (!ch.isSpace()) {
      out.append(ch);
    }
  }
  return out;
}

int edit_distance(const QString& a, const QString& b) {
  std::vector<int> previous(static_cast<std::size_t>(b.size()) + 1U);
  std::vector<int> current(previous.size());
  for (std::size_t j = 0; j < previous.size(); ++j) {
    previous[j] = static_cast<int>(j);
  }
  for (qsizetype i = 1; i <= a.size(); ++i) {
    current[0] = static_cast<int>(i);
    for (qsizetype j = 1; j <= b.size(); ++j) {
      const auto at = static_cast<std::size_t>(j);
      current[at] = std::min({previous[at] + 1, current[at - 1] + 1, previous[at - 1] + (a[i - 1] != b[j - 1] ? 1 : 0)});
    }
    std::swap(previous, current);
  }
  return previous.back();
}

// Share of `truth` the engine got wrong, whitespace aside.
double error_rate(const QString& truth, const QString& read) {
  const auto expected = squeezed(truth);
  return expected.isEmpty() ? 0.0 : static_cast<double>(edit_distance(expected, squeezed(read))) / expected.size();
}

QString text_of(const patchy::ocr::Result& result) {
  const auto text = result.text();
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QFont font_for(const QString& sample, QFontDatabase::WritingSystem system, int pixel_size) {
  QStringList families{QStringLiteral("Arial")};
  families << QFontDatabase::families(system);
  for (const auto& family : families) {
    QFont candidate(family);
    candidate.setPixelSize(pixel_size);
    candidate.setStyleStrategy(QFont::PreferAntialias);
    const QFontMetrics metrics(candidate);
    if (std::all_of(sample.begin(), sample.end(), [&](QChar ch) { return ch.isSpace() || metrics.inFont(ch); })) {
      return candidate;
    }
  }
  patchy::test::skip(patchy::test::SkipKind::feature, "no font to draw the test text with");
}

// Draws `lines` one under another from (40, 40); returns the image and each line's top.
QImage drawn_lines(const QStringList& lines, const QFont& font, std::vector<int>* tops = nullptr) {
  const QFontMetrics metrics(font);
  int width = 0;
  for (const auto& line : lines) {
    width = std::max(width, metrics.horizontalAdvance(line));
  }
  const int pitch = metrics.height() + metrics.height() / 2;
  QImage image(width + 80, static_cast<int>(lines.size()) * pitch + 80, QImage::Format_RGBA8888);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setFont(font);
  painter.setPen(Qt::black);
  for (qsizetype index = 0; index < lines.size(); ++index) {
    const int top = 40 + static_cast<int>(index) * pitch;
    if (tops != nullptr) {
      tops->push_back(top);
    }
    painter.drawText(40, top + metrics.ascent(), lines[index]);
  }
  return image;
}

void ui_ocr_model_engine_reads_rendered_text_if_available() {
  const StagedModels staged;
  const auto engine = staged_engine();
  CHECK(engine->id() == "ppocr");
  // Every pick names files the engine knows, and the list of files holds each once.
  const auto files = patchy::ai::ocr_model_files();
  CHECK(files.size() == 19);
  for (const auto& language : patchy::ai::ocr_model_languages()) {
    CHECK(std::find(files.begin(), files.end(), language.recognizer) != files.end());
    CHECK(std::find(files.begin(), files.end(), language.dictionary) != files.end());
  }
  CHECK(engine->languages().size() == patchy::ai::ocr_model_languages().size());

  // English: three lines, read in order, each on its drawn place, with words left to right.
  {
    const QStringList lines{QStringLiteral("The quick brown fox jumps over"), QStringLiteral("the lazy dog while seventeen"),
                            QStringLiteral("wizards quietly judge the box.")};
    std::vector<int> tops;
    const auto font = font_for(lines.join(QString()), QFontDatabase::Latin, 40);
    const QImage image = drawn_lines(lines, font, &tops);
    const auto result = engine->open("en")->recognize(view_of(image));
    CHECK(result.lines.size() == 3);
    CHECK(error_rate(lines.join(QString()), text_of(result)) <= 0.02);
    if (result.lines.size() == 3) {
      const QFontMetrics metrics(font);
      for (std::size_t index = 0; index < 3; ++index) {
        const auto& line = result.lines[index];
        CHECK(!line.vertical);
        CHECK(std::abs(line.angle_degrees) < 1.0);
        // The box holds the drawn line and not its neighbours.
        CHECK(line.box.y < tops[index] + metrics.ascent() && line.box.y + line.box.height > tops[index] + metrics.ascent());
        CHECK(line.box.height < 2.0 * metrics.height());
        CHECK(line.box.x < 60.0 && line.box.x + line.box.width > 40.0 + 0.9 * metrics.horizontalAdvance(lines[static_cast<qsizetype>(index)]));
        CHECK(line.words.size() >= 4);
        for (std::size_t word = 1; word < line.words.size(); ++word) {
          CHECK(line.words[word].box.x > line.words[word - 1].box.x);
        }
      }
    }
  }

  // Russian through the Cyrillic recognizer: the pick decides the recognizer.
  {
    const QStringList lines{QString::fromUtf8("\xD0\xA1\xD1\x8A\xD0\xB5\xD1\x88\xD1\x8C \xD0\xB6\xD0\xB5 \xD0\xB5\xD1\x89\xD1\x91 "
                                              "\xD1\x8D\xD1\x82\xD0\xB8\xD1\x85 \xD0\xBC\xD1\x8F\xD0\xB3\xD0\xBA\xD0\xB8\xD1\x85"),
                            QString::fromUtf8("\xD1\x84\xD1\x80\xD0\xB0\xD0\xBD\xD1\x86\xD1\x83\xD0\xB7\xD1\x81\xD0\xBA\xD0\xB8\xD1\x85 "
                                              "\xD0\xB1\xD1\x83\xD0\xBB\xD0\xBE\xD0\xBA, \xD0\xB4\xD0\xB0 \xD0\xB2\xD1\x8B\xD0\xBF\xD0\xB5\xD0\xB9 "
                                              "\xD1\x87\xD0\xB0\xD1\x8E.")};
    const auto font = font_for(lines.join(QString()), QFontDatabase::Cyrillic, 40);
    const QImage image = drawn_lines(lines, font);
    const auto result = engine->open("ru")->recognize(view_of(image));
    CHECK(result.lines.size() == 2);
    CHECK(error_rate(lines.join(QString()), text_of(result)) <= 0.05);
  }

  // Japanese, level and as a column (glyphs stacked upright: the column comes back marked
  // vertical and reads top to bottom).
  {
    patchy::test::register_test_fonts(patchy::test::TestFontRole::JapaneseGothic);
    const QStringList lines{QString::fromUtf8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE6\x96\x87\xE5\xAD\x97\xE3\x82\x92"
                                              "\xE8\xAA\x8D\xE8\xAD\x98\xE3\x81\x99\xE3\x82\x8B"),
                            QString::fromUtf8("\xE6\xA8\xAA\xE6\x9B\xB8\xE3\x81\x8D\xE3\x81\xAE\xE6\x96\x87\xE7\xAB\xA0\xE3\x82\x92"
                                              "\xE8\xAA\xAD\xE3\x81\xBF\xE5\x8F\x96\xE3\x82\x8A\xE3\x81\xBE\xE3\x81\x99")};
    const auto font = font_for(lines.join(QString()), QFontDatabase::Japanese, 44);
    const auto recognizer = engine->open("ja");
    const QImage level = drawn_lines(lines, font);
    const auto level_result = recognizer->recognize(view_of(level));
    CHECK(level_result.lines.size() == 2);
    CHECK(error_rate(lines.join(QString()), text_of(level_result)) <= 0.05);

    const QString column = QString::fromUtf8("\xE7\xB8\xA6\xE6\x9B\xB8\xE3\x81\x8D\xE3\x81\xAE\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
                                             "\xE6\x96\x87\xE5\xAD\x97\xE8\xAA\x8D\xE8\xAD\x98");
    const QFontMetrics metrics(font);
    QImage image(240, 80 + static_cast<int>(column.size()) * 52, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    {
      QPainter painter(&image);
      painter.setRenderHint(QPainter::TextAntialiasing);
      painter.setFont(font);
      painter.setPen(Qt::black);
      for (qsizetype index = 0; index < column.size(); ++index) {
        painter.drawText(QRect(90, 40 + static_cast<int>(index) * 52, 60, 52), Qt::AlignCenter, column.mid(index, 1));
      }
    }
    const auto column_result = recognizer->recognize(view_of(image));
    CHECK(column_result.lines.size() == 1);
    if (column_result.lines.size() == 1) {
      CHECK(column_result.lines[0].vertical);
      CHECK(column_result.lines[0].box.height > 4.0 * column_result.lines[0].box.width);
      CHECK(error_rate(column, text_of(column_result)) <= 0.2);
    }
  }

  // A blank page is an empty result; a cancel requested beforehand stops before any run.
  {
    QImage blank(400, 300, QImage::Format_RGBA8888);
    blank.fill(Qt::white);
    const auto recognizer = engine->open("en");
    CHECK(recognizer->recognize(view_of(blank)).lines.empty());
    patchy::ocr::Cancel cancel;
    cancel.request();
    bool cancelled = false;
    try {
      (void)recognizer->recognize(view_of(blank), &cancel);
    } catch (const patchy::ocr::RecognitionCancelled&) {
      cancelled = true;
    }
    CHECK(cancelled);
  }
  bool refused = false;
  try {
    (void)engine->open("xx");
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);
}

// The benchmark pages through the C++ engine: the same text the reference pipeline read,
// within a margin of its error rates (docs/ocr-benchmark.md), and the time a page takes.
void ui_ocr_model_engine_matches_the_reference_on_benchmark_pages_if_available() {
  const auto pages_dir = QDir(patchy::ui::to_qstring(patchy::test::local_fixtures_root() / "ocr-bench"));
  QFile described(pages_dir.filePath(QStringLiteral("pages.json")));
  if (!described.open(QIODevice::ReadOnly)) {
    patchy::test::skip(patchy::test::SkipKind::fixture,
                       "the benchmark pages are not generated (scripts/dev/ocr-bench/make_pages.py)");
  }
  const StagedModels staged;
  const auto engine = staged_engine();
  // Allowed error per page, percent: the reference's worst clean 300 ppi figure plus room.
  const auto allowed = [](const QString& name) {
    if (name.startsWith(QStringLiteral("ja_v"))) {
      return 16.0;  // the model drops small marks in vertical text
    }
    if (name.startsWith(QStringLiteral("latin"))) {
      return 3.5;
    }
    return 1.5;
  };
  const auto language_tag = [](const QString& language) {
    return language == QStringLiteral("latin") ? std::string("latin") : language.toStdString();
  };
  const auto pages = QJsonDocument::fromJson(described.readAll()).object().value(QStringLiteral("pages")).toArray();
  CHECK(!pages.isEmpty());
  for (const auto& value : pages) {
    const auto page = value.toObject();
    const auto name = page.value(QStringLiteral("name")).toString();
    QImage image(pages_dir.filePath(name + QStringLiteral(".png")));
    if (image.isNull()) {
      continue;
    }
    image = image.convertToFormat(QImage::Format_RGBA8888);
    QString truth;
    const auto lines = page.value(QStringLiteral("lines")).toArray();
    for (const auto& line : lines) {
      truth += line.toString();
    }
    const auto recognizer = engine->open(language_tag(page.value(QStringLiteral("language")).toString()));
    QElapsedTimer timer;
    timer.start();
    const auto result = recognizer->recognize(view_of(image));
    const auto elapsed = timer.elapsed();
    const double rate = 100.0 * error_rate(truth, text_of(result));
    std::fprintf(stderr, "  ocr benchmark page %-16s error %6.2f%%  lines %3zu/%-3d  %5lld ms\n",
                 name.toUtf8().constData(), rate, result.lines.size(), static_cast<int>(lines.size()),
                 static_cast<long long>(elapsed));
    CHECK(rate <= allowed(name));
    CHECK(result.lines.size() == static_cast<std::size_t>(lines.size()));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> ocr_model_engine_ui_tests() {
  return {
      {"ui_ocr_model_engine_reads_rendered_text_if_available", ui_ocr_model_engine_reads_rendered_text_if_available},
      {"ui_ocr_model_engine_matches_the_reference_on_benchmark_pages_if_available",
       ui_ocr_model_engine_matches_the_reference_on_benchmark_pages_if_available},
  };
}
