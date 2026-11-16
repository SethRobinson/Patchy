// Text recognition in the PDF viewer (docs/pdf-viewer-ocr.md): Find and text selection on
// pages that have no text of their own. The scripted fake engine stands in for a real one,
// so every expectation is against the lines it reports.

#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#if defined(PATCHY_HAVE_QT_PDF)

#include "import_print_resolution_test_support.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_page_fixture.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/pdf_ocr_text.hpp"
#include "ui/pdf_page_ocr.hpp"
#include "ui/pdf_page_view.hpp"
#include "ui/pdf_text_search.hpp"
#include "ui/pdf_text_selection.hpp"
#include "ui/pdf_viewer_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRectF>
#include <QStringList>
#include <QTransform>

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::ocr::Box;
using patchy::ocr::FakeEngineConfig;
using patchy::ocr::FakeLine;
using patchy::test::ui::ocr_test_font;
using patchy::test::ui::OcrTestPage;
using patchy::test::ui::process_events_until;
using patchy::test::ui::ScopedOcrEngines;
using patchy::test::ui::SettingsGroupRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::when_dialog_visible;
using patchy::test::ui::import_print_resolution_support::pdf_from_objects;
using patchy::ui::PdfPageOcr;
using patchy::ui::PdfViewerWindow;

// The pages are 600 x 400 px scans on 288 x 192 pt pages. The viewer reads a page at 300
// pixels per inch (1200 x 800), and the fake engine reports its boxes as scripted, so a
// scripted pixel is 0.24 pt on the page.
constexpr int kPageWidth = 600;
constexpr int kPageHeight = 400;
constexpr double kPointsPerPixel = 288.0 / 1200.0;

bool near(double value, double expected, double tolerance) {
  return std::abs(value - expected) <= tolerance;
}

std::string jpeg_of(const QImage& page) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "JPEG");
  writer.setQuality(92);
  CHECK(writer.write(page.convertToFormat(QImage::Format_RGB888)));
  return bytes.toStdString();
}

// One entry per page: a scan (its JPEG) or, when empty, a page of real text ("Hello PDF").
QByteArray pdf_of(const std::vector<std::string>& page_jpegs) {
  std::vector<std::string> objects;
  objects.emplace_back("<</Type/Catalog/Pages 2 0 R>>");
  std::string kids;
  for (std::size_t index = 0; index < page_jpegs.size(); ++index) {
    kids += std::to_string(3 + index * 3) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(page_jpegs.size()) + ">>");
  for (std::size_t index = 0; index < page_jpegs.size(); ++index) {
    const auto first = 3 + index * 3;
    const auto& jpeg = page_jpegs[index];
    const bool scan = !jpeg.empty();
    const std::string content = scan ? "q 288 0 0 192 0 0 cm /Im0 Do Q"
                                     : "BT /F1 18 Tf 1 0 0 1 20 100 Tm 0 0 0 rg (Hello PDF) Tj ET";
    const std::string resources = scan ? "/XObject<</Im0 " + std::to_string(first + 2) + " 0 R>>"
                                       : "/Font<</F1 " + std::to_string(first + 2) + " 0 R>>";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 288 192]/Resources<<" + resources + ">>/Contents " +
                      std::to_string(first + 1) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    if (scan) {
      objects.push_back("<</Type/XObject/Subtype/Image/Width " + std::to_string(kPageWidth) + "/Height " +
                        std::to_string(kPageHeight) +
                        "/ColorSpace/DeviceRGB/BitsPerComponent 8/Filter/DCTDecode/Length " +
                        std::to_string(jpeg.size()) + ">>\nstream\n" + jpeg + "\nendstream");
    } else {
      // A width for every character, so a reader sees the letters side by side.
      std::string widths;
      for (int code = 32; code <= 122; ++code) {
        widths += "500 ";
      }
      objects.push_back("<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding"
                        "/FirstChar 32/LastChar 122/Widths[" + widths + "]>>");
    }
  }
  return pdf_from_objects(objects);
}

QString write_pdf(const char* name, const QByteArray& bytes) {
  QDir().mkpath(QStringLiteral("test-artifacts"));
  const auto path = QStringLiteral("test-artifacts/%1.pdf").arg(QLatin1String(name));
  QFile::remove(path);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(bytes);
  return path;
}

struct CopyLog {
  QStringList copied;
};

PdfViewerWindow* open_viewer(patchy::ui::MainWindow& window, const QString& path, CopyLog* log = nullptr) {
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer != nullptr) {
    viewer->set_link_handlers([](const QUrl&) { return true; }, [log](const QString& text) {
      if (log != nullptr) {
        log->copied.append(text);
      }
    });
    viewer->resize(900, 700);
    viewer->show();
    QApplication::processEvents();
  }
  return viewer;
}

void close_viewer(PdfViewerWindow* viewer) {
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->close();
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
}

// Waits until recognition, the search and the selection have nothing left to do.
void settle(PdfViewerWindow& viewer) {
  CHECK(process_events_until(
      [&viewer] {
        return !viewer.page_ocr()->running() && !viewer.page_ocr()->busy() && !viewer.text_search()->busy() &&
               !viewer.page_view()->text_selection()->busy();
      },
      30000));
}

// Arms a driver that accepts (or cancels) the engine and language dialog.
void answer_recognize_dialog(bool accept, std::shared_ptr<bool> saw) {
  when_dialog_visible(QStringLiteral("pdfViewerRecognizeDialog"), [accept, saw](QDialog& dialog) {
    auto* engine = dialog.findChild<QComboBox*>(QStringLiteral("pdfViewerOcrEngineCombo"));
    auto* ok = dialog.findChild<QPushButton*>(QStringLiteral("pdfViewerRecognizeOkButton"));
    auto* note = dialog.findChild<QLabel*>(QStringLiteral("pdfViewerRecognizeNote"));
    CHECK(engine != nullptr && ok != nullptr && note != nullptr);
    if (note != nullptr) {
      CHECK(note->text().contains(QStringLiteral("Nothing is saved to the PDF")));
    }
    *saw = true;
    if (accept && ok != nullptr && ok->isEnabled()) {
      ok->click();
    } else {
      dialog.reject();
    }
  });
}

struct ScanFixture {
  SettingsGroupRestorer ocr_settings{QStringLiteral("ocr")};
  OcrTestPage page{kPageWidth, kPageHeight};
  std::shared_ptr<patchy::ocr::FakeEngine> fake;
  std::unique_ptr<ScopedOcrEngines> engines;
  patchy::ui::MainWindow window;

  explicit ScanFixture(int delay_ms = 0, std::string fail_with = {}) {
    page.draw(QStringLiteral("Scanned page heading"), 40, 40, ocr_test_font(40));
    page.draw(QStringLiteral("A line further down"), 40, 250, ocr_test_font(40));
    FakeEngineConfig config;
    config.lines = page.lines;
    config.delay_ms = delay_ms;
    config.fail_with = std::move(fail_with);
    fake = patchy::ocr::make_fake_engine(std::move(config));
    engines = std::make_unique<ScopedOcrEngines>(std::vector<std::shared_ptr<patchy::ocr::Engine>>{fake});
    show_window(window);
  }

  [[nodiscard]] std::string scan() const { return jpeg_of(page.image); }
  // A scripted line's box on the page, in points.
  [[nodiscard]] QRectF line_points(std::size_t index) const {
    const auto& box = page.lines[index].box;
    return QRectF(box.x * kPointsPerPixel, box.y * kPointsPerPixel, box.width * kPointsPerPixel,
                  box.height * kPointsPerPixel);
  }
};

void type_in_find(PdfViewerWindow& viewer, const QString& text) {
  viewer.open_find_bar();
  auto* edit = viewer.findChild<QLineEdit*>(QStringLiteral("pdfViewerFindEdit"));
  CHECK(edit != nullptr);
  if (edit != nullptr) {
    edit->setText(text);
  }
  CHECK(process_events_until([&viewer] { return !viewer.text_search()->busy(); }, 30000));
}

QString find_status(PdfViewerWindow& viewer) {
  auto* label = viewer.findChild<QLabel*>(QStringLiteral("pdfViewerFindStatusLabel"));
  return label != nullptr ? label->text() : QString();
}

void ui_pdf_viewer_ocr_text_model() {
  // A level line with two words and a column, read from an 800 x 600 image of a 400 x 300 pt page.
  patchy::ocr::Result result;
  patchy::ocr::Line level;
  level.text = "Hello world";
  level.box = Box{100, 50, 220, 20};
  level.quad = patchy::ocr::quad_from_box(level.box);
  level.words.push_back({"Hello", Box{100, 50, 90, 20}, patchy::ocr::quad_from_box(Box{100, 50, 90, 20})});
  level.words.push_back({"world", Box{210, 50, 110, 20}, patchy::ocr::quad_from_box(Box{210, 50, 110, 20})});
  patchy::ocr::Line column;
  column.text = "ABCD";
  column.box = Box{400, 50, 20, 200};
  column.quad = patchy::ocr::quad_from_box(column.box);
  column.vertical = true;
  patchy::ocr::Line empty;
  empty.text = "   ";
  empty.box = Box{10, 10, 10, 10};
  empty.quad = patchy::ocr::quad_from_box(empty.box);
  result.lines = {level, empty, column};
  const auto to_page = patchy::ui::pdf_ocr_image_to_page(QSize(800, 600), QSizeF(400.0, 300.0), 0);
  const auto page = patchy::ui::pdf_ocr_page_text(result, to_page);
  CHECK(page.has_text());
  CHECK(page.text == QStringLiteral("Hello world\nABCD"));
  CHECK(page.lines.size() == 2);
  CHECK(page.line_boxes().size() == 2);
  if (page.lines.size() != 2) {
    return;
  }
  CHECK(page.lines[0].start == 0 && page.lines[0].count == 11);
  CHECK(page.lines[1].start == 12 && page.lines[1].count == 4);
  // A word sits where the engine put it; the space is the gap between two words.
  auto rects = page.range_rects(0, 5);
  CHECK(rects.size() == 1);
  if (rects.size() == 1) {
    CHECK(near(rects[0].left(), 50.0, 0.01) && near(rects[0].right(), 95.0, 0.01));
    CHECK(near(rects[0].top(), 25.0, 0.01) && near(rects[0].bottom(), 35.0, 0.01));
  }
  rects = page.range_rects(6, 5);
  CHECK(rects.size() == 1 && near(rects[0].left(), 105.0, 0.01) && near(rects[0].right(), 160.0, 0.01));
  // A range over both lines is a rectangle on each; the line break has none of its own.
  rects = page.range_rects(6, 8);
  CHECK(rects.size() == 2);
  if (rects.size() == 2) {
    // The column's first two of four characters: its upper half.
    CHECK(near(rects[1].left(), 200.0, 0.01) && near(rects[1].right(), 210.0, 0.01));
    CHECK(near(rects[1].top(), 25.0, 0.01) && near(rects[1].bottom(), 75.0, 0.01));
  }
  // Carets: the ends of the line, the middle of a word, the nearest line for a point off any.
  CHECK(page.caret_at(QPointF(50.0, 30.0)) == 0);
  CHECK(page.caret_at(QPointF(30.0, 30.0)) == 0);
  CHECK(page.caret_at(QPointF(161.0, 30.0)) == 11);
  CHECK(page.caret_at(QPointF(59.5, 28.0)) == 1);  // the first letter ends at 59
  CHECK(page.caret_at(QPointF(205.0, 76.0)) == 14);  // half way down the column
  CHECK(page.caret_at(QPointF(205.0, 500.0)) == 16);
  CHECK(page.line_range_at(QPointF(100.0, 31.0)) == std::pair(0, 11));
  CHECK(page.line_range_at(QPointF(207.0, 90.0)) == std::pair(12, 4));
  // Nothing recognized: no text, no caret.
  const auto nothing = patchy::ui::pdf_ocr_page_text(patchy::ocr::Result{}, to_page);
  CHECK(!nothing.has_text() && nothing.caret_at(QPointF(1.0, 1.0)) == -1 && nothing.range_rects(0, 5).isEmpty());

  // A page shown turned: the image is read as the user sees it, and its places go back to
  // the page as stored. Checked against the turn Qt itself gives an image.
  const QSizeF stored(400.0, 300.0);
  for (int turns = 0; turns < 4; ++turns) {
    const QTransform turn = QImage::trueMatrix(QTransform().rotate(90.0 * turns), 400, 300);
    const QSize shown = turns % 2 == 0 ? QSize(400, 300) : QSize(300, 400);
    const auto back = patchy::ui::pdf_ocr_image_to_page(shown, stored, turns);
    for (const QPointF point : {QPointF(0.0, 0.0), QPointF(400.0, 0.0), QPointF(120.0, 250.0)}) {
      const QPointF there = back.map(turn.map(point));
      CHECK(near(there.x(), point.x(), 1e-6) && near(there.y(), point.y(), 1e-6));
    }
  }
}

void ui_pdf_viewer_ocr_find_and_select_on_a_scan() {
  ScanFixture fixture;
  const auto path = write_pdf("ui_pdf_viewer_ocr_scan", pdf_of({fixture.scan(), fixture.scan()}));
  CopyLog log;
  auto* viewer = open_viewer(fixture.window, path, &log);
  if (viewer == nullptr) {
    return;
  }
  auto* ocr = viewer->page_ocr();
  auto* search = viewer->text_search();
  auto* view = viewer->page_view();
  auto* find_button = viewer->findChild<QPushButton*>(QStringLiteral("pdfViewerFindRecognizeButton"));
  auto* side_button = viewer->findChild<QPushButton*>(QStringLiteral("pdfViewerRecognizeButton"));
  auto* side_status = viewer->findChild<QLabel*>(QStringLiteral("pdfViewerRecognizeStatusLabel"));
  CHECK(find_button != nullptr && side_button != nullptr && side_status != nullptr);
  if (find_button == nullptr || side_button == nullptr || side_status == nullptr) {
    close_viewer(viewer);
    return;
  }

  // Opening and searching reads nothing: Find says there is no text and offers to read it.
  type_in_find(*viewer, QStringLiteral("heading"));
  CHECK(find_status(*viewer) == QStringLiteral("This PDF has no searchable text"));
  CHECK(find_button->isVisible());
  CHECK(side_status->isHidden());
  CHECK(fixture.fake->calls().empty());
  CHECK(ocr->recognized_pages() == 0 && !ocr->running());

  // Cancelling the question reads nothing either.
  auto saw = std::make_shared<bool>(false);
  answer_recognize_dialog(false, saw);
  find_button->click();
  CHECK(*saw);
  CHECK(!ocr->running() && fixture.fake->calls().empty());

  // Asked for: both pages are read, at 300 pixels per inch, and Find has its matches.
  *saw = false;
  answer_recognize_dialog(true, saw);
  find_button->click();
  CHECK(*saw);
  settle(*viewer);
  const auto calls = fixture.fake->calls();
  CHECK(calls.size() == 2);
  for (const auto& call : calls) {
    CHECK(call.image_width == 1200 && call.image_height == 800);
  }
  CHECK(ocr->page_state(0) == PdfPageOcr::PageState::Recognized);
  CHECK(ocr->page_state(1) == PdfPageOcr::PageState::Recognized);
  CHECK(search->match_count() == 2);
  CHECK(find_status(*viewer) == QStringLiteral("1 of 2"));
  CHECK(!find_button->isVisible());
  CHECK(side_status->isVisible() && side_status->text().contains(QStringLiteral("2 page")));
  CHECK(side_button->text() == QStringLiteral("Recognize Text..."));
  // The match is the last word of the first line: at the right end of that line's box.
  const QRectF heading = fixture.line_points(0);
  const auto* matches = search->page_matches(0);
  CHECK(matches != nullptr && matches->rects.size() == 1 && matches->rects[0].size() == 1);
  if (matches != nullptr && matches->rects.size() == 1 && matches->rects[0].size() == 1) {
    const QRectF rect = matches->rects[0][0];
    CHECK(near(rect.right(), heading.right(), 0.05));
    CHECK(rect.left() > heading.center().x());
    CHECK(near(rect.top(), heading.top(), 0.05) && near(rect.bottom(), heading.bottom(), 0.05));
  }
  // A phrase across the two lines is found too, as on a page with text of its own.
  type_in_find(*viewer, QStringLiteral("heading a line"));
  CHECK(search->match_count() == 2);
  viewer->close_find_bar();

  // Selection: all of a page, a word, a line, and a range between two places.
  auto* selection = view->text_selection();
  const auto text = ocr->page_text(0);
  CHECK(text != nullptr && text->text == QStringLiteral("Scanned page heading\nA line further down"));
  if (text == nullptr) {
    close_viewer(viewer);
    return;
  }
  CHECK(selection->text_at(0, heading.center(), 1.0));
  CHECK(!selection->text_at(0, QPointF(280.0, 120.0), 1.0));
  selection->select_pages(0, 0);
  settle(*viewer);
  CHECK(view->selected_text() == QStringLiteral("Scanned page heading\nA line further down"));
  const QPointF in_page_word = text->range_rects(8, 4).constFirst().center();
  selection->select_word(0, in_page_word);
  settle(*viewer);
  CHECK(view->selected_text() == QStringLiteral("page"));
  const auto* word_rects = selection->page_rects(0);
  CHECK(word_rects != nullptr && word_rects->size() == 1 && word_rects->constFirst().contains(in_page_word));
  selection->select_line(0, fixture.line_points(1).center());
  settle(*viewer);
  CHECK(view->selected_text() == QStringLiteral("A line further down"));
  selection->set_anchor(0, QPointF(heading.left(), heading.center().y()));
  selection->select_to(0, text->range_rects(8, 4).constFirst().bottomRight());
  settle(*viewer);
  CHECK(view->selected_text() == QStringLiteral("Scanned page"));
  view->copy_selection();
  CHECK(!log.copied.isEmpty() && log.copied.constLast() == QStringLiteral("Scanned page"));
  // Across the two pages.
  selection->select_pages(0, 1);
  settle(*viewer);
  CHECK(view->selected_text() ==
        QStringLiteral("Scanned page heading\nA line further down\nScanned page heading\nA line further down"));
  close_viewer(viewer);

  // The same file again: nothing carries over from the window that read it.
  viewer = open_viewer(fixture.window, path);
  if (viewer == nullptr) {
    return;
  }
  type_in_find(*viewer, QStringLiteral("heading"));
  CHECK(find_status(*viewer) == QStringLiteral("This PDF has no searchable text"));
  CHECK(viewer->page_ocr()->recognized_pages() == 0);
  CHECK(fixture.fake->calls().size() == 2);
  close_viewer(viewer);
}

void ui_pdf_viewer_ocr_reads_only_pages_without_text() {
  ScanFixture fixture;
  const auto path = write_pdf("ui_pdf_viewer_ocr_mixed", pdf_of({std::string(), fixture.scan(), fixture.scan()}));
  auto* viewer = open_viewer(fixture.window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* ocr = viewer->page_ocr();
  // One page asked for (the page menu's entry): only that page is read.
  auto saw = std::make_shared<bool>(false);
  answer_recognize_dialog(true, saw);
  viewer->recognize_text({2});
  CHECK(*saw);
  settle(*viewer);
  CHECK(fixture.fake->calls().size() == 1);
  CHECK(ocr->page_state(2) == PdfPageOcr::PageState::Recognized);
  CHECK(ocr->page_state(0) == PdfPageOcr::PageState::Unread && ocr->page_state(1) == PdfPageOcr::PageState::Unread);
  // Every page: the page with text of its own is left alone, the page already read is not
  // read again.
  *saw = false;
  answer_recognize_dialog(true, saw);
  viewer->recognize_text();
  CHECK(*saw);
  settle(*viewer);
  CHECK(fixture.fake->calls().size() == 2);
  CHECK(ocr->page_state(0) == PdfPageOcr::PageState::OwnText);
  CHECK(ocr->page_state(1) == PdfPageOcr::PageState::Recognized);
  CHECK(ocr->page_text(0) == nullptr);
  CHECK(ocr->recognized_pages() == 2);
  // Find takes the page's own text and the recognized text alike.
  type_in_find(*viewer, QStringLiteral("hello"));
  CHECK(viewer->text_search()->match_count() == 1);
  CHECK(viewer->text_search()->current_match().page == 0);
  type_in_find(*viewer, QStringLiteral("further"));
  CHECK(viewer->text_search()->match_count() == 2);
  CHECK(find_status(*viewer) == QStringLiteral("1 of 2"));
  close_viewer(viewer);
}

void ui_pdf_viewer_ocr_stop_failure_and_close() {
  {
    // A slow engine: Stop ends the run after the page in flight, and what was read stays.
    ScanFixture fixture(250);
    const auto path = write_pdf("ui_pdf_viewer_ocr_stop", pdf_of({fixture.scan(), fixture.scan(), fixture.scan()}));
    auto* viewer = open_viewer(fixture.window, path);
    if (viewer == nullptr) {
      return;
    }
    auto* ocr = viewer->page_ocr();
    auto* side_button = viewer->findChild<QPushButton*>(QStringLiteral("pdfViewerRecognizeButton"));
    auto* side_status = viewer->findChild<QLabel*>(QStringLiteral("pdfViewerRecognizeStatusLabel"));
    CHECK(side_button != nullptr && side_status != nullptr);
    auto saw = std::make_shared<bool>(false);
    answer_recognize_dialog(true, saw);
    if (side_button != nullptr) {
      side_button->click();
    }
    CHECK(*saw && ocr->running());
    if (side_button != nullptr && side_status != nullptr) {
      CHECK(side_button->text() == QStringLiteral("Stop Recognizing"));
      CHECK(side_status->text() == QStringLiteral("Recognizing page 1 of 3..."));
    }
    CHECK(process_events_until([ocr] { return ocr->pages_done() >= 1; }, 30000));
    // The same button stops it: no question this time.
    if (side_button != nullptr) {
      side_button->click();
    }
    settle(*viewer);
    CHECK(!ocr->running());
    CHECK(ocr->recognized_pages() >= 1 && ocr->recognized_pages() < 3);
    if (side_button != nullptr && side_status != nullptr) {
      CHECK(side_button->text() == QStringLiteral("Recognize Text..."));
      CHECK(side_status->text().contains(QStringLiteral("Text was recognized on")));
    }
    // The pages left over are read by the next run; closing the window in the middle of
    // that one is safe.
    *saw = false;
    answer_recognize_dialog(true, saw);
    viewer->recognize_text();
    CHECK(*saw && ocr->running());
    close_viewer(viewer);
  }
  {
    // The engine fails: the run ends with its words, and the page stays unread.
    ScanFixture fixture(0, "engine broke");
    const auto path = write_pdf("ui_pdf_viewer_ocr_fail", pdf_of({fixture.scan(), fixture.scan()}));
    auto* viewer = open_viewer(fixture.window, path);
    if (viewer == nullptr) {
      return;
    }
    auto saw = std::make_shared<bool>(false);
    answer_recognize_dialog(true, saw);
    viewer->recognize_text();
    settle(*viewer);
    auto* side_status = viewer->findChild<QLabel*>(QStringLiteral("pdfViewerRecognizeStatusLabel"));
    CHECK(side_status != nullptr && side_status->text() == QStringLiteral("Text recognition stopped: engine broke"));
    CHECK(viewer->page_ocr()->page_state(0) == PdfPageOcr::PageState::Unread);
    CHECK(viewer->page_ocr()->recognized_pages() == 0);
    close_viewer(viewer);
  }
  {
    // No engine at all: the question says why and cannot be accepted.
    patchy::ui::MainWindow window;
    show_window(window);
    OcrTestPage page(kPageWidth, kPageHeight);
    page.draw(QStringLiteral("No engine"), 40, 40, ocr_test_font(40));
    const auto path = write_pdf("ui_pdf_viewer_ocr_none", pdf_of({jpeg_of(page.image)}));
    auto* viewer = open_viewer(window, path);
    if (viewer == nullptr) {
      return;
    }
    bool saw = false;
    when_dialog_visible(QStringLiteral("pdfViewerRecognizeDialog"), [&saw](QDialog& dialog) {
      auto* ok = dialog.findChild<QPushButton*>(QStringLiteral("pdfViewerRecognizeOkButton"));
      auto* note = dialog.findChild<QLabel*>(QStringLiteral("pdfViewerOcrUnavailableNote"));
      CHECK(ok != nullptr && !ok->isEnabled());
      CHECK(note != nullptr && note->isVisibleTo(&dialog) && !note->text().isEmpty());
      saw = true;
      dialog.reject();
    });
    viewer->recognize_text();
    CHECK(saw && !viewer->page_ocr()->running());
    close_viewer(viewer);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_viewer_ocr_ui_tests() {
  return {
      {"ui_pdf_viewer_ocr_text_model", ui_pdf_viewer_ocr_text_model},
      {"ui_pdf_viewer_ocr_find_and_select_on_a_scan", ui_pdf_viewer_ocr_find_and_select_on_a_scan},
      {"ui_pdf_viewer_ocr_reads_only_pages_without_text", ui_pdf_viewer_ocr_reads_only_pages_without_text},
      {"ui_pdf_viewer_ocr_stop_failure_and_close", ui_pdf_viewer_ocr_stop_failure_and_close},
  };
}

#else

std::vector<patchy::test::TestCase> pdf_viewer_ocr_ui_tests() {
  return {};
}

#endif
