// Recognized text as layers and the script calls that make them (docs/ocr.md, "Text
// layers" and "Scripting"). The scripted fake engine "recognizes" the boxes of text these
// tests draw themselves, so every expectation is against known pixels: the layers land on
// the drawn text, take its size, colour and slant, and group into blocks.

#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_page_fixture.hpp"
#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/ocr_text_layers.hpp"
#include "ui/pdf_recognized_text.hpp"
#include "ui/script_engine.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::ocr::Box;
using patchy::ocr::FakeEngineConfig;
using patchy::ocr::FakeLine;
using patchy::test::ui::ScopedOcrEngines;
using patchy::test::ui::show_window;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::ScriptEngineHost;

struct ScriptRun {
  bool failed{false};
  QJsonValue result;
  QString messages;
};

ScriptRun run(MainWindow& window, const QString& code) {
  auto& host = window.script_engine_host();
  ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("ocr-test");
  options.unattended = true;
  // False when the script failed before its first await: the error is in the backlog.
  const bool started = host.run_source(code, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 30000) {
    QApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  CHECK(!host.run_active());
  ScriptRun outcome;
  outcome.failed = !started || host.last_run_had_error();
  outcome.result = host.last_result();
  outcome.messages = host.message_backlog().join(QLatin1Char('\n'));
  return outcome;
}

// A script that must succeed; its patchy.setResult value.
QJsonObject run_ok(MainWindow& window, const QString& code) {
  const auto outcome = run(window, code);
  if (outcome.failed) {
    std::fprintf(stderr, "%s\n", outcome.messages.toUtf8().constData());
  }
  CHECK(!outcome.failed);
  return outcome.result.toObject();
}

QFont arial(int pixel_size, bool italic = false) {
  return patchy::test::ui::ocr_test_font(pixel_size, italic);
}

using Page = patchy::test::ui::OcrTestPage;

std::shared_ptr<patchy::ocr::FakeEngine> fake_for(const Page& page) {
  return patchy::test::ui::ocr_fake_for(page);
}

patchy::LayerId add_scan_layer(MainWindow& window, const QImage& image, QPoint at) {
  return patchy::test::ui::add_ocr_scan_layer(window, image, at);
}

const patchy::Layer* recognized_group(const patchy::Document& document) {
  return patchy::test::ui::recognized_text_group(document);
}

std::string metadata_value(const patchy::Layer& layer, const char* key) {
  const auto found = layer.metadata().find(key);
  return found == layer.metadata().end() ? std::string() : found->second;
}

// Where a text layer's ink sits in the document.
QRect layer_ink(const patchy::Layer& layer) {
  const auto visible = patchy::visible_alpha_local_bounds(layer.pixels());
  if (!visible.has_value()) {
    return QRect();
  }
  return QRect(layer.bounds().x + visible->x, layer.bounds().y + visible->y, visible->width, visible->height);
}

bool near(double a, double b, double tolerance) {
  return std::abs(a - b) <= tolerance;
}

const QString kRecognizeScan = QStringLiteral("var doc = app.activeDocument; var layer = doc.findLayer('Scan');");

void ui_script_ocr_lists_engines_and_languages() {
  MainWindow window;
  show_window(window);
  // The suite starts with no engine: nothing is available and the list is empty.
  auto none = run_ok(window, QStringLiteral("patchy.setResult({count: patchy.ocr.engines().length, "
                                            "available: patchy.ocr.available});"));
  CHECK(none.value(QStringLiteral("count")).toInt(-1) == 0);
  CHECK(!none.value(QStringLiteral("available")).toBool(true));

  FakeEngineConfig bare;
  bare.id = "bare";
  bare.languages.clear();
  const Page page(100, 100);
  const ScopedOcrEngines engines({fake_for(page), patchy::ocr::make_fake_engine(bare)});
  const auto listed = run_ok(window, QStringLiteral("var e = patchy.ocr.engines();"
                                                    "patchy.setResult({count: e.length, first: e[0], second: e[1], "
                                                    "available: patchy.ocr.available});"));
  CHECK(listed.value(QStringLiteral("count")).toInt() == 2);
  CHECK(listed.value(QStringLiteral("available")).toBool());
  const auto first = listed.value(QStringLiteral("first")).toObject();
  CHECK(first.value(QStringLiteral("id")).toString() == QStringLiteral("fake"));
  CHECK(first.value(QStringLiteral("available")).toBool());
  CHECK(first.value(QStringLiteral("unavailableReason")).toString().isEmpty());
  CHECK(first.value(QStringLiteral("defaultLanguage")).toString() == QStringLiteral("en-US"));
  const auto languages = first.value(QStringLiteral("languages")).toArray();
  CHECK(languages.size() == 2);
  CHECK(languages.at(1).toObject().value(QStringLiteral("tag")).toString() == QStringLiteral("ja"));
  CHECK(languages.at(1).toObject().value(QStringLiteral("name")).toString() == QStringLiteral("Japanese"));
  // An engine with no language is listed, unavailable, with its reason.
  const auto second = listed.value(QStringLiteral("second")).toObject();
  CHECK(second.value(QStringLiteral("id")).toString() == QStringLiteral("bare"));
  CHECK(!second.value(QStringLiteral("available")).toBool(true));
  CHECK(!second.value(QStringLiteral("unavailableReason")).toString().isEmpty());
}

void ui_script_recognize_text_returns_lines_in_document_pixels() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 900, 600);
  Page page(600, 300);
  const QRect first = page.draw(QStringLiteral("Hello recognized world"), 40, 30, arial(36));
  const QRect second = page.draw(QStringLiteral("Second line"), 40, 190, arial(36));
  const auto fake = fake_for(page);
  const ScopedOcrEngines engines({fake});
  add_scan_layer(window, page.image, QPoint(100, 50));

  // The layer on its own: the engine sees the layer's pixels, the script gets document
  // coordinates. Nothing is added by default.
  auto result = run_ok(window, kRecognizeScan + QStringLiteral("patchy.setResult(layer.recognizeText());"));
  CHECK(result.value(QStringLiteral("engine")).toString() == QStringLiteral("fake"));
  CHECK(result.value(QStringLiteral("language")).toString() == QStringLiteral("en-US"));
  CHECK(result.value(QStringLiteral("text")).toString() == QStringLiteral("Hello recognized world\nSecond line"));
  CHECK(result.value(QStringLiteral("group")).isNull());
  auto lines = result.value(QStringLiteral("lines")).toArray();
  CHECK(lines.size() == 2);
  if (lines.size() == 2) {
    const auto line = lines.at(0).toObject();
    CHECK(near(line.value(QStringLiteral("x")).toDouble(), first.x() + 100.0, 0.01));
    CHECK(near(line.value(QStringLiteral("y")).toDouble(), first.y() + 50.0, 0.01));
    CHECK(near(line.value(QStringLiteral("width")).toDouble(), first.width(), 0.01));
    CHECK(near(line.value(QStringLiteral("height")).toDouble(), first.height(), 0.01));
    CHECK(!line.value(QStringLiteral("vertical")).toBool(true));
    const auto words = line.value(QStringLiteral("words")).toArray();
    CHECK(words.size() == 3);
    if (words.size() == 3) {
      CHECK(words.at(2).toObject().value(QStringLiteral("text")).toString() == QStringLiteral("world"));
      CHECK(words.at(0).toObject().value(QStringLiteral("x")).toDouble() >= first.x() + 100.0 - 0.01);
    }
    CHECK(near(lines.at(1).toObject().value(QStringLiteral("y")).toDouble(), second.y() + 50.0, 0.01));
  }
  CHECK(recognized_group(MainWindowTestAccess::document(window)) == nullptr);
  const auto calls = fake->calls();
  CHECK(calls.size() == 1);
  if (calls.size() == 1) {
    CHECK(calls[0].language == "en-US");
    CHECK(calls[0].image_width == 600 && calls[0].image_height == 300);
  }

  // A rectangle reads only that part; a named language is the one opened.
  result = run_ok(window, kRecognizeScan + QStringLiteral("patchy.setResult(layer.recognizeText({language: 'ja', "
                                                          "rect: {x: 100, y: 50, width: 600, height: 150}}));"));
  CHECK(result.value(QStringLiteral("language")).toString() == QStringLiteral("ja"));
  CHECK(result.value(QStringLiteral("lines")).toArray().size() == 1);
  CHECK(fake->calls().size() == 2);
  if (fake->calls().size() == 2) {
    CHECK(fake->calls()[1].image_width == 600 && fake->calls()[1].image_height == 150);
    CHECK(fake->calls()[1].language == "ja");
  }

  // The whole image as composited: the canvas, so the scripted boxes arrive unshifted.
  result = run_ok(window, QStringLiteral("patchy.setResult(app.activeDocument.recognizeText());"));
  lines = result.value(QStringLiteral("lines")).toArray();
  CHECK(lines.size() == 2);
  if (lines.size() == 2) {
    CHECK(near(lines.at(0).toObject().value(QStringLiteral("x")).toDouble(), first.x(), 0.01));
  }
  CHECK(fake->calls().size() == 3);
  if (fake->calls().size() == 3) {
    CHECK(fake->calls()[2].image_width == 900 && fake->calls()[2].image_height == 600);
  }
}

void ui_script_recognize_text_creates_layers_in_one_undo_step() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 900, 600);
  Page page(700, 400);
  page.draw(QStringLiteral("First heading"), 30, 20, arial(40));
  page.draw(QStringLiteral("A separate note far below"), 30, 300, arial(28));
  const ScopedOcrEngines engines({fake_for(page)});
  const auto scan_id = add_scan_layer(window, page.image, QPoint(60, 80));
  const auto layers_before = MainWindowTestAccess::document(window).layers().size();

  const auto result = run_ok(
      window, kRecognizeScan + QStringLiteral("var r = layer.recognizeText({layers: 'visible', font: 'Arial'});"
                                              "patchy.setResult({name: r.group.name, kind: r.group.kind, "
                                              "children: r.group.children.length});"));
  CHECK(result.value(QStringLiteral("name")).toString() == QStringLiteral("Recognized Text"));
  CHECK(result.value(QStringLiteral("children")).toInt() == 2);

  auto& document = MainWindowTestAccess::document(window);
  CHECK(document.layers().size() == layers_before + 1);
  const auto* group = recognized_group(std::as_const(document));
  CHECK(group != nullptr);
  if (group == nullptr) {
    return;
  }
  // Above the layer it was read from, visible, Pass Through, tagged with engine and language.
  CHECK(std::as_const(document).layers().back().id() == group->id());
  CHECK(group->visible());
  CHECK(group->blend_mode() == patchy::BlendMode::PassThrough);
  CHECK(metadata_value(*group, patchy::kLayerMetadataOcrGroup) == "fake en-US");
  CHECK(group->children().size() == 2);
  if (group->children().size() == 2) {
    // Reading order from the top of the stack: the first line is the last child.
    const auto& top = group->children().back();
    const auto& bottom = group->children().front();
    CHECK(patchy::layer_is_text(top) && patchy::layer_is_text(bottom));
    CHECK(metadata_value(top, patchy::kLayerMetadataText) == "First heading");
    CHECK(metadata_value(bottom, patchy::kLayerMetadataText) == "A separate note far below");
    // Where its one line was found: the drawn line's box, for a searchable PDF export.
    const auto places =
        patchy::ui::parse_recognized_line_places(metadata_value(top, patchy::kLayerMetadataOcrLine));
    CHECK(places.has_value() && places->places.size() == 1);
    CHECK(places.has_value() && places->layer_bounds == top.bounds());
    CHECK(metadata_value(top, patchy::kLayerMetadataTextFont) == "Arial");
    CHECK(metadata_value(top, patchy::kLayerMetadataTextFlow) == "point");
    CHECK(top.name() == "First heading");
    CHECK(!top.pixels().empty());
  }
  // The source layer is untouched and still shown.
  const auto* scan = std::as_const(document).find_layer(scan_id);
  CHECK(scan != nullptr && scan->visible());

  // One undo step takes the whole group away.
  MainWindowTestAccess::undo(window);
  CHECK(recognized_group(std::as_const(MainWindowTestAccess::document(window))) == nullptr);
  CHECK(MainWindowTestAccess::document(window).layers().size() == layers_before);
}

void ui_script_recognize_text_options_are_strict() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 400, 300);
  Page page(300, 200);
  page.draw(QStringLiteral("Strict options"), 20, 20, arial(30));
  const auto fake = fake_for(page);
  add_scan_layer(window, page.image, QPoint(0, 0));
  const auto layers_before = MainWindowTestAccess::document(window).layers().size();
  const auto refused = [&](const QString& options, const QString& needle) {
    const auto outcome = run(window, kRecognizeScan + QStringLiteral("layer.recognizeText(%1);").arg(options));
    CHECK(outcome.failed);
    if (!outcome.messages.contains(needle)) {
      std::fprintf(stderr, "expected \"%s\" in: %s\n", needle.toUtf8().constData(),
                   outcome.messages.toUtf8().constData());
    }
    CHECK(outcome.messages.contains(needle));
  };

  // No engine on this system: the call says so.
  refused(QStringLiteral("{layers: 'visible'}"), QStringLiteral("No text recognition engine"));

  const ScopedOcrEngines engines({fake});
  refused(QStringLiteral("{layer: 'visible'}"), QStringLiteral("recognizeText"));
  refused(QStringLiteral("{layers: 'everything'}"), QStringLiteral("layers"));
  refused(QStringLiteral("{grouping: 'paragraphs'}"), QStringLiteral("grouping"));
  refused(QStringLiteral("{color: 'reddish'}"), QStringLiteral("color"));
  refused(QStringLiteral("{engine: 'nope', layers: 'visible'}"), QStringLiteral("nope"));
  refused(QStringLiteral("{language: 'xx-XX', layers: 'visible'}"), QStringLiteral("xx-XX"));
  refused(QStringLiteral("{rect: {x: 5000, y: 5000, width: 10, height: 10}, layers: 'visible'}"),
          QStringLiteral("nothing in that area"));
  // Every refusal came before anything was read or changed.
  CHECK(fake->calls().empty());
  CHECK(MainWindowTestAccess::document(window).layers().size() == layers_before);
  CHECK(recognized_group(std::as_const(MainWindowTestAccess::document(window))) == nullptr);
}

void ui_script_recognize_text_runs_unattended() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 500, 300);
  Page page(400, 200);
  page.draw(QStringLiteral("Slow engine"), 20, 20, arial(32));
  FakeEngineConfig config;
  config.lines = page.lines;
  config.delay_ms = 150;  // the call waits on a worker with the watchdog fed
  const auto fake = patchy::ocr::make_fake_engine(config);
  const ScopedOcrEngines engines({fake});
  add_scan_layer(window, page.image, QPoint(10, 10));
  const auto result =
      run_ok(window, kRecognizeScan + QStringLiteral("var r = layer.recognizeText({layers: 'hidden', font: 'Arial'});"
                                                      "patchy.setResult({text: r.text, visible: r.group.visible});"));
  CHECK(result.value(QStringLiteral("text")).toString() == QStringLiteral("Slow engine"));
  CHECK(!result.value(QStringLiteral("visible")).toBool(true));
  CHECK(fake->calls().size() == 1);

  // An engine that fails mid-run is a script error with its message, and nothing changes.
  FakeEngineConfig failing;
  failing.fail_with = "the engine fell over";
  const ScopedOcrEngines broken({patchy::ocr::make_fake_engine(failing)});
  const auto outcome = run(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'visible'});"));
  CHECK(outcome.failed);
  CHECK(outcome.messages.contains(QStringLiteral("the engine fell over")));
}

void ui_recognize_text_fits_lines_to_boxes() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 1000, 700);
  Page page(900, 600);
  const QRect large = page.draw(QStringLiteral("Large heading text"), 40, 30, arial(64));
  const QRect body = page.draw(QStringLiteral("Body text set quite a bit smaller"), 40, 260, arial(24));
  const QRect number = page.draw(QStringLiteral("7"), 800, 520, arial(30));
  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(50, 40));
  (void)run_ok(window, kRecognizeScan +
                           QStringLiteral("layer.recognizeText({layers: 'visible', grouping: 'lines', font: 'Arial'});"));
  const auto* group = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
  CHECK(group != nullptr && group->children().size() == 3);
  if (group == nullptr || group->children().size() != 3) {
    return;
  }
  const auto check = [&](const patchy::Layer& layer, const QRect& drawn, int size, int size_tolerance) {
    const int fitted = std::atoi(metadata_value(layer, patchy::kLayerMetadataTextSize).c_str());
    if (std::abs(fitted - size) > size_tolerance) {
      std::fprintf(stderr, "size %d, expected about %d\n", fitted, size);
    }
    CHECK(std::abs(fitted - size) <= size_tolerance);
    // The layer's ink sits on the drawn text.
    const QRect ink = layer_ink(layer);
    const QRect expected = drawn.translated(50, 40);
    CHECK(near(ink.center().x(), expected.center().x(), 3.0));
    CHECK(near(ink.center().y(), expected.center().y(), 3.0));
    // The size is a whole number of pixels, so a 24 px line can be off by half a pixel in
    // 24 (2%) before any difference between the two text renderers.
    const double width_tolerance = std::max(4.0, expected.width() * 0.035);
    if (!near(ink.width(), expected.width(), width_tolerance)) {
      std::fprintf(stderr, "ink %d wide, drawn %d wide, size %d\n", ink.width(), expected.width(), fitted);
      // The same block built directly, to tell a wrong fit from a layer redrawn afterwards.
      patchy::ocr::Result direct;
      for (const auto& scripted : page.lines) {
        patchy::ocr::Line line;
        line.text = scripted.text;
        line.box = scripted.box;
        line.quad = patchy::ocr::quad_from_box(scripted.box);
        direct.lines.push_back(line);
      }
      patchy::ui::OcrLayerStyle style;
      style.family = QStringLiteral("Arial");
      for (const auto& built : patchy::ui::build_recognized_text_layers(direct, page.image, QPoint(50, 40),
                                                                        patchy::ui::OcrGrouping::Lines, style)) {
        const auto built_ink = patchy::visible_alpha_local_bounds(built.pixels);
        std::fprintf(stderr, "direct \"%s\": size %d, ink %d wide, buffer %d wide, family %s\n",
                     built.settings.text.toUtf8().constData(), built.settings.size,
                     built_ink.has_value() ? built_ink->width : -1, built.pixels.width(),
                     QFontInfo(QFont(built.settings.family)).family().toUtf8().constData());
      }
      std::fprintf(stderr, "layer buffer %d wide, QPainter font resolves to %s\n", layer.pixels().width(),
                   QFontInfo(arial(64)).family().toUtf8().constData());
    }
    CHECK(near(ink.width(), expected.width(), width_tolerance));
  };
  // Top of the stack first: the heading, the body line, then the lone digit (too short to
  // fit by width, so its box height decides). The size is whatever makes Patchy's own
  // text drawing as long as the drawn line, which can differ a little from the size the
  // test drew at (this painter may hint its glyphs, the text pipeline does not): the ink
  // landing on the drawn pixels is the real check.
  check(group->children()[2], large, 64, 3);
  check(group->children()[1], body, 24, 2);
  const int digit_size = std::atoi(metadata_value(group->children()[0], patchy::kLayerMetadataTextSize).c_str());
  CHECK(digit_size >= 18 && digit_size <= 42);
  const QRect digit_ink = layer_ink(group->children()[0]);
  CHECK(near(digit_ink.center().x(), number.center().x() + 50.0, 3.0));
  CHECK(near(digit_ink.center().y(), number.center().y() + 40.0, 3.0));
  // Black text on white paper samples as near black.
  CHECK(QColor(QString::fromStdString(metadata_value(group->children()[2], patchy::kLayerMetadataTextColor)))
            .lightness() < 60);
}

void ui_recognize_text_one_size_per_block() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 900, 600);
  Page page(800, 500);
  // A paragraph: three lines of one size, 50 px apart, left aligned, of different lengths.
  const QRect first = page.draw(QStringLiteral("The first line of the paragraph"), 60, 60, arial(36));
  page.draw(QStringLiteral("a shorter second"), 60, 110, arial(36));
  const QRect third = page.draw(QStringLiteral("and the third one to end it"), 60, 160, arial(36));
  // A caption well below it, centred under nothing: its own block.
  page.draw(QStringLiteral("Caption"), 300, 400, arial(22));
  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(0, 0));
  (void)run_ok(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'visible', font: 'Arial'});"));
  const auto* group = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
  CHECK(group != nullptr && group->children().size() == 2);
  if (group == nullptr || group->children().size() != 2) {
    return;
  }
  const auto& paragraph = group->children()[1];  // first in reading order: on top
  const auto& caption = group->children()[0];
  // One layer holds the three lines, with their line breaks kept.
  CHECK(metadata_value(paragraph, patchy::kLayerMetadataText) ==
        "The first line of the paragraph\na shorter second\nand the third one to end it");
  CHECK(metadata_value(caption, patchy::kLayerMetadataText) == "Caption");
  // One size for the block.
  const int size = std::atoi(metadata_value(paragraph, patchy::kLayerMetadataTextSize).c_str());
  CHECK(std::abs(size - 36) <= 3);
  CHECK(std::abs(std::atoi(metadata_value(caption, patchy::kLayerMetadataTextSize).c_str()) - 22) <= 2);
  // The measured line pitch is the layer's leading, laid out under the leading model.
  CHECK(metadata_value(paragraph, patchy::kLayerMetadataTextLayoutMode) == patchy::kTextLayoutModePhotoshop);
  CHECK(metadata_value(caption, patchy::kLayerMetadataTextLayoutMode).empty());
  const QRect ink = layer_ink(paragraph);
  const QRect drawn = first.united(third);
  CHECK(near(ink.top(), drawn.top(), 4.0));
  CHECK(near(ink.bottom(), drawn.bottom(), 4.0));
  CHECK(near(ink.left(), drawn.left(), 4.0));
  CHECK(near(ink.width(), drawn.width(), 12.0));
  CHECK(metadata_value(paragraph, patchy::kLayerMetadataTextParagraphRuns).find("left") != std::string::npos);
}

void ui_recognize_text_keeps_line_breaks_in_blocks() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 900, 500);
  Page page(800, 400);
  // Centred lines: each is drawn about x = 400.
  const QFont font = arial(34);
  const QFontMetrics metrics(font);
  const QStringList texts = {QStringLiteral("A centred heading line"), QStringLiteral("short"),
                             QStringLiteral("and a middling third")};
  QRect all;
  for (qsizetype index = 0; index < texts.size(); ++index) {
    all = all.united(page.draw(texts[index], 400 - metrics.horizontalAdvance(texts[index]) / 2,
                               60 + static_cast<int>(index) * 46, font));
  }
  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(0, 0));
  (void)run_ok(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'visible', font: 'Arial'});"));
  const auto* group = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
  CHECK(group != nullptr && group->children().size() == 1);
  if (group == nullptr || group->children().size() != 1) {
    return;
  }
  const auto& block = group->children()[0];
  CHECK(metadata_value(block, patchy::kLayerMetadataText) == texts.join(QLatin1Char('\n')).toStdString());
  // The lines share their centres, so the block is centred and every line lands on its
  // drawn place, not just the widest one.
  CHECK(metadata_value(block, patchy::kLayerMetadataTextParagraphRuns).find("center") != std::string::npos);
  const QRect ink = layer_ink(block);
  CHECK(near(ink.center().x(), all.center().x(), 3.0));
  CHECK(near(ink.center().y(), all.center().y(), 4.0));
  CHECK(near(ink.height(), all.height(), 6.0));

  // One layer per line instead, on request.
  MainWindowTestAccess::undo(window);
  (void)run_ok(window, kRecognizeScan +
                           QStringLiteral("layer.recognizeText({layers: 'visible', grouping: 'lines', font: 'Arial'});"));
  const auto* lines = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
  CHECK(lines != nullptr && lines->children().size() == 3);
}

void ui_recognize_text_samples_colors() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 800, 500);
  const QColor paper(250, 244, 200);
  const QColor red(200, 30, 30);
  const QColor light(245, 245, 245);
  Page page(700, 400, paper);
  page.draw(QStringLiteral("Red text on cream paper"), 30, 30, arial(40), red);
  // Light text on a dark panel.
  {
    QPainter painter(&page.image);
    painter.fillRect(QRect(0, 200, 700, 200), QColor(20, 30, 60));
  }
  page.background = QColor(20, 30, 60);
  page.draw(QStringLiteral("Light text on a dark panel"), 30, 260, arial(40), light);
  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(0, 0));
  const auto colors = [&]() -> std::pair<QColor, QColor> {
    const auto* group = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
    CHECK(group != nullptr && group->children().size() == 2);
    if (group == nullptr || group->children().size() != 2) {
      return {};
    }
    return {QColor(QString::fromStdString(metadata_value(group->children()[1], patchy::kLayerMetadataTextColor))),
            QColor(QString::fromStdString(metadata_value(group->children()[0], patchy::kLayerMetadataTextColor)))};
  };
  const auto close_to = [](const QColor& a, const QColor& b, int tolerance) {
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
  };

  (void)run_ok(window, kRecognizeScan +
                           QStringLiteral("layer.recognizeText({layers: 'visible', grouping: 'lines', font: 'Arial'});"));
  auto sampled = colors();
  CHECK(close_to(sampled.first, red, 45));
  CHECK(close_to(sampled.second, light, 45));

  // A fixed colour wins over sampling.
  MainWindowTestAccess::undo(window);
  (void)run_ok(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'visible', grouping: 'lines', "
                                                       "font: 'Arial', color: '#0000ff'});"));
  sampled = colors();
  CHECK(sampled.first == QColor(0, 0, 255) && sampled.second == QColor(0, 0, 255));

  // The sampler on its own.
  CHECK(close_to(patchy::ui::sampled_text_color(page.image, QRectF(page.lines[0].box.x, page.lines[0].box.y,
                                                                    page.lines[0].box.width, page.lines[0].box.height)),
                 red, 45));
  // A box with nothing in it is black, not the paper colour.
  CHECK(patchy::ui::sampled_text_color(page.image, QRectF(400, 120, 200, 40)) == QColor(Qt::black));
}

void ui_recognize_text_detects_slanted_text() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 900, 500);
  Page page(800, 400);
  const QRect upright = page.draw(QStringLiteral("Upright roman letters here"), 30, 40, arial(40));
  const QRect slanted = page.draw(QStringLiteral("Slanted italic letters here"), 30, 240, arial(40, true));
  // The measure on its own: upright within a step of zero, an italic face well past it.
  CHECK(std::abs(patchy::ui::measured_slant_degrees(page.image, QRectF(upright))) <= 2.0);
  const double lean = patchy::ui::measured_slant_degrees(page.image, QRectF(slanted));
  CHECK(lean >= 8.0 && lean <= 18.0);
  // Too little to measure is zero.
  CHECK(patchy::ui::measured_slant_degrees(page.image, QRectF(5, 5, 6, 6)) == 0.0);

  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(0, 0));
  (void)run_ok(window, kRecognizeScan +
                           QStringLiteral("layer.recognizeText({layers: 'visible', grouping: 'lines', font: 'Arial'});"));
  const auto* group = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
  CHECK(group != nullptr && group->children().size() == 2);
  if (group == nullptr || group->children().size() != 2) {
    return;
  }
  CHECK(metadata_value(group->children()[1], patchy::kLayerMetadataTextItalic) == "false");
  CHECK(metadata_value(group->children()[0], patchy::kLayerMetadataTextItalic) == "true");

  // With matching off: the Type tool's size and colour, upright, whatever the image says.
  MainWindowTestAccess::undo(window);
  (void)run_ok(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'visible', grouping: 'lines', "
                                                       "font: 'Arial', matchStyle: false});"));
  const auto* plain = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
  CHECK(plain != nullptr && plain->children().size() == 2);
  if (plain != nullptr && plain->children().size() == 2) {
    CHECK(metadata_value(plain->children()[0], patchy::kLayerMetadataTextItalic) == "false");
    CHECK(metadata_value(plain->children()[0], patchy::kLayerMetadataTextSize) ==
          metadata_value(plain->children()[1], patchy::kLayerMetadataTextSize));
  }
}

void ui_recognize_text_hidden_layers_keep_composite() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 700, 400);
  Page page(600, 300);
  page.draw(QStringLiteral("Hidden text stays hidden"), 30, 40, arial(36));
  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(20, 20));
  const QImage before = patchy::ui::qimage_from_document(MainWindowTestAccess::document(window), true);
  (void)run_ok(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'hidden', font: 'Arial'});"));
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto* group = recognized_group(document);
  CHECK(group != nullptr);
  if (group == nullptr) {
    return;
  }
  // The group is there, with its text, and hidden: the image is the same pixels.
  CHECK(!group->visible());
  CHECK(group->children().size() == 1 && group->children()[0].visible());
  CHECK(patchy::ui::qimage_from_document(document, true) == before);
}

void ui_recognize_text_hide_original_hides_source() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 700, 400);
  Page page(600, 300, QColor(230, 220, 200));
  page.draw(QStringLiteral("Show the text instead"), 30, 40, arial(36));
  const ScopedOcrEngines engines({fake_for(page)});
  const auto scan_id = add_scan_layer(window, page.image, QPoint(20, 30));
  (void)run_ok(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'replace', font: 'Arial'});"));
  auto& document = MainWindowTestAccess::document(window);
  const auto* group = recognized_group(std::as_const(document));
  const auto* scan = std::as_const(document).find_layer(scan_id);
  CHECK(group != nullptr && scan != nullptr);
  if (group == nullptr || scan == nullptr) {
    return;
  }
  // The original is hidden, never changed: its pixels are what they were.
  CHECK(!scan->visible());
  CHECK(patchy::ui::qimage_from_pixel_buffer(scan->pixels()).convertToFormat(QImage::Format_RGBA8888) == page.image);
  // The text reads on white paper where the original was.
  CHECK(group->visible());
  CHECK(group->children().size() == 2);
  if (group->children().size() == 2) {
    const auto& paper = group->children().front();
    CHECK(paper.name() == "Paper" && !patchy::layer_is_text(paper));
    CHECK(paper.bounds().x == 20 && paper.bounds().y == 30 && paper.bounds().width == 600 &&
          paper.bounds().height == 300);
    CHECK(patchy::layer_is_text(group->children().back()));
  }
  const QImage shown = patchy::ui::qimage_from_document(std::as_const(document), true);
  CHECK(shown.pixelColor(25, 35) == QColor(Qt::white));

  // One undo step brings the original back and removes the group.
  MainWindowTestAccess::undo(window);
  const auto* restored = std::as_const(MainWindowTestAccess::document(window)).find_layer(scan_id);
  CHECK(restored != nullptr && restored->visible());
  CHECK(recognized_group(std::as_const(MainWindowTestAccess::document(window))) == nullptr);
}

// A list entry as a dark interface draws one: a title with two labels beside it on one
// row, a paragraph under them and a row of buttons under that, all one size. Every title,
// label and button would join the paragraph next to it, and they used to become lines of
// one text layer whose leading (the median distance between its lines) was next to nothing,
// so the whole entry was drawn on top of itself.
void ui_recognize_text_keeps_row_neighbours_apart() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 800, 260);
  const QColor dark(30, 32, 40);
  const QColor light(236, 236, 240);
  Page page(800, 260, dark);
  const QFont font = arial(26);
  struct Drawn {
    QString text;
    QRect ink;
  };
  std::vector<Drawn> singles;
  const auto single = [&](const QString& text, int x, int y) { singles.push_back({text, page.draw(text, x, y, font, light)}); };
  single(QStringLiteral("Entry title here"), 40, 30);
  single(QStringLiteral("Label one"), 300, 30);
  single(QStringLiteral("Waiting"), 480, 30);
  const QStringList paragraph{QStringLiteral("The first line of this paragraph goes right across the page"),
                              QStringLiteral("the second line follows it at the same pitch"),
                              QStringLiteral("and the third line ends the paragraph here.")};
  QRect paragraph_ink;
  for (qsizetype index = 0; index < paragraph.size(); ++index) {
    paragraph_ink = paragraph_ink.united(page.draw(paragraph[index], 40, 66 + static_cast<int>(index) * 32, font, light));
  }
  single(QStringLiteral("Accept it"), 40, 170);
  single(QStringLiteral("Not today"), 240, 170);
  single(QStringLiteral("Later"), 440, 170);

  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(0, 0));
  (void)run_ok(window, kRecognizeScan + QStringLiteral("layer.recognizeText({layers: 'replace', font: 'Arial'});"));
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto* group = recognized_group(document);
  // Seven text layers (the paragraph and six lines of their own) over the Paper.
  CHECK(group != nullptr && group->children().size() == 8);
  if (group == nullptr || group->children().size() != 8) {
    return;
  }
  const auto layer_with = [&](const std::string& text) -> const patchy::Layer* {
    for (const auto& child : group->children()) {
      if (metadata_value(child, patchy::kLayerMetadataText) == text) {
        return &child;
      }
    }
    return nullptr;
  };
  // Each line of a shared row is its own layer, on its own drawn place.
  for (const auto& drawn : singles) {
    const auto* layer = layer_with(drawn.text.toStdString());
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      continue;
    }
    const QRect ink = layer_ink(*layer);
    CHECK(near(ink.center().x(), drawn.ink.center().x(), 4.0));
    CHECK(near(ink.center().y(), drawn.ink.center().y(), 4.0));
    CHECK(near(ink.width(), drawn.ink.width(), 0.08 * drawn.ink.width() + 3.0));
  }
  // The paragraph is one layer at its own pitch, where it was drawn.
  const auto* block = layer_with(paragraph.join(QLatin1Char('\n')).toStdString());
  CHECK(block != nullptr);
  if (block != nullptr) {
    const QRect ink = layer_ink(*block);
    CHECK(near(ink.top(), paragraph_ink.top(), 4.0));
    CHECK(near(ink.bottom(), paragraph_ink.bottom(), 4.0));
    // The width is fitted to within 2%, and the block is centred on its lines.
    CHECK(near(ink.center().x(), paragraph_ink.center().x(), 4.0));
    CHECK(near(ink.width(), paragraph_ink.width(), 0.03 * paragraph_ink.width() + 3.0));
  }

  // The page is dark, so the Paper under the text is the page's own colour, not white: the
  // light text sampled from it shows.
  const auto& paper = group->children().front();
  CHECK(paper.name() == "Paper" && !patchy::layer_is_text(paper));
  CHECK(patchy::ui::recognized_text_paper_color(page.image) == dark);
  const QImage shown = patchy::ui::qimage_from_document(document, true);
  CHECK(shown.pixelColor(4, 4) == dark);
  CHECK(shown.pixelColor(795, 255) == dark);
  if (const auto* title = layer_with("Entry title here"); title != nullptr) {
    CHECK(QColor(QString::fromStdString(metadata_value(*title, patchy::kLayerMetadataTextColor))).lightness() > 180);
  }

  // A light page keeps white paper, whatever its tint; an image with nothing to read too.
  QImage cream(64, 64, QImage::Format_RGBA8888);
  cream.fill(QColor(230, 220, 200));
  CHECK(patchy::ui::recognized_text_paper_color(cream) == QColor(Qt::white));
  CHECK(patchy::ui::recognized_text_paper_color(QImage()) == QColor(Qt::white));
}

void ui_recognize_text_vertical_and_turned_lines() {
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 800, 700);
  Page page(700, 600);
  // Two vertical columns, read right to left, and one line turned 10 degrees. The pixels
  // do not matter here: the engine's report does.
  page.lines.push_back(FakeLine{Box{500, 60, 40, 360}, "COLUMN ONE", true});
  page.lines.push_back(FakeLine{Box{440, 60, 40, 300}, "COLUMN TWO", true});
  FakeLine turned{Box{60, 480, 300, 40}, "Turned ten degrees", false};
  turned.angle_degrees = 10.0;
  page.lines.push_back(turned);
  const ScopedOcrEngines engines({fake_for(page)});
  add_scan_layer(window, page.image, QPoint(0, 0));
  const auto result = run_ok(
      window, kRecognizeScan + QStringLiteral("var r = layer.recognizeText({layers: 'visible', font: 'Arial'});"
                                              "patchy.setResult({vertical: r.lines[0].vertical, "
                                              "angle: r.lines[2].angle, count: r.group.children.length});"));
  CHECK(result.value(QStringLiteral("vertical")).toBool());
  CHECK(near(result.value(QStringLiteral("angle")).toDouble(), 10.0, 0.001));
  CHECK(result.value(QStringLiteral("count")).toInt() == 2);
  const auto* group = recognized_group(std::as_const(MainWindowTestAccess::document(window)));
  CHECK(group != nullptr && group->children().size() == 2);
  if (group == nullptr || group->children().size() != 2) {
    return;
  }
  // Horizontal blocks read first, so the turned line is on top and the columns under it.
  const auto& line = group->children()[1];
  const auto& columns = group->children()[0];
  // The two columns are one vertical text layer, the right one first.
  CHECK(metadata_value(columns, patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
  CHECK(metadata_value(columns, patchy::kLayerMetadataText) == "COLUMN ONE\nCOLUMN TWO");
  const QRect column_ink = layer_ink(columns);
  CHECK(near(column_ink.center().x(), 490.0, 14.0));
  CHECK(column_ink.height() > 200 && column_ink.width() < 160);
  // The turned line keeps its angle as the layer's text transform and sits on its box.
  CHECK(metadata_value(line, patchy::kLayerMetadataText) == "Turned ten degrees");
  const auto transform = patchy::parse_layer_affine_transform(metadata_value(line, patchy::kLayerMetadataTextTransform));
  CHECK(transform.has_value());
  if (transform.has_value()) {
    const double degrees = std::atan2((*transform)[1], (*transform)[0]) * 180.0 / 3.14159265358979323846;
    CHECK(near(degrees, 10.0, 0.01));
  }
  const QRect line_ink = layer_ink(line);
  CHECK(near(line_ink.center().x(), 210.0, 6.0));
  CHECK(near(line_ink.center().y(), 500.0, 6.0));
  CHECK(line_ink.height() > 60);  // a 300 px line turned 10 degrees stands about 90 px tall
}

}  // namespace

std::vector<patchy::test::TestCase> ocr_layers_ui_tests() {
  return {
      {"ui_script_ocr_lists_engines_and_languages", ui_script_ocr_lists_engines_and_languages},
      {"ui_script_recognize_text_returns_lines_in_document_pixels",
       ui_script_recognize_text_returns_lines_in_document_pixels},
      {"ui_script_recognize_text_creates_layers_in_one_undo_step",
       ui_script_recognize_text_creates_layers_in_one_undo_step},
      {"ui_script_recognize_text_options_are_strict", ui_script_recognize_text_options_are_strict},
      {"ui_script_recognize_text_runs_unattended", ui_script_recognize_text_runs_unattended},
      {"ui_recognize_text_fits_lines_to_boxes", ui_recognize_text_fits_lines_to_boxes},
      {"ui_recognize_text_one_size_per_block", ui_recognize_text_one_size_per_block},
      {"ui_recognize_text_keeps_line_breaks_in_blocks", ui_recognize_text_keeps_line_breaks_in_blocks},
      {"ui_recognize_text_samples_colors", ui_recognize_text_samples_colors},
      {"ui_recognize_text_detects_slanted_text", ui_recognize_text_detects_slanted_text},
      {"ui_recognize_text_hidden_layers_keep_composite", ui_recognize_text_hidden_layers_keep_composite},
      {"ui_recognize_text_hide_original_hides_source", ui_recognize_text_hide_original_hides_source},
      {"ui_recognize_text_keeps_row_neighbours_apart", ui_recognize_text_keeps_row_neighbours_apart},
      {"ui_recognize_text_vertical_and_turned_lines", ui_recognize_text_vertical_and_turned_lines},
  };
}
