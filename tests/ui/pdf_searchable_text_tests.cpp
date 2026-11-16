// Recognized text in exported PDFs (docs/pdf-searchable-text.md, "From a document"): the
// lines of a document's Recognized Text groups go out as invisible text over the page image,
// one run a line, where the image shows them. The scripted fake engine reports the boxes of
// text these tests draw, and every file is read back through Patchy's own PDF reader (and
// through Qt PDF where the build has it), which share no code with the writer.

#include "ai/ocr_model_engine.hpp"
#include "ai/ocr_models.hpp"
#include "ai/segmentation_engine.hpp"
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pdf_source_page.hpp"
#include "formats/pdf_content.hpp"
#include "formats/pdf_file.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_page_fixture.hpp"
#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/multipage_pdf_export_dialog.hpp"
#include "ui/pdf_export.hpp"
#include "ui/pdf_recognized_text.hpp"
#include "ui/script_engine.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QTimer>
#include <QTransform>

#if defined(PATCHY_HAVE_QT_PDF)
#include <QPdfDocument>
#include <QPdfSelection>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::ocr::Box;
using patchy::ocr::FakeLine;
using patchy::test::ui::add_ocr_scan_layer;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::ocr_fake_for;
using patchy::test::ui::ocr_test_font;
using patchy::test::ui::OcrTestPage;
using patchy::test::ui::recognized_text_group;
using patchy::test::ui::ScopedOcrEngines;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::when_dialog_visible;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::PdfExportOptions;
using patchy::ui::RecognizedLinePlace;

constexpr int kPageWidth = 600;
constexpr int kPageHeight = 400;
constexpr char kHiddenNotice[] =
    "Hidden recognized text was left out: a PDF with editable layers holds only the text it shows";

bool near(double value, double expected, double tolerance) {
  return std::abs(value - expected) <= tolerance;
}

QString artifact(const char* name) {
  QDir().mkpath(QStringLiteral("test-artifacts"));
  const auto path = QDir::current().filePath(QStringLiteral("test-artifacts/%1.pdf").arg(QLatin1String(name)));
  QFile::remove(path);
  return path;
}

QByteArray bytes_of(const QString& path) {
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

// What a reader finds on each page of a file.
struct ReadSink final : patchy::pdf::ContentSink {
  std::vector<patchy::pdf::TextRun> hidden;
  std::vector<patchy::pdf::TextRun> visible;
  int images{0};

  void on_path(const patchy::pdf::PaintedPath&) override {}
  void on_text(const patchy::pdf::TextRun& run) override { visible.push_back(run); }
  void on_hidden_text(const patchy::pdf::TextRun& run) override { hidden.push_back(run); }
  void on_image(const patchy::pdf::PlacedImage&) override { ++images; }
  void on_shading(std::shared_ptr<const patchy::pdf::ResolvedShading>, const patchy::VectorPath&) override {}
  void on_notice(const std::string&) override {}
};

// The pages of `path`, one point per unit, y down from the top of the page as displayed.
std::vector<ReadSink> read_pages(const QString& path) {
  const auto raw = bytes_of(path);
  const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
  std::vector<std::string> notices;
  auto file = patchy::pdf::File::open(bytes, &notices);
  CHECK(file.has_value());
  std::vector<ReadSink> pages;
  if (!file.has_value()) {
    return pages;
  }
  pages.resize(file->pages().size());
  for (std::size_t index = 0; index < pages.size(); ++index) {
    patchy::pdf::execute_page(*file, file->pages()[index], 1.0, pages[index]);
  }
  return pages;
}

#if defined(PATCHY_HAVE_QT_PDF)
// The text Qt PDF (PDFium) extracts from a page, line breaks as spaces.
QString qt_pdf_text(const QString& path, int page) {
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  return document.getAllText(page).text().simplified();
}
#endif

struct ScriptRun {
  bool failed{false};
  QJsonValue result;
  QString messages;
};

ScriptRun run(MainWindow& window, const QString& code) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("pdf-searchable-test");
  options.unattended = true;
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

QJsonObject run_ok(MainWindow& window, const QString& code) {
  const auto outcome = run(window, code);
  if (outcome.failed) {
    std::fprintf(stderr, "%s\n", outcome.messages.toUtf8().constData());
  }
  CHECK(!outcome.failed);
  return outcome.result.toObject();
}

// `text` as a JavaScript string literal.
QString js(const QString& text) {
  const auto array = QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact));
  return array.mid(1, array.size() - 2);
}

// A window holding one document: a scan with two drawn lines and the text recognized from
// it, as `layers` says ("hidden", "visible", "replace").
struct Scanned {
  OcrTestPage page{kPageWidth, kPageHeight};
  QRect first;
  QRect second;
  std::unique_ptr<ScopedOcrEngines> engines;
  MainWindow window;

  explicit Scanned(const char* layers = "hidden") {
    first = page.draw(QStringLiteral("Scanned page heading"), 40, 40, ocr_test_font(40));
    second = page.draw(QStringLiteral("A line further down"), 40, 250, ocr_test_font(40));
    engines = std::make_unique<ScopedOcrEngines>(
        std::vector<std::shared_ptr<patchy::ocr::Engine>>{ocr_fake_for(page)});
    show_window(window);
    MainWindowTestAccess::create_document(window, kPageWidth, kPageHeight);
    add_ocr_scan_layer(window, page.image, QPoint(0, 0));
    recognize(layers);
  }

  void recognize(const char* layers) {
    run_ok(window, QStringLiteral("var doc = app.activeDocument; var layer = doc.findLayer('Scan');"
                                  "layer.recognizeText({layers: '%1', font: 'Arial'});")
                       .arg(QLatin1String(layers)));
    CHECK(recognized_text_group(std::as_const(document())) != nullptr);
  }

  [[nodiscard]] patchy::Document& document() { return MainWindowTestAccess::document(window); }
  // Points per document pixel on an exported page.
  [[nodiscard]] double scale() { return 72.0 / std::as_const(document()).print_settings().horizontal_ppi; }
};

// One flat page of the whole canvas, through the image writer.
PdfExportOptions flat_options() {
  PdfExportOptions options;
  options.pages = patchy::ui::PdfPageSource::Canvas;
  return options;
}

// A level run sits on the bottom edge of `box` (document pixels) and is as long as it.
void check_level_run(const patchy::pdf::TextRun& run, const QRectF& box, double scale) {
  CHECK(run.render_mode == 3);
  CHECK(near(run.transform.e, box.left() * scale, 0.05));
  CHECK(near(run.transform.f, box.bottom() * scale, 0.05));
  CHECK(run.transform.a > 0.0 && near(run.transform.b, 0.0, 1e-6));
  CHECK(run.width_is_known && near(run.intended_width, box.width() * scale, 0.05));
}

QRectF box_of(const QRect& ink) {
  return QRectF(ink.x(), ink.y(), ink.width(), ink.height());
}

void ui_pdf_searchable_text_line_places_round_trip() {
  std::vector<RecognizedLinePlace> places(2);
  places[0].quad = {QPointF(10.25, 20.5), QPointF(310.0, 20.5), QPointF(310.0, 60.75), QPointF(10.25, 60.75)};
  places[1].quad = {QPointF(500.0, 30.0), QPointF(540.0, 30.0), QPointF(540.0, 390.0), QPointF(500.0, 390.0)};
  places[1].vertical = true;
  const auto value = patchy::ui::serialize_recognized_line_places(patchy::Rect{8, 18, 540, 380}, places);
  CHECK(value.find(',') != std::string::npos && value.find(';') == std::string::npos);
  const auto parsed = patchy::ui::parse_recognized_line_places(value);
  CHECK(parsed.has_value());
  if (parsed.has_value()) {
    CHECK((parsed->layer_bounds == patchy::Rect{8, 18, 540, 380}));
    CHECK(parsed->places.size() == 2);
    if (parsed->places.size() == 2) {
      CHECK(!parsed->places[0].vertical && parsed->places[1].vertical);
      for (std::size_t line = 0; line < 2; ++line) {
        for (std::size_t corner = 0; corner < 4; ++corner) {
          CHECK(near(parsed->places[line].quad[corner].x(), places[line].quad[corner].x(), 0.006));
          CHECK(near(parsed->places[line].quad[corner].y(), places[line].quad[corner].y(), 0.006));
        }
      }
    }
  }
  // Anything else is no placement: the layer's bounds stand in.
  for (const char* bad : {"", "1", "2|0,0,1,1|0,0,1,0,1,1,0,1,0", "1|0,0,1,1", "1|0,0,1|0,0,1,0,1,1,0,1,0",
                          "1|0,0,1,1|0,0,1,0,1,1,0,1", "1|0,0,1,1|0,0,1,0,1,1,0,1,2", "1|0,0,1,1|0,0,x,0,1,1,0,1,0"}) {
    CHECK(!patchy::ui::parse_recognized_line_places(bad).has_value());
  }

  // Document pixels to page points: a 600 x 400 document on a 300 x 200 pt page, and the
  // same document as each quarter turn of /Rotate displays it.
  const QSize pixels(600, 400);
  const auto upright = patchy::ui::document_to_pdf_page(pixels, QSizeF(300.0, 200.0), 0);
  CHECK(upright.map(QPointF(0.0, 0.0)) == QPointF(0.0, 200.0));
  CHECK(upright.map(QPointF(600.0, 400.0)) == QPointF(300.0, 0.0));
  // /Rotate 90: the page is 200 x 300 before the turn; its bottom left shows at the top left.
  const auto quarter = patchy::ui::document_to_pdf_page(pixels, QSizeF(200.0, 300.0), 90);
  CHECK(quarter.map(QPointF(0.0, 0.0)) == QPointF(0.0, 0.0));
  CHECK(quarter.map(QPointF(600.0, 0.0)) == QPointF(0.0, 300.0));
  CHECK(quarter.map(QPointF(0.0, 400.0)) == QPointF(200.0, 0.0));
  const auto half = patchy::ui::document_to_pdf_page(pixels, QSizeF(300.0, 200.0), 180);
  CHECK(half.map(QPointF(0.0, 0.0)) == QPointF(300.0, 0.0));
  CHECK(half.map(QPointF(600.0, 400.0)) == QPointF(0.0, 200.0));
  const auto three = patchy::ui::document_to_pdf_page(pixels, QSizeF(200.0, 300.0), 270);
  CHECK(three.map(QPointF(0.0, 0.0)) == QPointF(200.0, 300.0));
  CHECK(three.map(QPointF(600.0, 0.0)) == QPointF(200.0, 0.0));
  CHECK(three.map(QPointF(0.0, 400.0)) == QPointF(0.0, 300.0));
}

void ui_pdf_searchable_text_export_writes_recognized_lines() {
  Scanned scanned;
  const auto& document = std::as_const(scanned.document());
  CHECK(patchy::ui::document_has_recognized_text(document));
  CHECK(patchy::ui::document_has_hidden_recognized_text(document));
  const double scale = scanned.scale();

  const auto path = artifact("ui_pdf_searchable_text");
  patchy::ui::write_pdf_document_file(document, path, flat_options());
  auto pages = read_pages(path);
  CHECK(pages.size() == 1);
  if (pages.size() != 1) {
    return;
  }
  // The page is still one image; the text paints nothing and comes in reading order, one
  // run for each line, on the line the image shows.
  CHECK(pages[0].images == 1);
  CHECK(pages[0].visible.empty());
  CHECK(pages[0].hidden.size() == 2);
  if (pages[0].hidden.size() == 2) {
    CHECK(pages[0].hidden[0].utf8 == "Scanned page heading");
    CHECK(pages[0].hidden[1].utf8 == "A line further down");
    check_level_run(pages[0].hidden[0], box_of(scanned.first), scale);
    check_level_run(pages[0].hidden[1], box_of(scanned.second), scale);
  }
  const auto content = bytes_of(path);
  CHECK(content.count(" Tj") == 2);
  CHECK(!content.contains(" TJ"));  // never an array of separately placed pieces
#if defined(PATCHY_HAVE_QT_PDF)
  CHECK(qt_pdf_text(path, 0) == QStringLiteral("Scanned page heading A line further down"));
#endif

  // Off: the file a document without the group writes, byte for byte.
  auto off = flat_options();
  off.searchable_text = false;
  const auto off_path = artifact("ui_pdf_searchable_text_off");
  patchy::ui::write_pdf_document_file(document, off_path, off);
  const auto off_pages = read_pages(off_path);
  CHECK(off_pages.size() == 1 && off_pages[0].hidden.empty());
  patchy::Document plain = document;
  CHECK(plain.remove_layer(recognized_text_group(document)->id()));
  CHECK(!patchy::ui::document_has_recognized_text(plain));
  const auto plain_path = artifact("ui_pdf_searchable_text_plain");
  patchy::ui::write_pdf_document_file(plain, plain_path, flat_options());
  CHECK(bytes_of(off_path) == bytes_of(plain_path));
  CHECK(bytes_of(off_path) != content);
}

void ui_pdf_searchable_text_follows_edited_and_moved_layers() {
  Scanned scanned;
  auto& document = scanned.document();
  const double scale = scanned.scale();
  const auto* group = recognized_text_group(std::as_const(document));
  CHECK(group != nullptr && group->children().size() == 2);
  if (group == nullptr || group->children().size() != 2) {
    return;
  }
  // Reading order from the top of the stack: the heading is the last child.
  const auto heading_id = group->children().back().id();
  const auto lower_id = group->children().front().id();

  // A correction typed into the heading goes out in its place.
  document.find_layer(heading_id)->metadata()[patchy::kLayerMetadataText] = "Corrected heading";
  // The lower line moved: its text moves with it.
  auto* lower = document.find_layer(lower_id);
  const auto moved_from = lower->bounds();
  lower->set_bounds(patchy::Rect{moved_from.x + 30, moved_from.y + 20, moved_from.width, moved_from.height});
  auto path = artifact("ui_pdf_searchable_text_edited");
  patchy::ui::write_pdf_document_file(std::as_const(document), path, flat_options());
  auto pages = read_pages(path);
  CHECK(pages.size() == 1 && pages[0].hidden.size() == 2);
  if (pages.size() == 1 && pages[0].hidden.size() == 2) {
    CHECK(pages[0].hidden[0].utf8 == "Corrected heading");
    check_level_run(pages[0].hidden[0], box_of(scanned.first), scale);
    CHECK(pages[0].hidden[1].utf8 == "A line further down");
    check_level_run(pages[0].hidden[1], box_of(scanned.second).translated(30.0, 20.0), scale);
  }

  // A line count the recognition never saw: the layer's own bounds stand in, shared evenly.
  document.find_layer(heading_id)->metadata()[patchy::kLayerMetadataText] = "Upper half\nLower half";
  const auto bounds = std::as_const(document).find_layer(heading_id)->bounds();
  path = artifact("ui_pdf_searchable_text_relined");
  patchy::ui::write_pdf_document_file(std::as_const(document), path, flat_options());
  pages = read_pages(path);
  CHECK(pages.size() == 1 && pages[0].hidden.size() == 3);
  if (pages.size() == 1 && pages[0].hidden.size() == 3) {
    CHECK(pages[0].hidden[0].utf8 == "Upper half");
    CHECK(pages[0].hidden[1].utf8 == "Lower half");
    const QRectF upper(bounds.x, bounds.y, bounds.width, bounds.height / 2.0);
    check_level_run(pages[0].hidden[0], upper, scale);
    check_level_run(pages[0].hidden[1], upper.translated(0.0, bounds.height / 2.0), scale);
  }

  // A rasterized layer is no longer text, and a deleted one is gone: nothing is written
  // for either.
  CHECK(document.remove_layer(heading_id));
  path = artifact("ui_pdf_searchable_text_removed");
  patchy::ui::write_pdf_document_file(std::as_const(document), path, flat_options());
  pages = read_pages(path);
  CHECK(pages.size() == 1 && pages[0].hidden.size() == 1);
}

void ui_pdf_searchable_text_keeps_unicode_and_columns() {
  const QString level = QString::fromUtf8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE8\xA6\x8B\xE5\x87\xBA\xE3\x81\x97");
  const QString column_one = QString::fromUtf8("\xE7\xB8\xA6\xE6\x9B\xB8\xE3\x81\x8D\xE3\x81\xAE\xE4\xB8\x80\xE8\xA1\x8C\xE7\x9B\xAE");
  const QString column_two = QString::fromUtf8("\xE4\xBA\x8C\xE8\xA1\x8C\xE7\x9B\xAE\xE3\x81\xA7\xE3\x81\x99");
  OcrTestPage page(700, 600);
  // The pixels do not matter here: the engine's report does.
  page.lines.push_back(FakeLine{Box{60, 40, 300, 40}, level.toStdString(), false});
  page.lines.push_back(FakeLine{Box{500, 120, 40, 360}, column_one.toStdString(), true});
  page.lines.push_back(FakeLine{Box{440, 120, 40, 300}, column_two.toStdString(), true});
  const ScopedOcrEngines engines({ocr_fake_for(page)});
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 700, 600);
  add_ocr_scan_layer(window, page.image, QPoint(0, 0));
  run_ok(window, QStringLiteral("var doc = app.activeDocument; var layer = doc.findLayer('Scan');"
                                "layer.recognizeText({layers: 'hidden', font: 'Arial'});"));
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const double scale = 72.0 / document.print_settings().horizontal_ppi;

  const auto path = artifact("ui_pdf_searchable_text_unicode");
  patchy::ui::write_pdf_document_file(document, path, flat_options());
  const auto pages = read_pages(path);
  CHECK(pages.size() == 1 && pages[0].hidden.size() == 3);
  if (pages.size() != 1 || pages[0].hidden.size() != 3) {
    return;
  }
  // The level line first, then the columns from the right, every character kept.
  CHECK(pages[0].hidden[0].utf8 == level.toStdString());
  CHECK(pages[0].hidden[1].utf8 == column_one.toStdString());
  CHECK(pages[0].hidden[2].utf8 == column_two.toStdString());
  check_level_run(pages[0].hidden[0], QRectF(60, 40, 300, 40), scale);
  // A column starts at its top left and reads down the page, as long as the column is tall.
  const auto& column = pages[0].hidden[1];
  CHECK(column.render_mode == 3);
  CHECK(near(column.transform.a, 0.0, 1e-6) && column.transform.b > 0.0);
  CHECK(near(column.transform.e, 500.0 * scale, 0.05));
  CHECK(near(column.transform.f, 120.0 * scale, 0.05));
  CHECK(near(column.intended_width, 360.0 * scale, 0.05));
  CHECK(near(pages[0].hidden[2].intended_width, 300.0 * scale, 0.05));
#if defined(PATCHY_HAVE_QT_PDF)
  const auto found = qt_pdf_text(path, 0);
  CHECK(found.contains(level));
  CHECK(found.contains(column_one));
  CHECK(found.contains(column_two));
#endif
}

void ui_pdf_searchable_text_rotated_pass_through_page() {
  for (const int rotate : {90, 180, 270}) {
    Scanned scanned;
    auto& document = scanned.document();
    const double scale = scanned.scale();
    // The page as another PDF stored it: turned, with /Rotate standing it upright again.
    // The stored image is the upright scan turned back the other way.
    const QImage stored = scanned.page.image.convertToFormat(QImage::Format_RGB888)
                              .transformed(QTransform().rotate(-rotate));
    QByteArray jpeg;
    {
      QBuffer buffer(&jpeg);
      buffer.open(QIODevice::WriteOnly);
      QImageWriter writer(&buffer, "JPEG");
      CHECK(writer.write(stored));
    }
    auto source = std::make_shared<patchy::PdfSourcePage>();
    source->image_bytes =
        std::make_shared<const std::vector<std::uint8_t>>(jpeg.begin(), jpeg.end());
    source->filter = "DCTDecode";
    source->color_space = "/DeviceRGB";
    source->image_width = stored.width();
    source->image_height = stored.height();
    source->page_width_points = stored.width() * scale;
    source->page_height_points = stored.height() * scale;
    source->rotate = rotate;
    source->image_matrix = {source->page_width_points, 0.0, 0.0, source->page_height_points, 0.0, 0.0};
    patchy::ui::attach_pdf_source_page(document, source);
    CHECK(std::as_const(document).metadata().pdf_source_page != nullptr);

    const auto path = artifact(("ui_pdf_searchable_text_rotate_" + std::to_string(rotate)).c_str());
    patchy::ui::write_pdf_document_file(std::as_const(document), path, flat_options());
    // The page went out as its own bytes, turned as it came, with the text over it.
    const auto content = bytes_of(path);
    CHECK(content.contains(jpeg));
    CHECK(content.contains(QByteArray("/Rotate ") + QByteArray::number(rotate)));
    const auto pages = read_pages(path);
    CHECK(pages.size() == 1 && pages[0].hidden.size() == 2);
    if (pages.size() != 1 || pages[0].hidden.size() != 2) {
      continue;
    }
    // As displayed, the runs sit on the lines exactly as on an upright page.
    CHECK(pages[0].hidden[0].utf8 == "Scanned page heading");
    check_level_run(pages[0].hidden[0], box_of(scanned.first), scale);
    check_level_run(pages[0].hidden[1], box_of(scanned.second), scale);
#if defined(PATCHY_HAVE_QT_PDF)
    CHECK(qt_pdf_text(path, 0) == QStringLiteral("Scanned page heading A line further down"));
#endif
  }
}

void ui_pdf_searchable_text_root_group_is_not_a_page() {
  Scanned scanned;
  auto& document = scanned.document();
  // Recognized text over the whole canvas is a group at the root, and never a page.
  CHECK(!patchy::ui::document_has_page_groups(std::as_const(document)));
  CHECK(patchy::ui::documents_for_top_level_groups(std::as_const(document), true).empty());

  patchy::Layer page_group(document.allocate_layer_id(), "Page A", patchy::LayerKind::Group);
  patchy::Layer art(document.allocate_layer_id(), "Art", patchy::LayerKind::Pixel);
  art.set_pixels(patchy::ui::pixels_from_image_rgba(scanned.page.image));
  art.set_bounds(patchy::Rect{0, 0, kPageWidth, kPageHeight});
  page_group.add_child(std::move(art));
  document.add_layer(std::move(page_group));
  CHECK(patchy::ui::document_has_page_groups(std::as_const(document)));
  // One page, and the hidden text stays behind: it shows on no page.
  auto pages = patchy::ui::documents_for_top_level_groups(std::as_const(document), true);
  CHECK(pages.size() == 1);
  CHECK(pages.size() == 1 && !patchy::ui::document_has_recognized_text(pages[0]));
  // Shown, it rides along like any layer in no group, when those are drawn on every page.
  document.find_layer(recognized_text_group(std::as_const(document))->id())->set_visible(true);
  pages = patchy::ui::documents_for_top_level_groups(std::as_const(document), true);
  CHECK(pages.size() == 1);
  CHECK(pages.size() == 1 && patchy::ui::document_has_recognized_text(pages[0]));
  pages = patchy::ui::documents_for_top_level_groups(std::as_const(document), false);
  CHECK(pages.size() == 1);
  CHECK(pages.size() == 1 && !patchy::ui::document_has_recognized_text(pages[0]));
}

void ui_pdf_searchable_text_editable_export_notes_hidden_text() {
  // The scan over the document's own background is two layers: with editable layers the
  // page goes through Qt's engine, which has no invisible text.
  auto editable = flat_options();
  editable.editable_layers = true;
  {
    Scanned scanned;
    CHECK(!patchy::ui::pdf_detail::document_is_single_raster_layer(std::as_const(scanned.document())));
    std::vector<std::string> notices;
    const auto path = artifact("ui_pdf_searchable_text_editable_hidden");
    patchy::ui::write_pdf_document_file(std::as_const(scanned.document()), path, editable, &notices);
    CHECK(std::count(notices.begin(), notices.end(), std::string(kHiddenNotice)) == 1);
    const auto pages = read_pages(path);
    CHECK(pages.size() == 1 && pages[0].hidden.empty());
    // Not asked for: nothing to say.
    notices.clear();
    auto off = editable;
    off.searchable_text = false;
    patchy::ui::write_pdf_document_file(std::as_const(scanned.document()), path, off, &notices);
    CHECK(std::count(notices.begin(), notices.end(), std::string(kHiddenNotice)) == 0);
    // The same page as one of two: the multi-page writer says it once.
    notices.clear();
    const patchy::Document* both[] = {&std::as_const(scanned.document()), &std::as_const(scanned.document())};
    CHECK(patchy::ui::write_multipage_pdf_file(both, path, editable, &notices));
    CHECK(std::count(notices.begin(), notices.end(), std::string(kHiddenNotice)) == 1);
  }
  {
    // Text that shows is written as the real text it is: no notice.
    Scanned scanned("visible");
    CHECK(!patchy::ui::document_has_hidden_recognized_text(std::as_const(scanned.document())));
    std::vector<std::string> notices;
    const auto path = artifact("ui_pdf_searchable_text_editable_visible");
    patchy::ui::write_pdf_document_file(std::as_const(scanned.document()), path, editable, &notices);
    CHECK(std::count(notices.begin(), notices.end(), std::string(kHiddenNotice)) == 0);
#if defined(PATCHY_HAVE_QT_PDF)
    CHECK(qt_pdf_text(path, 0).contains(QStringLiteral("Scanned page heading")));
#endif
  }
}

void ui_pdf_searchable_text_option_shows_only_with_recognized_text() {
  const SettingsValueRestorer restore[] = {
      SettingsValueRestorer(QStringLiteral("saveOptions/pdfSearchableText")),
      SettingsValueRestorer(QStringLiteral("saveOptions/pdfKeepOriginalImages")),
      SettingsValueRestorer(QStringLiteral("saveOptions/pdfMissingFontsAsImages")),
      SettingsValueRestorer(QStringLiteral("saveOptions/pdfImageQuality")),
      SettingsValueRestorer(QStringLiteral("saveOptions/pdfLossless")),
      SettingsValueRestorer(QStringLiteral("exportOptions/multiPagePdfSource")),
      SettingsValueRestorer(QStringLiteral("exportOptions/multiPagePdfUngroupedLayers")),
      SettingsValueRestorer(QStringLiteral("exportOptions/multiPagePdfHiddenGroups")),
      SettingsValueRestorer(QStringLiteral("exportOptions/multiPagePdfEditableLayers")),
  };
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("saveOptions/pdfSearchableText"));
    settings.sync();
  }
  // PDF Options (Save As and Export Multi-Page PDF).
  for (const bool available : {false, true}) {
    patchy::ui::PdfOptionsDialogContext context;
    context.recognized_text_available = available;
    context.layered = false;
    bool saw = false;
    when_dialog_visible(QStringLiteral("multiPagePdfExportDialog"), [&saw, available](QDialog& dialog) {
      auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("multiPagePdfSearchableTextCheck"));
      CHECK(check != nullptr);
      if (check != nullptr) {
        CHECK(check->isVisibleTo(&dialog) == available);
        CHECK(check->isChecked());  // on unless turned off
        CHECK(!check->toolTip().isEmpty());
        check->setChecked(false);
        saw = true;
      }
      dialog.accept();
    });
    const auto choice = patchy::ui::run_pdf_options_dialog(nullptr, context);
    CHECK(saw && choice.has_value());
    if (choice.has_value()) {
      // A check nobody could see changes nothing; one that showed is the answer, and is kept.
      CHECK(choice->options.searchable_text == !available);
      patchy::ui::ImageSaveOptions save_options;
      patchy::ui::apply_pdf_choice_to_save_options(*choice, save_options);
      CHECK(save_options.pdf_searchable_text == !available);
    }
    CHECK(patchy::ui::app_settings().contains(QStringLiteral("saveOptions/pdfSearchableText")) == available);
  }
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("saveOptions/pdfSearchableText"), true).toBool());
  CHECK(!patchy::ui::load_image_save_option_defaults().pdf_searchable_text);

  // The flat-save PDF options dialog has the same check under the same rule.
  for (const bool available : {false, true}) {
    patchy::ui::ImageSaveOptions defaults;
    defaults.pdf_recognized_text_available = available;
    bool saw = false;
    QTimer::singleShot(0, patchy::test::ui::guarded([&saw, available] {
      auto* dialog = find_top_level_dialog(QStringLiteral("pdfSaveOptionsDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("pdfSearchableTextCheck"));
      CHECK(check != nullptr);
      if (check != nullptr) {
        CHECK(check->isVisible() == available);
        CHECK(check->isChecked());
        check->setChecked(false);
        saw = true;
      }
      dialog->accept();
    }));
    const auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("pdf"), defaults);
    CHECK(saw && chosen.has_value());
    if (chosen.has_value()) {
      CHECK(chosen->pdf_searchable_text == !available);
    }
  }
}

void ui_script_pdf_searchable_text_option() {
  Scanned scanned;
  const auto default_path = artifact("ui_script_pdf_searchable_default");
  const auto off_path = artifact("ui_script_pdf_searchable_off");
  const auto export_path = artifact("ui_script_pdf_searchable_export");
  const auto export_off_path = artifact("ui_script_pdf_searchable_export_off");
  run_ok(scanned.window,
         QStringLiteral("var doc = app.activeDocument;"
                        "if (!doc.exportAs(%1, {editableLayers: false, pages: 'canvas'})) throw new Error('a');"
                        "if (!doc.exportAs(%2, {editableLayers: false, pages: 'canvas', searchableText: false}))"
                        "  throw new Error('b');"
                        "if (!app.exportPdf(doc, %3, {pages: 'canvas'})) throw new Error('c');"
                        "if (!app.exportPdf(doc, %4, {pages: 'canvas', searchableText: false})) throw new Error('d');")
             .arg(js(default_path), js(off_path), js(export_path), js(export_off_path)));
  for (const auto& [path, lines] : {std::pair{default_path, 2}, std::pair{off_path, 0}, std::pair{export_path, 2},
                                    std::pair{export_off_path, 0}}) {
    const auto pages = read_pages(path);
    CHECK(pages.size() == 1);
    CHECK(pages.size() == 1 && static_cast<int>(pages[0].hidden.size()) == lines);
  }
  // The key is a strict boolean like its neighbours.
  const auto wrong = run(scanned.window, QStringLiteral("app.activeDocument.exportAs(%1, {searchableText: 'yes'});")
                                             .arg(js(default_path)));
  CHECK(wrong.failed);
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

// The whole way with a real engine: PP-OCR reads a drawn page, the export writes what it
// read, and readers find it on the lines. Skips where the models are not staged.
void ui_pdf_searchable_text_real_engine_reads_back_if_available() {
  struct StagedModels {
    StagedModels() {
      patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-download");
    }
    ~StagedModels() {
      patchy::ai::release_ocr_models();
      patchy::ai::set_downloaded_model_directory({});
    }
  };
  const StagedModels staged;
  const auto engine = patchy::ai::ocr_model_engine();
  if (engine == nullptr) {
    patchy::test::skip(patchy::test::SkipKind::platform, "this build cannot run the text recognition models");
  }
  if (engine->unavailable() != patchy::ocr::Unavailable::None) {
    patchy::test::skip(patchy::test::SkipKind::feature,
                       "the text recognition models are not staged: " + engine->unavailable_reason());
  }
  const QString first_text = QStringLiteral("Searchable text from a real engine");
  const QString second_text = QStringLiteral("The second line of the scanned page");
  OcrTestPage page(1000, 500);
  const QRect first = page.draw(first_text, 60, 60, ocr_test_font(44));
  const QRect second = page.draw(second_text, 60, 260, ocr_test_font(44));
  const ScopedOcrEngines engines({engine});
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 1000, 500);
  add_ocr_scan_layer(window, page.image, QPoint(0, 0));
  run_ok(window, QStringLiteral("var doc = app.activeDocument; var layer = doc.findLayer('Scan');"
                                "layer.recognizeText({engine: 'ppocr', language: 'en', layers: 'hidden', "
                                "font: 'Arial'});"));
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const double scale = 72.0 / document.print_settings().horizontal_ppi;
  const auto path = artifact("ui_pdf_searchable_text_real_engine");
  patchy::ui::write_pdf_document_file(document, path, flat_options());
  const auto pages = read_pages(path);
  CHECK(pages.size() == 1 && pages[0].hidden.size() == 2);
  if (pages.size() != 1 || pages[0].hidden.size() != 2) {
    return;
  }
  const QRect inks[] = {first, second};
  const QString texts[] = {first_text, second_text};
  for (std::size_t index = 0; index < 2; ++index) {
    const auto& run = pages[0].hidden[index];
    CHECK(squeezed(QString::fromStdString(run.utf8)) == squeezed(texts[index]));
    // The engine's box is the ink and a margin around it: the run starts just left of and
    // below the drawn line, and is a little longer.
    CHECK(near(run.transform.e, inks[index].left() * scale, 24.0 * scale));
    CHECK(near(run.transform.f, (inks[index].bottom() + 1) * scale, 24.0 * scale));
    CHECK(run.intended_width >= inks[index].width() * scale * 0.95);
    CHECK(run.intended_width <= (inks[index].width() + 60) * scale);
  }
#if defined(PATCHY_HAVE_QT_PDF)
  const auto found = squeezed(qt_pdf_text(path, 0));
  CHECK(found.contains(squeezed(first_text)));
  CHECK(found.contains(squeezed(second_text)));
#endif
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_searchable_text_ui_tests() {
  return {
      {"ui_pdf_searchable_text_line_places_round_trip", ui_pdf_searchable_text_line_places_round_trip},
      {"ui_pdf_searchable_text_export_writes_recognized_lines", ui_pdf_searchable_text_export_writes_recognized_lines},
      {"ui_pdf_searchable_text_follows_edited_and_moved_layers",
       ui_pdf_searchable_text_follows_edited_and_moved_layers},
      {"ui_pdf_searchable_text_keeps_unicode_and_columns", ui_pdf_searchable_text_keeps_unicode_and_columns},
      {"ui_pdf_searchable_text_rotated_pass_through_page", ui_pdf_searchable_text_rotated_pass_through_page},
      {"ui_pdf_searchable_text_root_group_is_not_a_page", ui_pdf_searchable_text_root_group_is_not_a_page},
      {"ui_pdf_searchable_text_editable_export_notes_hidden_text",
       ui_pdf_searchable_text_editable_export_notes_hidden_text},
      {"ui_pdf_searchable_text_option_shows_only_with_recognized_text",
       ui_pdf_searchable_text_option_shows_only_with_recognized_text},
      {"ui_script_pdf_searchable_text_option", ui_script_pdf_searchable_text_option},
      {"ui_pdf_searchable_text_real_engine_reads_back_if_available",
       ui_pdf_searchable_text_real_engine_reads_back_if_available},
  };
}
