// The PDF viewer and its Print as Book (docs/pdf-viewer.md, docs/pdf-book-print.md).

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#if defined(PATCHY_HAVE_QT_PDF)

#include "core/book_imposition.hpp"
#include "import_print_resolution_test_support.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/pdf_book_print_dialog.hpp"
#include "ui/pdf_page_view.hpp"
#include "ui/pdf_viewer_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTest>
#include <QTextBrowser>
#include <QToolBar>
#include <QWheelEvent>

#include <cmath>
#include <string>
#include <vector>

using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::process_events_until;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::when_dialog_visible;
using patchy::test::ui::import_print_resolution_support::pdf_from_objects;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::PdfPageView;
using patchy::ui::PdfViewerWindow;

namespace {

constexpr double kPageWidth = 200.0;
constexpr double kPageHeight = 300.0;

// Page i is filled with gray 0.1 + 0.07 i, so a sample names the page, and has a black
// square at its top-left corner, so a sample shows which way up it is.
double page_gray(int page) {
  return 0.1 + 0.07 * page;
}

std::string number(double value) {
  return QString::number(value, 'f', 3).toStdString();
}

QByteArray numbered_pdf_bytes(int pages) {
  std::vector<std::string> objects;
  objects.emplace_back("<</Type/Catalog/Pages 2 0 R>>");
  std::string kids;
  for (int page = 0; page < pages; ++page) {
    kids += std::to_string(3 + 2 * page) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(pages) + ">>");
  for (int page = 0; page < pages; ++page) {
    const std::string gray = number(page_gray(page));
    const std::string content = gray + " " + gray + " " + gray + " rg 0 0 " + number(kPageWidth) + " " +
                                number(kPageHeight) + " re f 0 0 0 rg 0 " + number(kPageHeight - 20.0) +
                                " 20 20 re f";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + number(kPageWidth) + " " + number(kPageHeight) +
                      "]/Contents " + std::to_string(4 + 2 * page) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
  }
  return pdf_from_objects(objects);
}

// One 200 x 200 point page in four colours: red top left, green top right, blue bottom
// left, yellow bottom right.
QByteArray quadrant_pdf_bytes() {
  const std::string content =
      "1 0 0 rg 0 100 100 100 re f 0 1 0 rg 100 100 100 100 re f 0 0 1 rg 0 0 100 100 re f "
      "1 1 0 rg 100 0 100 100 re f";
  return pdf_from_objects({"<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R]/Count 1>>",
                           "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]/Contents 4 0 R>>",
                           "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content +
                               "\nendstream"});
}

QString write_file(const QString& path, const QByteArray& bytes) {
  QFile::remove(path);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(bytes);
  return path;
}

// The page a sample shows: -1 for white paper (a blank page or no page).
int page_at(const QImage& image, int x, int y) {
  const QColor color = image.pixelColor(x, y);
  if (color.red() > 245 && color.green() > 245 && color.blue() > 245) {
    return -1;
  }
  return static_cast<int>(std::lround((color.redF() - 0.1) / 0.07));
}

int dark_pixels(const QImage& image, const QRect& area) {
  int count = 0;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      if (qGray(image.pixel(x, y)) < 20) {
        ++count;
      }
    }
  }
  return count;
}

bool near_color(const QColor& color, int r, int g, int b) {
  return std::abs(color.red() - r) < 40 && std::abs(color.green() - g) < 40 && std::abs(color.blue() - b) < 40;
}

void wait_for_renders(PdfPageView& view) {
  CHECK(process_events_until([&view] { return !view.render_pending(); }, 15000));
}

void send_wheel(QWidget* target, int delta) {
  const QPointF position(target->width() / 2.0, target->height() / 2.0);
  QWheelEvent event(position, target->mapToGlobal(position), QPoint(), QPoint(0, delta), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(target, &event);
}

void ui_pdf_viewer_opens_pdf_files() {
  SettingsValueRestorer viewer_setting(QStringLiteral("imports/pdfOpenInViewer"));
  SettingsValueRestorer mode_setting(QStringLiteral("imports/pdfMode"));
  SettingsValueRestorer target_setting(QStringLiteral("imports/pdfPagesTarget"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), true);
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_open.pdf"), numbered_pdf_bytes(5));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = MainWindowTestAccess::session_count(window);

  // Open goes to the viewer: no import dialog, no document.
  MainWindowTestAccess::open_document_path_as_user(window, path);
  auto* viewer = window.findChild<PdfViewerWindow*>();
  CHECK(viewer != nullptr);
  if (viewer == nullptr) {
    return;
  }
  CHECK(viewer->isVisible());
  CHECK(MainWindowTestAccess::session_count(window) == sessions_before);
  CHECK(viewer->page_view()->page_count() == 5);
  CHECK(viewer->findChild<QPushButton*>(QStringLiteral("pdfViewerEditButton")) != nullptr);
  CHECK(viewer->findChild<QPushButton*>(QStringLiteral("pdfViewerPrintBookButton")) != nullptr);
  // Opening the same file again raises that viewer.
  MainWindowTestAccess::open_document_path_as_user(window, path);
  CHECK(window.findChildren<PdfViewerWindow*>().size() == 1);

  // Edit...: the Import PDF dialog starts on the viewer's page and opens it for editing;
  // the viewer then closes.
  viewer->page_view()->go_to_page(2);
  CHECK(viewer->page_view()->current_page() == 2);
  bool saw_dialog = false;
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [&saw_dialog](QDialog& dialog) {
    saw_dialog = true;
    auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* target = dialog.findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* import_button = dialog.findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    CHECK(pages != nullptr && mode != nullptr && target != nullptr && resolution != nullptr &&
          import_button != nullptr);
    if (pages == nullptr || mode == nullptr || target == nullptr || resolution == nullptr ||
        import_button == nullptr) {
      dialog.reject();
      return;
    }
    const auto selected = pages->selectedItems();
    CHECK(selected.size() == 1 && pages->row(selected.front()) == 2);
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("flatten"))));
    target->setCurrentIndex(std::max(0, target->findData(QStringLiteral("documents"))));
    resolution->setCurrentText(QStringLiteral("72"));
    import_button->click();
  });
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->edit_pages();
  CHECK(saw_dialog);
  CHECK(MainWindowTestAccess::session_count(window) == sessions_before + 1);
  CHECK(MainWindowTestAccess::document(window).width() == static_cast<int>(kPageWidth));
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
  // The page read last is remembered for the next open.
  MainWindowTestAccess::open_document_path_as_user(window, path);
  viewer = window.findChild<PdfViewerWindow*>();
  CHECK(viewer != nullptr && viewer->page_view()->current_page() == 2);
  if (viewer != nullptr) {
    const QPointer<PdfViewerWindow> second(viewer);
    viewer->close();
    CHECK(process_events_until([&second] { return second.isNull(); }));
  }

  // Preferences can turn the viewer off: Open shows the Import PDF dialog again. (Another
  // file: the page imported above is open, and opening its file just switches to it.)
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), false);
  const QString other = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_off.pdf"), numbered_pdf_bytes(2));
  bool saw_import = false;
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [&saw_import](QDialog& dialog) {
    saw_import = true;
    dialog.reject();
  });
  MainWindowTestAccess::open_document_path_as_user(window, other);
  CHECK(saw_import);
  CHECK(window.findChild<PdfViewerWindow*>() == nullptr);
}

void ui_pdf_viewer_spreads_and_page_turns() {
  SettingsValueRestorer pages_setting(QStringLiteral("pdfViewer/pages"));
  SettingsValueRestorer continuous_setting(QStringLiteral("pdfViewer/continuous"));
  SettingsValueRestorer direction_setting(QStringLiteral("pdfViewer/direction"));
  SettingsValueRestorer zoom_setting(QStringLiteral("pdfViewer/zoom"));
  SettingsValueRestorer geometry_setting(QStringLiteral("pdfViewer/geometry"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_spreads.pdf"), numbered_pdf_bytes(7));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer == nullptr) {
    return;
  }
  viewer->resize(900, 700);
  viewer->show();
  QApplication::processEvents();
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPagesCoverAlone);
  view->first_page();
  CHECK(view->current_pages() == std::vector<int>({0}));
  view->next_row();
  CHECK(view->current_pages() == std::vector<int>({1, 2}));
  // Right to left: the left arrow goes forward.
  view->set_reading_direction(patchy::PageReadingDirection::RightToLeft);
  QTest::keyClick(view, Qt::Key_Left);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  QTest::keyClick(view, Qt::Key_Right);
  CHECK(view->current_pages() == std::vector<int>({1, 2}));
  view->set_reading_direction(patchy::PageReadingDirection::LeftToRight);
  // One notch of the wheel at the page edge turns the page; the next notch right after it
  // is the tail of the same gesture and turns nothing.
  send_wheel(view->viewport(), -120);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  send_wheel(view->viewport(), -120);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  QTest::keyClick(view, Qt::Key_End);
  CHECK(view->current_pages() == std::vector<int>({5, 6}));
  QTest::keyClick(view, Qt::Key_Home);
  CHECK(view->current_pages() == std::vector<int>({0}));
  // The toolbar's page field follows.
  auto* page_spin = viewer->findChild<QSpinBox*>(QStringLiteral("pdfViewerPageSpin"));
  CHECK(page_spin != nullptr && page_spin->value() == 1 && page_spin->maximum() == 7);
  if (page_spin != nullptr) {
    page_spin->setValue(6);
    CHECK(view->current_pages() == std::vector<int>({5, 6}));
  }
  wait_for_renders(*view);
  CHECK(view->cached_width_for_testing(5) > 0 && view->cached_width_for_testing(6) > 0);
  viewer->grab().save(QStringLiteral("test-artifacts/ui_pdf_viewer_spreads.png"));

  // Continuous: the wheel scrolls through the pages instead.
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::Width);
  view->first_page();
  QApplication::processEvents();
  const int before = view->verticalScrollBar()->value();
  send_wheel(view->viewport(), -120);
  CHECK(view->verticalScrollBar()->value() > before);
  CHECK(view->verticalScrollBar()->maximum() > 0);

  // Zoom steps and actual size.
  view->set_zoom_percent(100.0);
  CHECK(std::abs(view->zoom_percent() - 100.0) < 1e-6 && view->fit_mode() == patchy::PageFitMode::None);
  view->zoom_in();
  CHECK(std::abs(view->zoom_percent() - 125.0) < 1e-6);

  // Full screen hides the chrome; Esc brings it back.
  viewer->set_full_screen(true);
  CHECK(viewer->full_screen_mode() && !viewer->findChild<QToolBar*>(QStringLiteral("pdfViewerToolbar"))->isVisible());
  QTest::keyClick(view, Qt::Key_Escape);
  CHECK(!viewer->full_screen_mode() && viewer->findChild<QToolBar*>(QStringLiteral("pdfViewerToolbar"))->isVisible());
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->close();
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
  // The layout choices persist.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/pages")).toString() == QStringLiteral("single"));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/continuous")).toBool());
}

// The view renders the visible part of a page zoomed past a whole-page image through Qt PDF's
// scaled clip rectangle, and turns the page itself. Both are pinned here: a clip must give
// the same pixels as that part of the whole page, and each quarter turn must show the right
// corner of the page at the top left.
void ui_pdf_viewer_detail_renders_match_the_page() {
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_quadrants.pdf"), quadrant_pdf_bytes());
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  QPdfDocumentRenderOptions options;
  options.setScaledSize(QSize(200, 200));
  options.setScaledClipRect(QRect(100, 0, 100, 100));
  const QImage top_right = document.render(0, QSize(100, 100), options);
  CHECK(near_color(top_right.pixelColor(50, 50), 0, 255, 0));
  options.setScaledClipRect(QRect(0, 100, 100, 100));
  const QImage bottom_left = document.render(0, QSize(100, 100), options);
  CHECK(near_color(bottom_left.pixelColor(50, 50), 0, 0, 255));

  SettingsValueRestorer zoom_setting(QStringLiteral("pdfViewer/zoom"));
  SettingsValueRestorer continuous_setting(QStringLiteral("pdfViewer/continuous"));
  SettingsValueRestorer geometry_setting(QStringLiteral("pdfViewer/geometry"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer == nullptr) {
    return;
  }
  viewer->resize(800, 600);
  viewer->show();
  QApplication::processEvents();
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_zoom_percent(1600.0);
  // Top left of the page, past the 16 px margin.
  const auto top_left_color = [view] {
    view->horizontalScrollBar()->setValue(0);
    view->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    wait_for_renders(*view);
    return view->viewport()->grab().toImage().pixelColor(60, 60);
  };
  CHECK(near_color(top_left_color(), 255, 0, 0));
  // The page is far past a whole-page image: the sharp render is a detail of it.
  CHECK(view->cached_width_for_testing(0) >= 3000);
  view->rotate_clockwise();  // a quarter turn clockwise brings bottom left to the top left
  CHECK(near_color(top_left_color(), 0, 0, 255));
  view->rotate_clockwise();
  CHECK(near_color(top_left_color(), 255, 255, 0));
  view->rotate_clockwise();
  CHECK(near_color(top_left_color(), 0, 255, 0));
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->close();
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
}

void ui_pdf_book_print_imposes_sheets() {
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_book_pages.pdf"), numbered_pdf_bytes(6));
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  patchy::ui::PdfBookPrintOptions options;
  options.paper = QPageSize::Letter;
  options.marks = false;
  constexpr double kDpi = 36.0;  // a Letter sheet is 396 x 306
  const auto side = [&](int sheet, bool back) {
    const auto imposition = patchy::ui::book_imposition_for(options, document.pageCount());
    const auto pages = patchy::ui::book_source_pages(options, document.pageCount());
    return patchy::ui::render_book_sheet_side(document, options, imposition, pages, sheet, back, kDpi);
  };
  // Booklet of 6 pages padded to 8: [blank|1][2|blank] and [6|3][4|5] (one-based).
  QImage image = side(0, false);
  CHECK(image.size() == QSize(396, 306));
  CHECK(page_at(image, 99, 153) == -1 && page_at(image, 297, 153) == 0);
  image = side(0, true);
  CHECK(page_at(image, 99, 153) == 1 && page_at(image, 297, 153) == -1);
  image = side(1, false);
  CHECK(page_at(image, 99, 153) == 5 && page_at(image, 297, 153) == 2);
  // The last page as the back cover: it shares the outside of sheet 1 with the cover.
  options.last_page_is_back_cover = true;
  image = side(0, false);
  CHECK(page_at(image, 99, 153) == 5 && page_at(image, 297, 153) == 0);
  // Backs for a long-edge printer are turned half way round.
  options.turn_backs = true;
  image = side(1, true);
  CHECK(page_at(image, 99, 153) == 4 && page_at(image, 297, 153) == 3);
  options.turn_backs = false;
  options.last_page_is_back_cover = false;
  // Right to left mirrors the sheet: the cover on the left half.
  options.right_to_left = true;
  image = side(0, false);
  CHECK(page_at(image, 99, 153) == 0 && page_at(image, 297, 153) == -1);
  options.right_to_left = false;
  // A page range prints only those pages.
  options.first_page = 2;
  options.last_page = 5;
  image = side(0, false);
  CHECK(page_at(image, 99, 153) == 5 && page_at(image, 297, 153) == 2);
  options.first_page = 0;
  options.last_page = -1;

  // The mini zine: four by two, top row upside down.
  options.style = patchy::BookPrintStyle::MiniZine;
  image = side(0, false);
  const int cell_w = 99;
  const int cell_h = 153;
  const auto centre = [&](int column, int row) { return page_at(image, column * cell_w + 49, row * cell_h + 76); };
  CHECK(centre(0, 0) == 4 && centre(1, 0) == 3 && centre(2, 0) == 2 && centre(3, 0) == 1);
  CHECK(centre(0, 1) == 5 && centre(1, 1) == -1 && centre(2, 1) == -1 && centre(3, 1) == 0);
  // The black corner square: bottom right of an upside-down panel, top left of an upright one.
  const QRect top_cell(0, 0, cell_w, cell_h);
  const QRect bottom_cell(0, cell_h, cell_w, cell_h);
  const auto upper_left = [](QRect cell) { return QRect(cell.topLeft(), cell.size() / 2); };
  const auto lower_right = [](QRect cell) {
    return QRect(cell.topLeft() + QPoint(cell.width() / 2, cell.height() / 2), cell.size() / 2);
  };
  CHECK(dark_pixels(image, lower_right(top_cell)) > 4 && dark_pixels(image, upper_left(top_cell)) == 0);
  CHECK(dark_pixels(image, upper_left(bottom_cell)) > 4 && dark_pixels(image, lower_right(bottom_cell)) == 0);

  // The steps name what the first sheet shows.
  options.style = patchy::BookPrintStyle::Booklet;
  const auto imposition = patchy::ui::book_imposition_for(options, document.pageCount());
  const QStringList steps = patchy::ui::book_assembly_steps(options, imposition, document.pageCount());
  CHECK(steps.size() >= 5);
  CHECK(steps.join(QLatin1Char('\n')).contains(QStringLiteral("a blank page and page 1")));
}

void ui_pdf_book_print_writes_sheets_pdf() {
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_book_source.pdf"), numbered_pdf_bytes(6));
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  patchy::ui::PdfBookPrintOptions options;
  options.paper = QPageSize::Letter;
  options.dpi = 150;
  options.steps_page = true;
  const QString out = QStringLiteral("test-artifacts/ui_pdf_book_sheets.pdf");
  QFile::remove(out);
  int calls = 0;
  const auto result = patchy::ui::write_pdf_book(document, options, out, [&calls](int done, int count) {
    ++calls;
    return done <= count;
  });
  CHECK(result.ok && result.pages == 5 && calls == 5);
  QPdfDocument written;
  CHECK(written.load(out) == QPdfDocument::Error::None);
  CHECK(written.pageCount() == 5);
  const QSizeF sheet = written.pagePointSize(0);
  CHECK(std::abs(sheet.width() - 792.0) < 0.5 && std::abs(sheet.height() - 612.0) < 0.5);
  const QImage front = written.render(0, QSize(396, 306));
  CHECK(page_at(front, 297, 153) == 0);
  // Cancel keeps the file that was there.
  const auto size_before = QFileInfo(out).size();
  const auto cancelled = patchy::ui::write_pdf_book(document, options, out, [](int, int) { return false; });
  CHECK(cancelled.cancelled && !cancelled.ok && QFileInfo(out).size() == size_before);

  // A Unicode folder and name, both for the viewer's open and the book written from it.
  const auto directory = patchy::test::unicode_artifact_dir(u8"pdf-viewer");
  const auto unicode_source = patchy::ui::to_qstring(
      directory / patchy::test::unicode_path_piece(std::u8string(patchy::test::kUnicodeCombinedStem) + u8".pdf"));
  write_file(unicode_source, numbered_pdf_bytes(4));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = PdfViewerWindow::open(&window, unicode_source, {});
  CHECK(viewer != nullptr && viewer->page_view()->page_count() == 4);
  if (viewer != nullptr) {
    const auto unicode_out = patchy::ui::to_qstring(
        directory / patchy::test::unicode_path_piece(std::u8string(patchy::test::kUnicodePathStems[0]) + u8" book.pdf"));
    options.steps_page = false;
    options.dpi = 72;
    const auto unicode_result = patchy::ui::write_pdf_book(*viewer->document(), options, unicode_out);
    CHECK(unicode_result.ok && unicode_result.pages == 2 && QFileInfo::exists(unicode_out));
    const QPointer<PdfViewerWindow> guard(viewer);
    viewer->close();
    CHECK(process_events_until([&guard] { return guard.isNull(); }));
  }
}

void ui_pdf_book_print_dialog_follows_choices() {
  SettingsValueRestorer style_setting(QStringLiteral("pdfBookPrint/style"));
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_book_dialog.pdf"), numbered_pdf_bytes(6));
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  when_dialog_visible(QStringLiteral("pdfBookPrintDialog"), [&saw_dialog](QDialog& dialog) {
    saw_dialog = true;
    auto* style = dialog.findChild<QComboBox*>(QStringLiteral("pdfBookPrintStyleCombo"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("pdfBookPrintSummaryLabel"));
    auto* sheet = dialog.findChild<QSpinBox*>(QStringLiteral("pdfBookPrintSheetSpin"));
    auto* steps = dialog.findChild<QTextBrowser*>(QStringLiteral("pdfBookPrintStepsView"));
    auto* front = dialog.findChild<QLabel*>(QStringLiteral("pdfBookPrintFrontPreview"));
    auto* back = dialog.findChild<QLabel*>(QStringLiteral("pdfBookPrintBackPreview"));
    CHECK(style != nullptr && summary != nullptr && sheet != nullptr && steps != nullptr && front != nullptr &&
          back != nullptr);
    if (style == nullptr || summary == nullptr || sheet == nullptr || steps == nullptr || front == nullptr ||
        back == nullptr) {
      dialog.reject();
      return;
    }
    style->setCurrentIndex(std::max(0, style->findData(QStringLiteral("booklet"))));
    CHECK(sheet->maximum() == 2);
    CHECK(process_events_until([front] { return !front->pixmap().isNull(); }, 10000));
    CHECK(steps->toPlainText().contains(QStringLiteral("Staple")));
    dialog.grab().save(QStringLiteral("test-artifacts/ui_pdf_book_print_dialog.png"));
    // The zine is one-sided: no second side to preview.
    style->setCurrentIndex(std::max(0, style->findData(QStringLiteral("zine"))));
    CHECK(sheet->maximum() == 1 && !back->isVisible());
    CHECK(summary->text().contains(QStringLiteral("2 blank page")));
    CHECK(steps->toPlainText().contains(QStringLiteral("cut")));
    dialog.reject();
  });
  patchy::ui::PdfBookPrintStart start;
  patchy::ui::run_pdf_book_print_dialog(&window, document, path, start);
  CHECK(saw_dialog);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfBookPrint/style")).toString() == QStringLiteral("zine"));
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_viewer_ui_tests() {
  return {
      {"ui_pdf_viewer_opens_pdf_files", ui_pdf_viewer_opens_pdf_files},
      {"ui_pdf_viewer_spreads_and_page_turns", ui_pdf_viewer_spreads_and_page_turns},
      {"ui_pdf_viewer_detail_renders_match_the_page", ui_pdf_viewer_detail_renders_match_the_page},
      {"ui_pdf_book_print_imposes_sheets", ui_pdf_book_print_imposes_sheets},
      {"ui_pdf_book_print_writes_sheets_pdf", ui_pdf_book_print_writes_sheets_pdf},
      {"ui_pdf_book_print_dialog_follows_choices", ui_pdf_book_print_dialog_follows_choices},
  };
}

#else

std::vector<patchy::test::TestCase> pdf_viewer_ui_tests() {
  return {};
}

#endif
