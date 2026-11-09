// Scan a Book to PDF (docs/book-scan.md): the wizard driven offscreen through the test
// scanner (PATCHY_FAKE_BOOK_SCANNER), the project manifest, page order, the PDF it
// writes, and app.buildBookPdf.
#include "core/book_scan.hpp"
#include "ui/app_settings.hpp"
#include "ui/book_pdf_export.hpp"
#include "ui/book_scan_dialog.hpp"
#include "ui/book_scan_project.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_qss.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTransform>
#include <QtGlobal>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

using patchy::test::ui::process_events_until;
using patchy::test::ui::scripting_support::backlog_contains;
using patchy::test::ui::scripting_support::run_script;
using patchy::test::ui::show_window;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kDpi = 300;

struct EnvGuard {
  EnvGuard(const char* name, const QByteArray& value) : name_(name) { qputenv(name, value); }
  ~EnvGuard() { qunsetenv(name_); }
  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;

private:
  const char* name_;
};

bool inside(const patchy::OrientedBox& box, double x, double y, double& local_x, double& local_y) {
  const double radians = box.angle_degrees * kPi / 180.0;
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  const double dx = x - box.center_x;
  const double dy = y - box.center_y;
  local_x = dx * c + dy * s + box.width / 2.0;
  local_y = -dx * s + dy * c + box.height / 2.0;
  return local_x >= 0.0 && local_y >= 0.0 && local_x < box.width && local_y < box.height;
}

// A whole-bed scan (the test scanner takes each image as the bed at the requested
// resolution): a dark open-lid background with the book on it. `spread` true draws
// two pages with a fold shadow and text lines; false draws a colored cover.
QString write_bed(const QString& folder, const QString& name, int width, int height, const patchy::OrientedBox& book,
                  bool spread, int marker) {
  QImage image(width, height, QImage::Format_RGB888);
  image.fill(QColor(22, 22, 22));
  for (int y = 0; y < height; ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < width; ++x) {
      double lx = 0.0;
      double ly = 0.0;
      if (!inside(book, x + 0.5, y + 0.5, lx, ly)) {
        continue;
      }
      int r = 40;
      int g = 70;
      int b = 150;
      if (spread) {
        const double fold = std::abs(lx - book.width / 2.0) / book.width;
        int value = fold < 0.02 ? 150 + static_cast<int>(fold * 4000.0) : 238;
        const double page_x = std::fmod(lx, book.width / 2.0) / (book.width / 2.0);
        if (page_x > 0.15 && page_x < 0.85 && ly > book.height * 0.15 && ly < book.height * 0.85 &&
            static_cast<int>(ly) % 16 < 4) {
          value = 40;
        }
        // A page-number block in the top outer corner tells the pages apart.
        if (ly < book.height * 0.08 && ((lx < book.width * 0.08) || (lx > book.width * 0.92))) {
          value = marker;
        }
        r = g = b = std::min(value, 255);
      }
      row[x * 3] = static_cast<uchar>(r);
      row[x * 3 + 1] = static_cast<uchar>(g);
      row[x * 3 + 2] = static_cast<uchar>(b);
    }
  }
  const QString path = QDir(folder).filePath(name);
  image.save(path, "PNG");
  return path;
}

struct BookFixture {
  QString project_folder;
  QString inputs;
};

QString fresh_artifact_folder(const QString& name) {
  patchy::test::ui::ensure_artifact_dir();
  const QString folder = QDir::current().filePath(QStringLiteral("test-artifacts/") + name);
  if (QDir(folder).exists()) {
    patchy::test::ui::remove_test_scratch_dir(folder);
  }
  QDir().mkpath(folder);
  return folder;
}

// Cover plus `spreads` spreads, 3 x 2 inch bed at 300 dpi; the open book is 2.4 x 1.6
// inches (two 1.2 x 1.6 inch pages).
QStringList write_book_inputs(const QString& folder, int spreads) {
  QStringList files;
  files << write_bed(folder, QStringLiteral("cover.png"), 900, 600, {450.0, 300.0, 720.0, 480.0, 0.0}, false, 0);
  for (int i = 0; i < spreads; ++i) {
    const double shift = (i % 2 == 0 ? -1.0 : 1.0) * 12.0 * (i + 1);
    const double angle = i == 1 ? 1.5 : 0.0;
    files << write_bed(folder, QStringLiteral("spread-%1.png").arg(i + 1), 900, 600,
                       {450.0 + shift, 300.0 - shift / 2.0, 720.0, 480.0, angle}, true, 60 + i * 20);
  }
  return files;
}

void wait_for_scan(patchy::ui::BookScanDialog& dialog) {
  CHECK(process_events_until([&dialog] { return !dialog.scan_in_progress(); }, 10000));
}

// Runs the wizard to the review step with the given inputs. `capture` saves each step
// as a test artifact under that prefix.
void scan_book(patchy::ui::BookScanDialog& dialog, const QString& project_folder, int spreads,
               const std::string& capture = {}) {
  dialog.set_quiet(true);
  if (!capture.empty()) {
    dialog.show();
  }
  dialog.set_project_folder(project_folder);
  dialog.set_title(QStringLiteral("Test Book"));
  if (!capture.empty()) {
    QApplication::processEvents();
    patchy::test::ui::save_widget_artifact(capture + "_setup", dialog);
  }
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Cover);
  if (auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"))) {
    resolution->setCurrentIndex(resolution->findData(kDpi));
  }
  if (!capture.empty()) {
    patchy::test::ui::save_widget_artifact(capture + "_cover", dialog);
  }
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  for (int i = 0; i < spreads; ++i) {
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty());
  }
  if (!capture.empty()) {
    patchy::test::ui::save_widget_artifact(capture + "_scanning", dialog);
  }
  dialog.finish_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Review);
  if (!capture.empty()) {
    patchy::test::ui::save_widget_artifact(capture + "_review", dialog);
  }
}

QStringList media_boxes(const QByteArray& pdf) {
  QStringList boxes;
  const QRegularExpression pattern(QStringLiteral(R"(/MediaBox \[([^\]]*)\])"));
  auto it = pattern.globalMatch(QString::fromLatin1(pdf));
  while (it.hasNext()) {
    boxes << it.next().captured(1);
  }
  return boxes;
}

QString utf16_hex(const QString& text) {
  QString hex = QStringLiteral("FEFF");
  for (const QChar c : text) {
    hex += QStringLiteral("%1").arg(static_cast<uint>(c.unicode()), 4, 16, QLatin1Char('0')).toUpper();
  }
  return hex;
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Cancel Scan answers at once (the button greys out as "Cancelling...", the status says so, and
// a "Cancelling Scan" notice opens), and all of it goes away when the scanner reports back.
void ui_book_scan_cancel_shows_feedback() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_cancel_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_cancel_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 1).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Cancel Book"));
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCancelButton"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("bookScanStatusLabel"));
  CHECK(cancel != nullptr && status != nullptr);
  if (cancel == nullptr || status == nullptr) {
    return;
  }
  const auto notice = [&dialog] {
    return dialog.findChild<QProgressDialog*>(QStringLiteral("bookScanCancellingDialog"));
  };
  {
    const EnvGuard slow("PATCHY_FAKE_BOOK_SCANNER_DELAY_MS", "400");
    dialog.start_page_scan();
    CHECK(dialog.scan_in_progress() && cancel->isVisible() && cancel->isEnabled());
    cancel->click();
    CHECK(dialog.scan_in_progress());
    CHECK(!cancel->isEnabled() && cancel->text() == QStringLiteral("Cancelling..."));
    CHECK(status->text() == QStringLiteral("Cancelling the scan..."));
    CHECK(notice() != nullptr && notice()->isVisible());
    if (notice() != nullptr) {
      patchy::test::ui::save_widget_artifact("ui_book_scan_cancelling_notice", *notice());
    }
    wait_for_scan(dialog);
  }
  CHECK(process_events_until([&notice] { return notice() == nullptr; }));
  CHECK(status->text() == QStringLiteral("Scan cancelled."));
  CHECK(!cancel->isVisible() && cancel->isEnabled() && cancel->text() == QStringLiteral("Cancel Scan"));
  CHECK(dialog.project().scans.empty());
  // The next scan runs as usual and gets the image the cancelled one did not use.
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 1U);
  dialog.reject();
}

// The setup step: the step bar, and the guide card with its picture and numbered steps for a
// new book (hidden when the folder already holds one), in Dark and Light.
void ui_book_scan_setup_page_guides_the_cover_scan() {
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_setup_project")) + QStringLiteral("/book");
  // The test scanner, listed but never asked to scan here.
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", "unused.png");
  auto& manager = patchy::ui::ThemeManager::instance();
  const patchy::ui::ColorSchemePreference entry_preference = manager.preference();
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Field Notes"));
  auto* bar = dialog.findChild<QWidget*>(QStringLiteral("bookScanStepBar"));
  auto* guide = dialog.findChild<QWidget*>(QStringLiteral("bookScanSetupGuide"));
  auto* picture = dialog.findChild<QWidget*>(QStringLiteral("bookScanPlacementPicture"));
  auto* heading = dialog.findChild<QLabel*>(QStringLiteral("bookScanGuideHeading"));
  auto* cover = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
  CHECK(bar != nullptr && guide != nullptr && picture != nullptr && heading != nullptr && cover != nullptr);
  if (bar == nullptr || guide == nullptr || picture == nullptr || heading == nullptr || cover == nullptr) {
    return;
  }
  QApplication::processEvents();
  CHECK(bar->isVisible() && guide->isVisible() && picture->isVisible());
  CHECK(heading->text() == QStringLiteral("Scan the cover first"));
  // Large where it is drawn: the page's sheet, not a setFont the application sheet replaces.
  CHECK(QFontInfo(heading->font()).pixelSize() >= 20);
  CHECK(cover->isVisible() && cover->isEnabled() && cover->isDefault());
  CHECK(bar->accessibleName() == QStringLiteral("1. Set up and scan the cover"));
  patchy::test::ui::save_widget_artifact("ui_book_scan_setup_dark", dialog);
  // MainWindow re-resolves every window's themed sheets on a scheme change; here the test does.
  manager.set_preference(patchy::ui::ColorSchemePreference::Light, /*persist=*/false);
  patchy::ui::rebuild_themed_styles_in(dialog);
  QApplication::processEvents();
  patchy::test::ui::save_widget_artifact("ui_book_scan_setup_light", dialog);
  manager.set_preference(entry_preference, /*persist=*/false);
  patchy::ui::rebuild_themed_styles_in(dialog);
  QApplication::processEvents();
  // A folder that holds a book hides the guide: Continue This Book is the next step.
  patchy::ui::BookProject book;
  book.folder = project;
  book.title = QStringLiteral("Field Notes");
  QDir().mkpath(project);
  patchy::ui::save_book_project(book);
  dialog.set_project_folder(QString());
  dialog.set_project_folder(project);
  CHECK(guide->isHidden());
  dialog.reject();
}

QStringList turned_inputs(const QStringList& files);

// A book scanned without a cover can still say which way its top points: in the cover step
// before any scan, and in the scanning step once the first scan shows it lying sideways.
void ui_book_scan_skip_cover_sets_top_of_book() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_skip_cover_inputs"));
  const QString project =
      fresh_artifact_folder(QStringLiteral("ui_book_scan_skip_cover_project")) + QStringLiteral("/book");
  // Two spreads and no cover scan, each lying with the top of the book to the right.
  const QStringList files = turned_inputs(write_book_inputs(inputs, 2).mid(1));
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "2x3");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Sideways Book"));
  const QVariant include_covers = patchy::ui::app_settings().value(QStringLiteral("bookScan/includeCovers"));
  dialog.skip_cover();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Cover);
  CHECK(!dialog.project().cover.has_value());
  auto* cover_up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUpCombo"));
  CHECK(cover_up != nullptr && cover_up->isEnabled());
  if (cover_up == nullptr) {
    return;
  }
  // Set before the first scan, with nothing scanned yet to turn.
  cover_up->setCurrentIndex(2);  // Down
  CHECK(dialog.project().up_turns == 2);
  cover_up->setCurrentIndex(0);  // Up: the user does not know yet
  CHECK(dialog.project().up_turns == 0);
  if (auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"))) {
    resolution->setCurrentIndex(resolution->findData(kDpi));
  }
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 1U);
  auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningUpCombo"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("bookScanStatusLabel"));
  CHECK(up != nullptr && status != nullptr);
  if (up == nullptr || status == nullptr || dialog.project().scans.empty()) {
    return;
  }
  // The first scan shows the book on its side: the scanning step turns it upright.
  CHECK(!up->isHidden() && up->isEnabled());
  patchy::test::ui::save_widget_artifact("ui_book_scan_skip_cover_sideways", dialog);
  up->setCurrentIndex(1);  // Right
  CHECK(dialog.project().up_turns == 3);
  CHECK(status->text() == QStringLiteral("Every scan of this book was turned."));
  const auto check_upright = [&dialog](std::size_t index) {
    const auto& spread = dialog.project().scans[index].layout.spread;
    CHECK(std::abs(spread.width - 720.0) < 20.0 && std::abs(spread.height - 480.0) < 20.0);
    CHECK(std::abs(spread.angle_degrees) < 3.0);
    CHECK(std::abs(dialog.project().scans[index].layout.gutter - 0.5) < 0.03);
  };
  check_upright(0);
  patchy::test::ui::save_widget_artifact("ui_book_scan_skip_cover_turned", dialog);
  // Tilt reaches a quarter turn either way, so one page laid sideways can be turned alone.
  auto* tilt = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("bookScanSpreadTiltSpin"));
  auto* cover_tilt = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("bookScanCoverTiltSpin"));
  CHECK(tilt != nullptr && cover_tilt != nullptr);
  if (tilt != nullptr && cover_tilt != nullptr) {
    CHECK(tilt->minimum() == -90.0 && tilt->maximum() == 90.0);
    CHECK(cover_tilt->minimum() == -90.0 && cover_tilt->maximum() == 90.0);
    dialog.select_scan_for_editing(0);
    const double before = dialog.project().scans[0].layout.spread.angle_degrees;
    tilt->setValue(90.0);
    CHECK(dialog.project().scans[0].layout.spread.angle_degrees == 90.0);
    tilt->setValue(before);
    CHECK(dialog.project().scans[0].layout.spread.angle_degrees == before);
    dialog.back_to_next_pages();
  }
  // The next scan comes in upright, and the turn is saved with the book.
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 2U);
  if (dialog.project().scans.size() == 2U) {
    check_upright(1);
  }
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->up_turns == 3 && !saved->cover.has_value());
  // A book without a cover leaves the next book's Include covers choice alone.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("bookScan/includeCovers")) == include_covers);
  dialog.reject();
}

void ui_book_scan_menu_actions_registered() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* scan_action = window.findChild<QAction*>(QStringLiteral("fileImportScanBookAction"));
  // Continuing a book is part of the wizard's setup step, not a menu command of its own.
  CHECK(window.findChild<QAction*>(QStringLiteral("fileImportResumeBookScanAction")) == nullptr);
  CHECK(window.hotkey_registry().find_command(QStringLiteral("file.import_resume_book_scan")) == nullptr);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
  CHECK(scan_action != nullptr);
  auto* import_menu = window.findChild<QMenu*>(QStringLiteral("fileImportMenu"));
  CHECK(import_menu != nullptr && import_menu->actions().contains(scan_action));
  const auto* command = window.hotkey_registry().find_command(QStringLiteral("file.import_scan_book_pdf"));
  CHECK(command != nullptr && command->action == scan_action);
  CHECK(scan_action->menuRole() == QAction::NoRole);
#else
  CHECK(scan_action == nullptr);
#endif
}

void ui_book_scan_full_flow_exports_uniform_pages() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 3).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  scan_book(dialog, project, 3, "ui_book_scan_step");
  const auto& book = dialog.project();
  CHECK(book.cover.has_value());
  CHECK(book.scans.size() == 3);
  CHECK(book.mode == patchy::ui::BookScanMode::TwoPages);
  // The cover outline was found on the dark background: 2.4 x 1.6 inches.
  CHECK(std::abs(book.cover->layout.outline.width / book.cover->dpi - 2.4) < 0.05);
  // Every spread found its fold near the middle and its own tilt.
  for (const auto& scan : book.scans) {
    CHECK(std::abs(scan.layout.gutter - 0.5) < 0.02);
  }
  CHECK(std::abs(book.scans[1].layout.spread.angle_degrees - 1.5) < 0.5);
  const auto size = patchy::ui::resolved_page_size(book);
  CHECK(std::abs(size.width - 1.2) < 0.05);
  CHECK(std::abs(size.height - 1.6) < 0.05);
  // Front cover, six pages, back cover.
  const auto order = patchy::ui::book_page_order(book);
  CHECK(order.size() == 8);
  CHECK(order.front().kind == patchy::ui::BookPageRef::Kind::FrontCover);
  CHECK(order.back().kind == patchy::ui::BookPageRef::Kind::BackCover);
  CHECK(order[1].scan == 0 && order[1].side == patchy::BookPageSide::Left);
  CHECK(order[2].scan == 0 && order[2].side == patchy::BookPageSide::Right);

  const QString pdf_path = QDir(inputs).filePath(QStringLiteral("book.pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  const QByteArray pdf = read_file(pdf_path);
  const QStringList boxes = media_boxes(pdf);
  CHECK(boxes.size() == 8);
  CHECK(QSet<QString>(boxes.begin(), boxes.end()).size() == 1);  // every page the same size
  CHECK(pdf.contains(utf16_hex(QStringLiteral("Test Book")).toLatin1()));
  CHECK(pdf.contains("/DCTDecode"));

  // The first page of the first spread starts with its fore-edge marker in the top-left
  // corner (gray 60), the right page with it in the top-right corner.
  patchy::ui::BookScanCache cache(book);
  const auto left = patchy::ui::render_book_page(book, order[1], size, 100, cache);
  const auto right = patchy::ui::render_book_page(book, order[2], size, 100, cache);
  CHECK(left.width() == 120 && left.height() == 160);
  CHECK(std::abs(static_cast<int>(left.pixel(3, 3)[0]) - 60) < 25);
  CHECK(std::abs(static_cast<int>(right.pixel(right.width() - 4, 3)[0]) - 60) < 25);
  // The tilted spread comes out straight: a text line's row is dark at both its ends.
  const auto tilted = patchy::ui::render_book_page(book, order[3], size, 300, cache, false);
  patchy::test::ui::save_image_artifact("ui_book_scan_tilted_page", patchy::ui::book_image_from_pixels(tilted));
  int straight_rows = 0;
  for (std::int32_t y = tilted.height() / 4; y < tilted.height() * 3 / 4; ++y) {
    const auto at = [&](std::int32_t x) { return static_cast<int>(tilted.pixel(x, y)[0]); };
    if (at(tilted.width() * 3 / 10) < 120 && at(tilted.width() * 7 / 10) < 120) {
      ++straight_rows;
    }
  }
  CHECK(straight_rows > 40);
}

void ui_book_scan_black_and_white_and_script_export() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_bw_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_bw_project")) + QStringLiteral("/book");
  {
    const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
    const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
    patchy::ui::BookScanDialog dialog(nullptr);
    scan_book(dialog, project, 2);
    auto* output = dialog.findChild<QComboBox*>(QStringLiteral("bookScanOutputCombo"));
    CHECK(output != nullptr);
    output->setCurrentIndex(2);  // Black and white
    CHECK(dialog.project().output.mode == patchy::BookOutputMode::BlackAndWhite);
    const QString pdf_path = QDir(inputs).filePath(QStringLiteral("bw.pdf"));
    CHECK(dialog.export_pdf(pdf_path, false));
    const QByteArray pdf = read_file(pdf_path);
    CHECK(pdf.contains("/BitsPerComponent 1"));
    CHECK(media_boxes(pdf).size() == 6);
  }
  // The saved project exports again from a script, with overrides.
  patchy::ui::MainWindow window;
  show_window(window);
  const QString script_pdf = QDir(inputs).filePath(QStringLiteral("script.pdf"));
  const auto json_string = [](const QString& text) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
  };
  CHECK(run_script(window, QStringLiteral(R"JS(
    var pages = app.buildBookPdf(%1, %2, { mode: 'gray', pageSize: [1.25, 1.75], dpi: 150, title: 'Scripted' });
    if (pages !== 6) throw new Error('pages ' + pages);
    var threw = false;
    try { app.buildBookPdf(%1, %2, { mode: 'sepia' }); } catch (e) { threw = true; }
    if (!threw) throw new Error('bad mode accepted');
    threw = false;
    try { app.buildBookPdf(%2, %2); } catch (e) { threw = true; }
    if (!threw) throw new Error('non-project folder accepted');
    console.log('book-ok');
  )JS")
                                  .arg(json_string(project), json_string(script_pdf))));
  CHECK(backlog_contains(window, QStringLiteral("book-ok")));
  const QByteArray pdf = read_file(script_pdf);
  CHECK(pdf.contains("/DeviceGray"));
  CHECK(pdf.contains("/MediaBox [0 0 90 126]"));
  CHECK(pdf.contains(utf16_hex(QStringLiteral("Scripted")).toLatin1()));
}

void ui_book_scan_resume_and_rescan() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_resume_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_resume_project")) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 2);
  // A third spread image serves the rescan.
  files << files.last();
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  QByteArray saved_json;
  {
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.set_project_folder(project);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    dialog.start_page_scanning();
    dialog.start_page_scan();
    wait_for_scan(dialog);
    dialog.start_page_scan();
    wait_for_scan(dialog);
    const QString second = dialog.project().scans.back().file;
    dialog.rescan_last();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty());
    // The rescan replaced the last spread under its own name.
    CHECK(dialog.project().scans.size() == 2);
    CHECK(dialog.project().scans.back().file == second);
    CHECK(QFile::exists(QDir(project).filePath(second)));
    CHECK(QDir(project).entryList({QStringLiteral("*-new.png")}, QDir::Files).isEmpty());
    saved_json = patchy::ui::book_project_json(dialog.project());
    // Closing mid-scan keeps the project for Resume.
  }
  QString error;
  auto loaded = patchy::ui::load_book_project(project, &error);
  CHECK(loaded.has_value());
  if (!loaded) {
    return;
  }
  CHECK(error.isEmpty());
  CHECK(patchy::ui::book_project_json(*loaded) == saved_json);
  {
    // The next wizard opens on the same folder and offers to continue its book.
    patchy::ui::BookScanDialog resumed(nullptr);
    resumed.set_quiet(true);
    auto* folder_edit = resumed.findChild<QLineEdit*>(QStringLiteral("bookScanFolderEdit"));
    CHECK(folder_edit != nullptr && QDir::fromNativeSeparators(folder_edit->text()) == QDir(project).absolutePath());
    auto* continue_button = resumed.findChild<QPushButton*>(QStringLiteral("bookScanContinueButton"));
    auto* cover_button = resumed.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
    CHECK(continue_button != nullptr && !continue_button->isHidden());
    CHECK(cover_button != nullptr && cover_button->isHidden());
    auto* status = resumed.findChild<QLabel*>(QStringLiteral("bookScanFolderStatus"));
    CHECK(status != nullptr && status->text().contains(QStringLiteral("3")));
    resumed.show();
    patchy::test::ui::save_widget_artifact("ui_book_scan_step_setup_continue", resumed);
    resumed.continue_book();
    CHECK(resumed.last_error().isEmpty());
    CHECK(resumed.step() == patchy::ui::BookScanDialog::Step::Scanning);
    CHECK(resumed.project().scans.size() == 2);
  }

  // Not a book folder: a clear error, nothing created.
  QString not_book_error;
  CHECK(!patchy::ui::load_book_project(inputs, &not_book_error).has_value());
  CHECK(!not_book_error.isEmpty());
  // A manifest naming a file outside its folder is refused.
  QByteArray hostile = saved_json;
  hostile.replace("scans/spread-0001.png", "../../outside.png");
  CHECK(!patchy::ui::book_project_from_json(hostile, &not_book_error).has_value());
}

void ui_book_scan_one_page_mode_offered_for_wide_book() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_wide_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_wide_project")) + QStringLiteral("/book");
  // A 2 x 2 inch bed cannot hold the 2.4 inch open book.
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 0).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "2x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  // The cover cannot show whether a book that fills the glass fits open: two pages per
  // scan stays, with a notice offering one page per scan.
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::TwoPages);
  auto* notice = dialog.findChild<QLabel*>(QStringLiteral("bookScanModeNotice"));
  CHECK(notice != nullptr && !notice->text().isEmpty());
  auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("bookScanModeCombo"));
  CHECK(mode != nullptr && mode->currentIndex() == 0);
  if (mode != nullptr) {
    mode->setCurrentIndex(1);
  }
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::OnePage);
}

// Pages per scan changed after scanning: the scans so far are read again in the new mode.
void ui_book_scan_switch_pages_per_scan_redetects() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_switch_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_switch_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  for (int i = 0; i < 2; ++i) {
    dialog.start_page_scan();
    wait_for_scan(dialog);
  }
  auto* pages = dialog.findChild<QComboBox*>(QStringLiteral("bookScanPagesCombo"));
  auto* use = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUseCombo"));
  CHECK(pages != nullptr && pages->currentIndex() == 0 && use != nullptr && !use->isHidden());
  CHECK(patchy::ui::book_page_order(dialog.project()).size() == 6);
  if (pages == nullptr || use == nullptr) {
    return;
  }
  pages->setCurrentIndex(1);
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::OnePage);
  CHECK(use->isHidden());
  // Each scan is one page now: its box is the whole open book on the dark bed.
  CHECK(patchy::ui::book_page_order(dialog.project()).size() == 4);
  CHECK(std::abs(dialog.project().scans[0].layout.spread.width / dialog.project().scans[0].dpi - 2.4) < 0.1);
  pages->setCurrentIndex(0);
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::TwoPages);
  CHECK(!use->isHidden());
  CHECK(patchy::ui::book_page_order(dialog.project()).size() == 6);
  for (const auto& scan : dialog.project().scans) {
    CHECK(std::abs(scan.layout.gutter - 0.5) < 0.02);
  }
}

// The two spine lines start close together; either can be dragged past the other.
void ui_book_scan_spine_lines_drag_past_each_other() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_spine_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_spine_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 0).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  QApplication::processEvents();
  auto* preview = dialog.findChild<QWidget*>(QStringLiteral("bookScanCoverPreview"));
  CHECK(preview != nullptr && dialog.project().cover.has_value());
  if (preview == nullptr || !dialog.project().cover) {
    return;
  }
  // The cover scan is 900 x 600 pixels, shown fitted in the preview.
  const double scale = std::min(preview->width() / 900.0, preview->height() / 600.0);
  const QPointF origin((preview->width() - 900.0 * scale) / 2.0, (preview->height() - 600.0 * scale) / 2.0);
  const auto& outline = dialog.project().cover->layout.outline;
  const auto at = [&](double fraction) {
    const double x = outline.center_x + (fraction - 0.5) * outline.width;
    return (origin + QPointF(x, outline.center_y) * scale).toPoint();
  };
  const auto drag = [&](double from, double to) {
    QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, at(from));
    QTest::mouseMove(preview, at((from + to) / 2.0));
    QTest::mouseMove(preview, at(to));
    QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, at(to));
  };
  patchy::test::ui::save_widget_artifact("ui_book_scan_spine_before", dialog);
  const double left = dialog.project().cover->layout.spine_left;
  const double right = dialog.project().cover->layout.spine_right;
  // Press on the right line and drag it well right.
  drag(right, 0.65);
  CHECK(std::abs(dialog.project().cover->layout.spine_left - left) < 0.01);
  CHECK(std::abs(dialog.project().cover->layout.spine_right - 0.65) < 0.02);
  // Drag the left line past the right one: they swap roles and stay ordered.
  drag(left, 0.8);
  CHECK(std::abs(dialog.project().cover->layout.spine_left - 0.65) < 0.02);
  CHECK(std::abs(dialog.project().cover->layout.spine_right - 0.8) < 0.02);
  patchy::test::ui::save_widget_artifact("ui_book_scan_spine_after", dialog);
  // The edit is what the manifest holds.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->cover && std::abs(saved->cover->layout.spine_right - 0.8) < 0.02);
}

// Start Over deletes the old book's files but keeps the folder and anything else in it.
void ui_book_scan_start_over_keeps_folder() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_over_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_over_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 1).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  {
    patchy::ui::BookScanDialog dialog(nullptr);
    scan_book(dialog, project, 1);
    CHECK(dialog.export_pdf(QDir(project).filePath(QStringLiteral("Test Book.pdf")), false));
  }
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  auto* title = dialog.findChild<QLineEdit*>(QStringLiteral("bookScanTitleEdit"));
  CHECK(title != nullptr && title->text() == QStringLiteral("Test Book"));
  dialog.start_over();
  CHECK(QDir(project).exists());
  CHECK(!QFile::exists(QDir(project).filePath(QStringLiteral("book.json"))));
  CHECK(!QDir(project).exists(QStringLiteral("scans")));
  CHECK(QFile::exists(QDir(project).filePath(QStringLiteral("Test Book.pdf"))));
  auto* cover_button = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
  auto* continue_button = dialog.findChild<QPushButton*>(QStringLiteral("bookScanContinueButton"));
  CHECK(cover_button != nullptr && !cover_button->isHidden());
  CHECK(continue_button != nullptr && continue_button->isHidden());
  CHECK(title != nullptr && title->text().isEmpty());
}

void ui_book_scan_rtl_orders_pages() {
  patchy::ui::BookProject project;
  project.include_covers = true;
  project.cover = patchy::ui::BookCover{};
  project.scans.resize(2);
  project.scans[1].use = patchy::ui::BookPageUse::LeftOnly;
  project.direction = patchy::ui::BookReadingDirection::RightToLeft;
  const auto order = patchy::ui::generated_page_order(project);
  CHECK(order.size() == 5);
  CHECK(order[0].kind == patchy::ui::BookPageRef::Kind::FrontCover);
  CHECK(order[1].scan == 0 && order[1].side == patchy::BookPageSide::Right);
  CHECK(order[2].scan == 0 && order[2].side == patchy::BookPageSide::Left);
  CHECK(order[3].scan == 1 && order[3].side == patchy::BookPageSide::Left);
  CHECK(order[4].kind == patchy::ui::BookPageRef::Kind::BackCover);
  // A custom order wins, and a page's own turn round-trips through the manifest.
  project.custom_order = std::vector<patchy::ui::BookPageRef>{
      {patchy::ui::BookPageRef::Kind::Scan, 1, patchy::BookPageSide::Left, 2},
      {patchy::ui::BookPageRef::Kind::Blank, -1, patchy::BookPageSide::Left, 0}};
  project.cover->file = QStringLiteral("scans/cover.png");
  for (auto& scan : project.scans) {
    scan.file = QStringLiteral("scans/x.png");
  }
  const auto reloaded = patchy::ui::book_project_from_json(patchy::ui::book_project_json(project), nullptr);
  CHECK(reloaded.has_value());
  if (reloaded) {
    CHECK(patchy::ui::book_page_order(*reloaded) == *project.custom_order);
    CHECK(reloaded->direction == patchy::ui::BookReadingDirection::RightToLeft);
  }
}

// Each input image turned a quarter turn clockwise: the top of the book points right.
QStringList turned_inputs(const QStringList& files) {
  QStringList turned;
  for (const QString& file : files) {
    const QString path = QFileInfo(file).dir().filePath(QStringLiteral("right-") + QFileInfo(file).fileName());
    QImage(file).transformed(QTransform().rotate(90.0)).save(path, "PNG");
    turned << path;
  }
  return turned;
}

// Where a fraction across a box's width lands at its middle height, in the preview
// widget that shows an upright image of `image_size` fitted.
QPoint preview_point(const QWidget& preview, const QSize& image_size, const patchy::OrientedBox& box, double fraction) {
  const double scale = std::min(preview.width() / static_cast<double>(image_size.width()),
                                preview.height() / static_cast<double>(image_size.height()));
  const QPointF origin((preview.width() - image_size.width() * scale) / 2.0,
                       (preview.height() - image_size.height() * scale) / 2.0);
  const double radians = box.angle_degrees * kPi / 180.0;
  const double local_x = (fraction - 0.5) * box.width;
  const QPointF at(box.center_x + local_x * std::cos(radians), box.center_y + local_x * std::sin(radians));
  return (origin + at * scale).toPoint();
}

QSize upright_scan_size(const patchy::ui::BookProject& book, const QString& file, int turns) {
  const QSize raw = QImageReader(QDir(book.folder).filePath(file)).size();
  return turns % 2 != 0 ? raw.transposed() : raw;
}

// Drags the gutter line of the scan the scanning step shows to `to`.
void drag_gutter(patchy::ui::BookScanDialog& dialog, int scan_index, double to) {
  auto* preview = dialog.findChild<QWidget*>(QStringLiteral("bookScanSpreadPreview"));
  CHECK(preview != nullptr);
  if (preview == nullptr) {
    return;
  }
  const auto& book = dialog.project();
  const auto& scan = book.scans[static_cast<std::size_t>(scan_index)];
  const QSize size = upright_scan_size(book, scan.file, book.up_turns + scan.extra_turns);
  const auto box = scan.layout.spread;
  const double from = scan.layout.gutter;
  QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, preview_point(*preview, size, box, from));
  QTest::mouseMove(preview, preview_point(*preview, size, box, (from + to) / 2.0));
  QTest::mouseMove(preview, preview_point(*preview, size, box, to));
  QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, preview_point(*preview, size, box, to));
  QApplication::processEvents();
}

// The gutter dragged in the box editor stays where it was dropped (it jumped back when
// dragged to the right), through the next scan and a reopen of book.json, upright and
// with the top of the book pointing right (a quarter turn).
void check_gutter_drag_sticks(bool quarter_turn) {
  const QString tag = quarter_turn ? QStringLiteral("_turned") : QString();
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_gutter_inputs") + tag);
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_gutter_project") + tag) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 3);
  if (quarter_turn) {
    files = turned_inputs(files);
  }
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", quarter_turn ? "2x3" : "3x2");
  double dropped = 0.0;
  {
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(project);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    if (quarter_turn) {
      auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUpCombo"));
      CHECK(up != nullptr);
      if (up != nullptr) {
        up->setCurrentIndex(1);  // Right
      }
      CHECK(dialog.project().up_turns == 3);
    }
    dialog.start_page_scanning();
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.project().scans.size() == 1);
    if (dialog.project().scans.empty()) {
      return;
    }
    QApplication::processEvents();
    const double found = dialog.project().scans[0].layout.gutter;
    CHECK(std::abs(found - 0.5) < 0.03);
    // To the right first: on release the line used to jump back to where it was found.
    dropped = found + 0.08;
    drag_gutter(dialog, 0, dropped);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    CHECK(dialog.project().scans[0].user_edited);
    patchy::test::ui::save_widget_artifact("ui_book_scan_gutter_dragged" + tag.toStdString(), dialog);
    // And to the left.
    dropped = found - 0.06;
    drag_gutter(dialog, 0, dropped);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    // The next scan leaves the edited one alone.
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.project().scans.size() == 2);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    // An earlier scan picked from the strip takes a drag too, and only it changes.
    const double second = dialog.project().scans[1].layout.gutter;
    dialog.select_scan_for_editing(0);
    CHECK(dialog.selected_scan() == 0);
    dropped = found + 0.05;
    drag_gutter(dialog, 0, dropped);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    CHECK(std::abs(dialog.project().scans[1].layout.gutter - second) < 1e-9);
  }
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->scans.size() == 2);
  if (saved && !saved->scans.empty()) {
    CHECK(std::abs(saved->scans[0].layout.gutter - dropped) < 0.01);
    CHECK(saved->scans[0].user_edited);
  }
  // Continue This Book shows the gutter where it was left.
  patchy::ui::BookScanDialog resumed(nullptr);
  resumed.set_quiet(true);
  resumed.set_project_folder(project);
  resumed.continue_book();
  CHECK(resumed.step() == patchy::ui::BookScanDialog::Step::Scanning);
  CHECK(resumed.project().scans.size() == 2);
  if (resumed.project().scans.size() == 2) {
    CHECK(std::abs(resumed.project().scans[0].layout.gutter - dropped) < 0.01);
  }
}

void ui_book_scan_gutter_drag_sticks() {
  check_gutter_drag_sticks(false);
  check_gutter_drag_sticks(true);
}

// The strip shows the covers and every page; a click on one shows its scan with its
// options, Rescan replaces only that scan (or the cover), and Back to Next Pages returns.
void ui_book_scan_edit_and_rescan_earlier_scans() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_edit_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_edit_project")) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 3);
  // Then: the cover again, the first spread again, the cover once more.
  files << files[0] << files[1] << files[0];
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  // BMP files, as the WIA driver hands over: they must not stay behind in scans/.
  const EnvGuard format("PATCHY_FAKE_BOOK_SCANNER_FORMAT", "bmp");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  for (int i = 0; i < 3; ++i) {
    dialog.start_page_scan();
    wait_for_scan(dialog);
  }
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 3);
  if (dialog.project().scans.size() != 3 || !dialog.project().cover) {
    return;
  }
  CHECK(QDir(project).entryList({QStringLiteral("*.bmp")}, QDir::Files).isEmpty());
  CHECK(QDir(project + QStringLiteral("/scans")).entryList({QStringLiteral("*.bmp")}, QDir::Files).isEmpty());
  QApplication::processEvents();

  // Front cover, six pages, back cover.
  auto* strip = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPageStrip"));
  auto* instruction = dialog.findChild<QLabel*>(QStringLiteral("bookScanInstructionLabel"));
  auto* back = dialog.findChild<QPushButton*>(QStringLiteral("bookScanBackToNextButton"));
  auto* rescan = dialog.findChild<QPushButton*>(QStringLiteral("bookScanRescanButton"));
  auto* use = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUseCombo"));
  auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningUpCombo"));
  CHECK(strip != nullptr && instruction != nullptr && back != nullptr && rescan != nullptr && use != nullptr &&
        up != nullptr);
  if (strip == nullptr || instruction == nullptr || back == nullptr || rescan == nullptr || use == nullptr ||
      up == nullptr) {
    return;
  }
  CHECK(strip->count() == 8);
  CHECK(strip->item(0)->text() == QStringLiteral("Front cover"));
  CHECK(strip->item(1)->text() == QStringLiteral("1"));
  CHECK(strip->item(7)->text() == QStringLiteral("Back cover"));
  CHECK(!strip->item(0)->icon().isNull());
  CHECK(back->isHidden());
  CHECK(instruction->text().contains(QStringLiteral("7")));

  // A click on the front cover shows the cover with its own options.
  const auto click = [strip](int row) {
    strip->scrollToItem(strip->item(row));
    QTest::mouseClick(strip->viewport(), Qt::LeftButton, Qt::NoModifier, strip->visualItemRect(strip->item(row)).center());
  };
  click(0);
  CHECK(process_events_until([&dialog] { return dialog.cover_selected(); }, 3000));
  CHECK(!back->isHidden());
  CHECK(!up->isHidden() && use->isHidden());
  CHECK(rescan->text() == QStringLiteral("Rescan Cover"));
  CHECK(instruction->text().contains(QStringLiteral("Back to Next Pages")));
  CHECK(strip->item(0)->isSelected() && strip->item(7)->isSelected() && !strip->item(1)->isSelected());
  patchy::test::ui::save_widget_artifact("ui_book_scan_cover_selected", dialog);

  // Rescanning the cover keeps the book's settings and pages, and the file's name.
  const QString cover_file = dialog.project().cover->file;
  const auto mode = dialog.project().mode;
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.cover_selected());
  CHECK(dialog.project().cover && dialog.project().cover->file == cover_file);
  CHECK(dialog.project().mode == mode && dialog.project().include_covers);
  CHECK(dialog.project().scans.size() == 3);
  CHECK(QDir(project + QStringLiteral("/scans")).entryList({QStringLiteral("cover-new*")}, QDir::Files).isEmpty());

  // A click on page 2 (the first spread's right page) picks the first scan.
  click(2);
  CHECK(process_events_until([&dialog] { return !dialog.cover_selected() && dialog.selected_scan() == 0; }, 3000));
  CHECK(rescan->text() == QStringLiteral("Rescan This Scan"));
  CHECK(!use->isHidden() && !up->isHidden());
  CHECK(instruction->text().contains(QStringLiteral("1")) && instruction->text().contains(QStringLiteral("Back to Next Pages")));
  CHECK(strip->item(1)->isSelected() && strip->item(2)->isSelected() && !strip->item(3)->isSelected());
  // Its Use choice applies to it, not to the newest scan.
  use->setCurrentIndex(1);  // Left page only
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::LeftOnly);
  CHECK(dialog.project().scans[2].use == patchy::ui::BookPageUse::Both);
  CHECK(strip->count() == 7);

  // Rescan This Scan replaces the first scan in place.
  const QString first_file = dialog.project().scans[0].file;
  const QString last_file = dialog.project().scans[2].file;
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 3);
  CHECK(dialog.project().scans[0].file == first_file && dialog.project().scans[2].file == last_file);
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::LeftOnly);
  CHECK(dialog.selected_scan() == 0);
  CHECK(QDir(project + QStringLiteral("/scans")).entryList({QStringLiteral("*-new*"), QStringLiteral("*.bmp")}, QDir::Files).isEmpty());

  // Top of the book turned half a turn and back again keeps every box.
  const auto before = dialog.project().scans[1].layout;
  dialog.select_cover_for_editing();
  up->setCurrentIndex(2);  // Down
  CHECK(dialog.project().up_turns == 2);
  CHECK(std::abs(dialog.project().scans[1].layout.gutter - (1.0 - before.gutter)) < 1e-9);
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::RightOnly);
  up->setCurrentIndex(0);  // Up
  CHECK(dialog.project().up_turns == 0);
  CHECK(std::abs(dialog.project().scans[1].layout.spread.center_x - before.spread.center_x) < 1e-6);
  CHECK(std::abs(dialog.project().scans[1].layout.spread.center_y - before.spread.center_y) < 1e-6);
  CHECK(std::abs(dialog.project().scans[1].layout.gutter - before.gutter) < 1e-9);
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::LeftOnly);

  // Back to Next Pages shows the newest scan and the next pages again.
  back->click();
  CHECK(!dialog.cover_selected() && dialog.selected_scan() == 2);
  CHECK(back->isHidden());
  CHECK(rescan->text() == QStringLiteral("Rescan Last"));
  CHECK(instruction->text().contains(QStringLiteral("Turn to pages 6 and 7")));

  // From the review step, Edit Scan opens a page's scan; a custom page order stays.
  dialog.finish_scanning();
  auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
  auto* turn = dialog.findChild<QPushButton*>(QStringLiteral("bookScanRotatePagesButton"));
  auto* edit = dialog.findChild<QPushButton*>(QStringLiteral("bookScanEditPageButton"));
  CHECK(pages != nullptr && turn != nullptr && edit != nullptr);
  if (pages == nullptr || turn == nullptr || edit == nullptr) {
    return;
  }
  pages->setCurrentRow(3);  // the second scan's left page
  pages->item(3)->setSelected(true);
  turn->click();
  CHECK(dialog.project().custom_order.has_value());
  const auto order = dialog.project().custom_order.value_or(std::vector<patchy::ui::BookPageRef>{});
  pages->setCurrentRow(3);
  edit->click();
  CHECK(process_events_until([&dialog] { return dialog.step() == patchy::ui::BookScanDialog::Step::Scanning; }, 3000));
  CHECK(dialog.selected_scan() == 1);
  dialog.select_cover_for_editing();
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().custom_order && *dialog.project().custom_order == order);
  // Everything above is in book.json.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->scans.size() == 3 && saved->custom_order && *saved->custom_order == order);
  if (saved && saved->scans.size() == 3) {
    CHECK(saved->scans[0].use == patchy::ui::BookPageUse::LeftOnly);
  }
}

void ui_unicode_book_scan_project_folder() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_unicode_inputs"));
  const QString base = fresh_artifact_folder(QStringLiteral("ui_book_scan_unicode_project"));
  const QString project =
      QDir(base).filePath(QString::fromUtf8(reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
                                            static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())));
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 1).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  scan_book(dialog, project, 1);
  CHECK(QFile::exists(QDir(project).filePath(QStringLiteral("book.json"))));
  CHECK(patchy::ui::load_book_project(project, nullptr).has_value());
  const QString pdf_path = QDir(project).filePath(QString::fromUtf8(
      reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
      static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())) + QStringLiteral(".pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  CHECK(media_boxes(read_file(pdf_path)).size() == 4);
}

// A scanner switched on and off while the window is open (docs/book-scan.md, "Scanners
// turning on and off"): the scanner list, Scan Cover and the resolution list follow
// without Refresh, the open scanner is closed and opened again, a scan the scanner drops
// out of is repeated once, and a scanner that stays off is reported.
void ui_book_scan_follows_scanner_power() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_power_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_power_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 3).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  const EnvGuard devices("PATCHY_FAKE_BOOK_SCANNER_DEVICES", "none");
  const EnvGuard dpis("PATCHY_FAKE_BOOK_SCANNER_DPIS", "150,300");
  const auto set_devices = [](const char* list) {
    qputenv("PATCHY_FAKE_BOOK_SCANNER_DEVICES", list);
    patchy::ui::notify_test_scanner_devices_changed();
  };
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Power Book"));
  auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("bookScanDeviceCombo"));
  auto* cover = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
  auto* skip = dialog.findChild<QPushButton*>(QStringLiteral("bookScanSkipCoverButton"));
  auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("bookScanStatusLabel"));
  CHECK(combo != nullptr && cover != nullptr && skip != nullptr && resolution != nullptr && status != nullptr);
  // Switched off when the window opens.
  CHECK(combo->count() == 1 && combo->currentData().toString().isEmpty());
  CHECK(!cover->isEnabled() && !skip->isEnabled());
  CHECK(!cover->toolTip().isEmpty());
  dialog.show();
  patchy::test::ui::save_widget_artifact("ui_book_scan_power_off", dialog);
  // Switched on: the list fills by itself.
  set_devices("fake=Test scanner");
  CHECK(combo->count() == 1 && combo->currentData().toString() == QStringLiteral("fake"));
  CHECK(cover->isEnabled() && skip->isEnabled() && cover->toolTip().isEmpty());
  patchy::test::ui::save_widget_artifact("ui_book_scan_power_on", dialog);
  // A second scanner appears: the pick stays.
  set_devices("other=Other scanner;fake=Test scanner");
  CHECK(combo->count() == 2 && combo->currentData().toString() == QStringLiteral("fake"));
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Cover);
  CHECK(dialog.scanner_open());
  CHECK(resolution->count() == 2 && resolution->isEnabled());
  resolution->setCurrentIndex(resolution->findData(kDpi));
  // Switched off: the scanner is closed and its capture settings are unavailable.
  set_devices("other=Other scanner");
  CHECK(!dialog.scanner_open());
  CHECK(!resolution->isEnabled());
  // Switched on again, now offering other resolutions: the next scan opens it again
  // (the book's scanner, not the other one) and the resolution list follows.
  qputenv("PATCHY_FAKE_BOOK_SCANNER_DPIS", "300,1200");
  set_devices("other=Other scanner;fake=Test scanner");
  CHECK(resolution->isEnabled());
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.scanner_open());
  CHECK(dialog.project().device_id == QStringLiteral("fake"));
  CHECK(dialog.project().scans.size() == 1);
  CHECK(resolution->count() == 2 && resolution->findData(1200) >= 0 && resolution->currentData().toInt() == kDpi);
  // Switched off and on during a scan: the scan fails, is repeated once, and succeeds.
  dialog.start_page_scan();
  set_devices("other=Other scanner;fake=Test scanner");
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 2);
  CHECK(dialog.scanner_open());
  // Switched off: the window says so until it is back.
  set_devices("other=Other scanner");
  CHECK(!status->text().isEmpty());
  set_devices("other=Other scanner;fake=Test scanner");
  CHECK(status->text().isEmpty());
  // Off for good: the next scan reports it instead of using the other scanner.
  set_devices("other=Other scanner");
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(!dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 2);
}

// Capture settings per scan: a spread scanned in color at 600 dpi among grayscale 300 dpi
// ones, an earlier scan rescanned at other settings, a page with its own output in a
// black and white PDF, and the screen-sized scan copies the editor and preview show.
void ui_book_scan_per_scan_settings_and_page_output() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_settings_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_settings_project")) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 3);
  // The second spread and the rescan of the first are whole-bed images at 600 dpi.
  files[2] = write_bed(inputs, QStringLiteral("spread-2-600.png"), 1800, 1200, {900.0, 600.0, 1440.0, 960.0, 0.0}, true, 80);
  files << write_bed(inputs, QStringLiteral("spread-1-600.png"), 1800, 1200, {880.0, 610.0, 1440.0, 960.0, 0.0}, true, 60);
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningResolutionCombo"));
  auto* color = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningColorCombo"));
  auto* note = dialog.findChild<QLabel*>(QStringLiteral("bookScanCaptureNote"));
  CHECK(resolution != nullptr && color != nullptr && note != nullptr);
  if (resolution == nullptr || color == nullptr || note == nullptr) {
    return;
  }
  CHECK(!resolution->isHidden() && resolution->isEnabled() && color->isEnabled());
  const auto set_capture = [&](int dpi, int mode) {
    resolution->setCurrentIndex(resolution->findData(dpi));
    color->setCurrentIndex(mode);
  };
  set_capture(kDpi, 1);  // Grayscale
  CHECK(dialog.project().capture_mode == patchy::ui::ScanColorMode::Grayscale);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  set_capture(600, 0);  // Color
  dialog.start_page_scan();
  wait_for_scan(dialog);
  set_capture(kDpi, 1);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  const auto& book = dialog.project();
  CHECK(book.scans.size() == 3);
  if (book.scans.size() != 3) {
    return;
  }
  CHECK(book.scans[0].dpi == kDpi && book.scans[0].capture_mode == patchy::ui::ScanColorMode::Grayscale);
  CHECK(book.scans[1].dpi == 600 && book.scans[1].capture_mode == patchy::ui::ScanColorMode::Color);
  CHECK(book.scans[2].dpi == kDpi && book.scans[2].capture_mode == patchy::ui::ScanColorMode::Grayscale);
  CHECK(QImageReader(QDir(project).filePath(book.scans[0].file)).imageFormat() == QImage::Format_Grayscale8);
  // Pages are measured in inches, so the 600 dpi spread has the same page size.
  const auto size = patchy::ui::resolved_page_size(book);
  CHECK(std::abs(size.width - 1.2) < 0.05 && std::abs(size.height - 1.6) < 0.05);
  // Every scan has its view copy beside its thumbnail.
  for (const auto& scan : book.scans) {
    CHECK(QFile::exists(QDir(project).filePath(QStringLiteral("thumbs/") + QFileInfo(scan.file).completeBaseName() +
                                               QStringLiteral("-view.jpg"))));
  }

  // An earlier scan shows its own settings; changing them is for its rescan only.
  dialog.select_scan_for_editing(0);
  CHECK(resolution->currentData().toInt() == kDpi && color->currentIndex() == 1);
  CHECK(!note->isHidden() && note->text().contains(QStringLiteral("300")));
  set_capture(600, 0);
  CHECK(dialog.project().capture_dpi == kDpi && dialog.project().capture_mode == patchy::ui::ScanColorMode::Grayscale);
  CHECK(note->text().contains(QStringLiteral("Rescan This Scan")));
  const QString first_file = book.scans[0].file;
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(book.scans[0].file == first_file && book.scans[0].dpi == 600 &&
        book.scans[0].capture_mode == patchy::ui::ScanColorMode::Color);
  CHECK(std::abs(book.scans[0].layout.spread.width / 600.0 - 2.4) < 0.05);
  CHECK(dialog.project().capture_dpi == kDpi);
  // Back to the next pages: the book's settings again.
  dialog.back_to_next_pages();
  CHECK(resolution->currentData().toInt() == kDpi && color->currentIndex() == 1 && note->isHidden());

  // Review: a black and white book with the color spread's pages in color.
  dialog.finish_scanning();
  auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
  auto* output = dialog.findChild<QComboBox*>(QStringLiteral("bookScanOutputCombo"));
  auto* page_output = dialog.findChild<QComboBox*>(QStringLiteral("bookScanPageOutputCombo"));
  auto* preview = dialog.findChild<QLabel*>(QStringLiteral("bookScanReviewPreview"));
  CHECK(pages != nullptr && output != nullptr && page_output != nullptr && preview != nullptr);
  if (pages == nullptr || output == nullptr || page_output == nullptr || preview == nullptr) {
    return;
  }
  output->setCurrentIndex(2);  // Black and white
  CHECK(pages->count() == 8);  // front cover, six pages, back cover
  pages->clearSelection();
  pages->item(3)->setSelected(true);  // the second scan's two pages
  pages->item(4)->setSelected(true);
  page_output->setCurrentIndex(page_output->findData(static_cast<int>(patchy::BookOutputMode::Color)));
  CHECK(dialog.project().custom_order.has_value());
  const auto order = patchy::ui::book_page_order(dialog.project());
  CHECK(order.size() == 8 && order[3].scan == 1 && order[4].scan == 1);
  CHECK(order[3].output == patchy::BookOutputMode::Color && order[4].output == patchy::BookOutputMode::Color);
  CHECK(!order[2].output.has_value() && !order[5].output.has_value());
  CHECK(pages->item(3)->text().contains(QStringLiteral("Color")) && !pages->item(2)->text().contains(QStringLiteral("(")));
  CHECK(pages->item(3)->isSelected() && pages->item(4)->isSelected() && !pages->item(2)->isSelected());
  CHECK(patchy::ui::page_output_options(dialog.project(), order[3]).mode == patchy::BookOutputMode::Color);
  CHECK(patchy::ui::page_output_options(dialog.project(), order[2]).mode == patchy::BookOutputMode::BlackAndWhite);
  patchy::test::ui::save_widget_artifact("ui_book_scan_page_output", dialog);
  const QString pdf_path = QDir(inputs).filePath(QStringLiteral("mixed.pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  const QByteArray pdf = read_file(pdf_path);
  CHECK(pdf.contains("/BitsPerComponent 1"));
  CHECK(pdf.contains("/DCTDecode"));
  CHECK(media_boxes(pdf).size() == 8);
  // Saved: each scan's mode and each page's own output.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->scans.size() == 3 && saved->custom_order && *saved->custom_order == order);
  if (saved && saved->scans.size() == 3) {
    CHECK(saved->scans[0].capture_mode == patchy::ui::ScanColorMode::Color);
    CHECK(saved->scans[2].capture_mode == patchy::ui::ScanColorMode::Grayscale);
  }

  // The review preview comes from the view copy: it shows a page whose full scan is gone.
  const QString full = QDir(project).filePath(book.scans[2].file);
  const QString aside = full + QStringLiteral(".aside");
  CHECK(QFile::rename(full, aside));
  pages->setCurrentRow(5);  // the third scan's left page
  CHECK(!preview->pixmap().isNull());
  CHECK(QFile::rename(aside, full));
}

// The review list reorders by drag and drop: the selected pages move to the gap under
// the drop, and the order is saved.
void ui_book_scan_review_drag_reorders_pages() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_drag_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_drag_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  scan_book(dialog, project, 2);
  dialog.show();
  auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
  CHECK(pages != nullptr);
  if (pages == nullptr) {
    return;
  }
  QApplication::processEvents();
  const auto before = patchy::ui::book_page_order(dialog.project());
  CHECK(before.size() == 6 && pages->count() == 6);
  if (before.size() != 6 || pages->count() != 6) {
    return;
  }
  // Drag page 1 (row 1) to the gap after the last page.
  const auto drop = [pages](const std::vector<int>& rows, QPoint at) {
    QStringList parts;
    for (const int row : rows) {
      parts << QString::number(row);
    }
    QMimeData mime;
    mime.setData(QStringLiteral("application/x-patchy-book-pages"),
                 QByteArray::number(reinterpret_cast<quintptr>(pages)) + ':' + parts.join(QLatin1Char(',')).toLatin1());
    QDragEnterEvent enter(at, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pages->viewport(), &enter);
    CHECK(enter.isAccepted());
    QDragMoveEvent move(at, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pages->viewport(), &move);
    CHECK(move.isAccepted());
    QDropEvent event(QPointF(at), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pages->viewport(), &event);
    CHECK(event.isAccepted());
    QApplication::processEvents();
  };
  const QRect last = pages->visualItemRect(pages->item(5));
  drop({1}, QPoint(last.right() - 2, last.center().y()));
  auto order = patchy::ui::book_page_order(dialog.project());
  CHECK(dialog.project().custom_order.has_value());
  CHECK(order.size() == 6);
  if (order.size() != 6) {
    return;
  }
  CHECK(order[5] == before[1] && order[0] == before[0] && order[1] == before[2]);
  CHECK(pages->item(5)->isSelected());
  // Two pages dropped on the left half of the first page go to the front, in order.
  pages->clearSelection();
  const QRect first = pages->visualItemRect(pages->item(0));
  drop({3, 4}, QPoint(first.left() + 2, first.center().y()));
  order = patchy::ui::book_page_order(dialog.project());
  CHECK(order.size() == 6 && order[0] == before[4] && order[1] == before[5] && order[2] == before[0]);
  // A drop from somewhere else is refused.
  QMimeData foreign;
  foreign.setData(QStringLiteral("application/x-patchy-book-pages"), QByteArrayLiteral("1:0"));
  QDragEnterEvent enter(first.center(), Qt::MoveAction, &foreign, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(pages->viewport(), &enter);
  CHECK(!enter.isAccepted());
  // The order is saved, and the same move by the test surface agrees.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->custom_order && *saved->custom_order == order);
  dialog.move_review_pages({0}, 6);
  const auto moved = patchy::ui::book_page_order(dialog.project());
  CHECK(moved.size() == 6 && moved[5] == order[0] && moved[0] == order[1]);
  // Back is the row's first button and returns to scanning.
  auto* back = dialog.findChild<QPushButton*>(QStringLiteral("bookScanReviewBackButton"));
  auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("bookScanDeletePagesButton"));
  CHECK(back != nullptr && remove != nullptr);
  if (back != nullptr && remove != nullptr) {
    CHECK(back->text() == QStringLiteral("Back"));
    CHECK(back->geometry().right() < remove->geometry().left());
    back->click();
    CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> book_scan_ui_tests() {
  return {
      {"ui_book_scan_menu_actions_registered", ui_book_scan_menu_actions_registered},
      {"ui_book_scan_cancel_shows_feedback", ui_book_scan_cancel_shows_feedback},
      {"ui_book_scan_skip_cover_sets_top_of_book", ui_book_scan_skip_cover_sets_top_of_book},
      {"ui_book_scan_setup_page_guides_the_cover_scan", ui_book_scan_setup_page_guides_the_cover_scan},
      {"ui_book_scan_full_flow_exports_uniform_pages", ui_book_scan_full_flow_exports_uniform_pages},
      {"ui_book_scan_black_and_white_and_script_export", ui_book_scan_black_and_white_and_script_export},
      {"ui_book_scan_resume_and_rescan", ui_book_scan_resume_and_rescan},
      {"ui_book_scan_one_page_mode_offered_for_wide_book", ui_book_scan_one_page_mode_offered_for_wide_book},
      {"ui_book_scan_switch_pages_per_scan_redetects", ui_book_scan_switch_pages_per_scan_redetects},
      {"ui_book_scan_spine_lines_drag_past_each_other", ui_book_scan_spine_lines_drag_past_each_other},
      {"ui_book_scan_start_over_keeps_folder", ui_book_scan_start_over_keeps_folder},
      {"ui_book_scan_rtl_orders_pages", ui_book_scan_rtl_orders_pages},
      {"ui_book_scan_gutter_drag_sticks", ui_book_scan_gutter_drag_sticks},
      {"ui_book_scan_edit_and_rescan_earlier_scans", ui_book_scan_edit_and_rescan_earlier_scans},
      {"ui_book_scan_follows_scanner_power", ui_book_scan_follows_scanner_power},
      {"ui_book_scan_per_scan_settings_and_page_output", ui_book_scan_per_scan_settings_and_page_output},
      {"ui_book_scan_review_drag_reorders_pages", ui_book_scan_review_drag_reorders_pages},
      {"ui_unicode_book_scan_project_folder", ui_unicode_book_scan_project_folder},
  };
}
